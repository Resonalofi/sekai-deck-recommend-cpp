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


// 存储卡组推荐计算的结果以及过程中需要记录的信息
struct RecommendCalcInfo {
    long long start_ts = 0;
    long long timeout = std::numeric_limits<long long>::max();
    int timeout_check_count = 0;
    bool is_timeout = false;
    std::priority_queue<RecommendDeck, std::vector<RecommendDeck>, std::greater<>> deckQueue = {};
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
    // 无活动剪枝的 (属性,组合)x角色 综合力聚合缓冲，复用避免逐节点分配
    std::vector<int> prunePowerScratch{};
    // 首张卡兼容候选列表缓冲，仅深度 cIndex+1 构建，复用避免逐节点分配
    std::vector<const CardDetail*> compatibleScratch{};

    // 添加一个新结果
    void update(const RecommendDeck &deck, int limit);

    // 合并另一份计算结果（用于并行运行多个算法后汇总）
    void merge(const RecommendCalcInfo &other, int limit);

    bool wouldUpdate(const RecommendCandidate& candidate, int limit) const;

    // 检查是否超时
    bool isTimeout();
};


#endif
