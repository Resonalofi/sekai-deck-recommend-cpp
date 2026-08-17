#ifndef DECK_RESULT_UPDATE_H
#define DECK_RESULT_UPDATE_H

#include "deck-information/deck-calculator.h"
#include "live-score/live-calculator.h"
#include <array>
#include <set>
#include <queue>
#include <bitset>

enum class RecommendTarget {
    Score,
    Power,
    Skill,
    Bonus,
    Mysekai,
};

constexpr double SCORE_MAX = 10000000;
constexpr double POWER_MAX = 500000;
constexpr double SKILL_MAX = 500 * 10;  // 实效可能有一位小数点

struct RecommendDeck : DeckDetail {
    // 实际分数或pt
    int score;
    // 游玩歌曲分数
    int liveScore;
    // 多人技能加成（实效）
    double multiLiveScoreUp;
    // 优化目标值（不一定是分数）
    double targetValue;
    // Mysekai活动点数
    int mysekaiEventPoint;
    // 找出这一队的算法集合（按 RecommendAlgorithm 取值置位），取出结果时才填
    uint32_t algorithmMask = 0;

    RecommendDeck() = default;

    RecommendDeck(const DeckDetail &deckDetail, RecommendTarget target, Score s)
        : DeckDetail(deckDetail) {
            if (target == RecommendTarget::Mysekai) {
                // 烤森目标值
                this->targetValue = s.mysekaiInternalPoint;
                this->mysekaiEventPoint = s.mysekaiEventPoint;
                this->score = 0;
                this->liveScore = 0;
                this->multiLiveScoreUp = 0;
            } 
            else {
                this->score = s.score;
                this->liveScore = s.liveScore;
                this->multiLiveScoreUp = deckDetail.multiLiveScoreUp;
                this->mysekaiEventPoint = 0;

                int power = deckDetail.power.total;
                // 根据不同优化目标计算目标值
                if (target == RecommendTarget::Power) {
                    targetValue = power + double(score) / SCORE_MAX;
                } else if (target == RecommendTarget::Skill) {
                    targetValue = multiLiveScoreUp + double(score) / SCORE_MAX;
                } else {
                    targetValue = score + double(liveScore) / SCORE_MAX;
                }
            }
        }

    bool operator>(const RecommendDeck &other) const;
};

struct RecommendCandidate {
    Score score{};
    double targetValue = 0;
    double multiLiveScoreUp = 0;
    int power = 0;
    int leaderCardId = 0;
    int statusMask = 0;
    int resultScore = 0;
};


// 分数上界整枝所需的、与递归节点无关的预计算量
struct ScoreUpperBoundInfo {
    bool enabled = false;
    // 卡池里是否存在非零活动加成；无活动时为 false，可整块跳过加成上界的聚合
    bool hasEventBonus = false;
    // WL 异色加成上界，取加成表中的最大档（非 WL 为 0）
    double diffAttrBonus = 0.0;
    // 支援卡组加成上界，取排序后前 N 张之和（非 WL 为 0）
    // 实际值还要排除主队伍里的卡，只会更小
    double supportDeckBonus = 0.0;
};


// DFS 分数上界的卡池索引；综合力表的存储由 RecommendCalcInfo 的复用缓冲提供，
// 避免把大数组插入递归热字段之间。
struct DfsScoreBoundIndex {
    static constexpr int attrCount = 16;
    static constexpr int characterCount = 32;

    std::array<double, characterCount> skills{};
    std::array<double, characterCount> bonuses{};
    uint16_t attrs = 0;
    uint16_t units = 0;

    void build(
        const std::vector<const CardDetail*>& cards,
        std::vector<int>& powers
    );

    const int* powerRow(
        const std::vector<int>& powers,
        int attr,
        int unit
    ) const;
};


// 存储卡组推荐计算的结果以及过程中需要记录的信息
struct RecommendCalcInfo {
    long long start_ts = 0;
    long long timeout = std::numeric_limits<long long>::max();
    int timeout_check_count = 0;
    bool is_timeout = false;
    std::priority_queue<RecommendDeck, std::vector<RecommendDeck>, std::greater<>> deckQueue = {};
    // 已出现过的卡组哈希；DFS/GA 的每个候选都要查一次，保持紧凑
    std::unordered_set<uint64_t> deckQueueHashSet = {};

    std::vector<const CardDetail*> deckCards = {};
    std::bitset<32> deckCharacters = 0;
    uint16_t deckCommonUnitMask = 0;
    int deckAttr = 0;
    bool deckAllSameAttr = true;
    std::array<int, 2> deckMixedUnitPowerTotals{};
    std::unordered_map<uint64_t, double> deckTargetValueMap{};
    // 分数上界整枝的预计算量，由 recommendHighScoreDeck 一次算好
    ScoreUpperBoundInfo scoreBound{};
    // 基础卡池的 (属性,组合)x角色 综合力索引存储
    std::vector<int> scoreBoundPowerScratch{};
    // 首张卡兼容候选列表缓冲，仅深度 cIndex+1 构建，复用避免逐节点分配
    std::vector<const CardDetail*> compatibleScratch{};

    // 以下几项与 DFS 递归状态无关，放在结构体末尾，避免挤走上面这些热字段的偏移
    // 与 deckQueueHashSet 同键，记录找出该卡组的算法集合
    std::unordered_map<uint64_t, uint32_t> deckSourceMasks = {};
    // 当前正在运行的算法的置位，由 recommendHighScoreDeck 在每次运行算法前设置
    uint32_t currentAlgorithmMask = 0;
    // 是否需要在候选被去重时补记来源算法；只运行一个算法时来源必然是它自己，
    // 省掉候选热路径上的第二次哈希查找
    bool trackAlgorithmSources = false;
    // 上界索引元数据和兼容子池缓冲放在末尾，避免改变上面的递归热字段偏移
    DfsScoreBoundIndex scoreBoundIndex{};
    std::vector<int> compatibleScoreBoundPowers{};
    DfsScoreBoundIndex compatibleScoreBoundIndex{};

    // 添加一个新结果
    void update(const RecommendDeck &deck, int limit);

    // 合并另一份计算结果（用于并行运行多个算法后汇总）
    void merge(const RecommendCalcInfo &other, int limit);

    // 取出结果时查询某一队由哪些算法找出
    uint32_t sourceMaskOf(const RecommendDeck &deck) const;

    // 判断候选是否值得展开成完整结果；已存在的同一队会记上当前算法
    bool wouldUpdate(const RecommendCandidate& candidate, int limit);

    // 检查是否超时
    bool isTimeout();
};


#endif
