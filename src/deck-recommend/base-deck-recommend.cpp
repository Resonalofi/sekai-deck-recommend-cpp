#include "deck-recommend/base-deck-recommend.h"
#include "card-priority/card-priority-filter.h"
#include "common/timer.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#include <functional>
#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace {

// DFS 并行工作线程的栈大小。递归深度上界 member+1=6 层，大对象都在
// RecommendCalcInfo 里而不在栈帧上，实测 64KB 即可跑对，这里留 8 倍余量。
// 默认 8MB 的线程栈会让 VmData 涨约 28%（纯地址空间保留，RSS 只涨 0.07%）。
constexpr std::size_t kDfsWorkerStackBytes = 512u * 1024u;

#if defined(__unix__) || defined(__APPLE__)
// std::thread 无法指定栈大小，POSIX 下用 pthread 手工建线程。
class SmallStackThread {
    pthread_t handle_{};
    bool joinable_ = false;
    std::function<void()> body_{};

    static void* trampoline(void* raw) {
        auto* self = static_cast<SmallStackThread*>(raw);
        self->body_();
        return nullptr;
    }

public:
    explicit SmallStackThread(std::function<void()> body) : body_(std::move(body)) {
        pthread_attr_t attr{};
        if (pthread_attr_init(&attr) != 0)
            throw std::runtime_error("pthread_attr_init failed");
        pthread_attr_setstacksize(&attr, kDfsWorkerStackBytes);
        const int rc = pthread_create(&handle_, &attr, &trampoline, this);
        pthread_attr_destroy(&attr);
        if (rc != 0)
            throw std::runtime_error("pthread_create failed");
        joinable_ = true;
    }
    SmallStackThread(const SmallStackThread&) = delete;
    SmallStackThread& operator=(const SmallStackThread&) = delete;
    void join() {
        if (joinable_) {
            pthread_join(handle_, nullptr);
            joinable_ = false;
        }
    }
    ~SmallStackThread() { join(); }
};
#else
// 其余平台（含 Windows MSVC）退回 std::thread：行为不变，只是不省 VmData
class SmallStackThread {
    std::thread thread_;
public:
    explicit SmallStackThread(std::function<void()> body) : thread_(std::move(body)) {}
    SmallStackThread(const SmallStackThread&) = delete;
    SmallStackThread& operator=(const SmallStackThread&) = delete;
    void join() { if (thread_.joinable()) thread_.join(); }
};
#endif

}  // namespace



namespace {

// 单调时钟的纳秒读数，用于给各算法计时（timeout 用的 high_resolution_clock 可能回跳）
long long steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace


void BaseDeckRecommend::runRecommendAlgorithm(
    RecommendAlgorithm algorithm,
    int liveType,
    const DeckRecommendConfig& config,
    const EventConfig& eventConfig,
    const std::vector<CardDetail>& pool,
    SupportDeckMap& supportCards,
    const std::function<Score(const DeckScoreDetail&)>& scoreFunc,
    RecommendCalcInfo& info,
    int honorBonus,
    const std::vector<CardDetail>& fixedCards
) {
    const bool isChallengeLive = Enums::LiveType::isChallenge(liveType);

    if (algorithm == RecommendAlgorithm::SA) {
        // 使用模拟退火
        long long seed = config.saSeed;
        if (seed == -1)
            seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();

        auto rng = Rng(seed);
        for (int i = 0; i < config.saRunCount && !info.isTimeout(); i++) {
            findBestCardsSA(
                liveType, config, rng, pool, supportCards, scoreFunc,
                info,
                config.limit, isChallengeLive, config.member, honorBonus,
                eventConfig.eventType, eventConfig.eventId, fixedCards
            );
        }
        return;
    }

    if (algorithm == RecommendAlgorithm::GA) {
        // 使用遗传算法
        long long seed = config.gaSeed;
        if (seed == -1)
            seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();

        auto rng = Rng(seed);
        findBestCardsGA(
            liveType, config, rng, pool, supportCards, scoreFunc,
            info,
            config.limit, isChallengeLive, config.member, honorBonus,
            eventConfig.eventType, eventConfig.eventId, fixedCards
        );
        return;
    }

    if (algorithm == RecommendAlgorithm::DFS) {
        // 使用DFS
        info.deckCards.clear();
        info.deckCharacters = 0;
        info.deckCommonUnitMask = (uint16_t{1} << UNIT_MAX) - 1;
        info.deckAllSameAttr = true;
        info.deckMixedUnitPowerTotals = {};
        std::vector<const CardDetail*> dfsCards;
        dfsCards.reserve(pool.size());
        for (const auto& card : pool)
            dfsCards.push_back(&card);

        if (info.scoreBound.enabled) {
            info.scoreBoundIndex.build(
                dfsCards, info.scoreBoundPowerScratch, info.scoreBoundPowerTops
            );
        }

        // 插入固定卡牌
        for (const auto& card : fixedCards) {
            if (info.deckCards.empty())
                info.deckAttr = card.attr;
            else
                info.deckAllSameAttr &= card.attr == info.deckAttr;
            info.deckCommonUnitMask &= card.unitMask;
            for (int sameAttr = 0; sameAttr < 2; ++sameAttr) {
                info.deckMixedUnitPowerTotals[sameAttr] += std::max(
                    card.powerTotals[0][sameAttr], card.powerTotals[1][sameAttr]
                );
            }
            info.deckCards.push_back(&card);
            info.deckCharacters.flip(card.characterId);
        }

        const bool wlPrune =
            eventConfig.eventType != Enums::EventType::world_bloom || eventConfig.eventUnit != 0;
        // 分区并行会改变顶层遍历顺序，只在「卡组得分与卡内次序无关」时才成立。
        // 三个条件缺一不可：
        //   1. Score 目标——Power/Skill/Mysekai 在 559~576 行用 preCard 做
        //      「肯定弱于上一张已访问卡就跳过」的有损启发式剪枝，结果本身
        //      依赖顶层遍历顺序，分区会改变它们的（近似）结果。
        //   2. 多人——getLiveScoreByDeck 的多人 5 卡快路径把得分算成
        //      skillScoreUps[0] + Σ skillScoreUps[1..4]/5，对位置 1~4 对称，
        //      只依赖谁在位置 0（由最强技能确定性选出）。solo/auto 走通用
        //      路径，那里 skills[cardCount] = skillScoreUps[0] 的环绕元素、
        //      以及 specific 策略的置换都让得分对卡内次序敏感。
        //   3. member == 5——只有满 5 卡才进那条对称快路径；cardCount < 5 会
        //      落到通用路径并触发补零搬移，同样变成次序敏感。
        // 实测依据：auto 与 multi member=4 的用例在仅分区（无共享下界）下即可
        // 复现差异，且同一套卡在不同 limit 下算出不同 liveScore——该性质在
        // 0d9c352（本轮改动之前）就已存在，不是并行引入的。
        const bool orderIndependentScore =
            config.target == RecommendTarget::Score
            && Enums::LiveType::isMulti(liveType)
            && config.member == 5;
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        // 单线程 WASM 构建没有线程支持，分区并行退化为串行
        const int configuredDfsThreads = 1;
#else
        const int configuredDfsThreads = std::max(1, config.dfsParallelThreads);
#endif
        const int dfsThreads = orderIndependentScore ? configuredDfsThreads : 1;
        if (dfsThreads <= 1) {
            findBestCardsDFS(
                liveType, config, dfsCards, supportCards, scoreFunc,
                info,
                config.limit, isChallengeLive, config.member, honorBonus,
                eventConfig.eventType, eventConfig.eventId, fixedCards,
                wlPrune
            );
            return;
        }

        // 顶层按第一张卡分区并行；每线程一份引擎/支援卡组/计算信息，
        // join 后按固定线程下标依次 merge，与双算法并行路径同构
        std::vector<BaseDeckRecommend> dfsEngines(dfsThreads, *this);
        std::vector<SupportDeckMap> dfsSupportCards(dfsThreads, supportCards);
        std::vector<RecommendCalcInfo> dfsInfos(dfsThreads, info);
        std::vector<std::exception_ptr> dfsErrors(dfsThreads);
        // 跨线程共享剪枝下界：已装满线程的局部第 N 名取最大值
        std::atomic<double> sharedTargetFloor{-std::numeric_limits<double>::infinity()};
        std::atomic<int> sharedPowerFloor{std::numeric_limits<int>::min()};
        for (auto& dfsInfoSlot : dfsInfos) {
            dfsInfoSlot.sharedTargetFloor = &sharedTargetFloor;
            dfsInfoSlot.sharedPowerFloor = &sharedPowerFloor;
        }
        std::vector<std::unique_ptr<SmallStackThread>> dfsThreadPool{};
        dfsThreadPool.reserve(dfsThreads - 1);
        auto runPartition = [&](int slot) {
            try {
                dfsEngines[slot].findBestCardsDFS(
                    liveType, config, dfsCards, dfsSupportCards[slot], scoreFunc,
                    dfsInfos[slot],
                    config.limit, isChallengeLive, config.member, honorBonus,
                    eventConfig.eventType, eventConfig.eventId, fixedCards,
                    wlPrune, false, dfsThreads, slot
                );
            }
            catch (...) {
                dfsErrors[slot] = std::current_exception();
            }
        };
        // pthread_create 会因线程数上限或内存压力失败。改动前的实现从不建
        // 线程、永远能返回结果，所以这里不能把「慢一点」变成「请求失败」：
        // 建不出来的分区改由调用线程自己跑完。各分区互不依赖、合并按分支序，
        // 所以谁执行哪个分区不影响结果，只影响调度。
        int dfsSpawned = 0;
        for (int slot = 1; slot < dfsThreads; ++slot) {
            try {
                dfsThreadPool.push_back(std::make_unique<SmallStackThread>(
                    [&runPartition, slot] { runPartition(slot); }
                ));
                ++dfsSpawned;
            }
            catch (const std::runtime_error&) {
                // 建线程失败（POSIX 抛 runtime_error，std::thread 抛
                // system_error，后者也派生自 runtime_error）。剩余分区一并
                // 留给调用线程，不再尝试。
                break;
            }
        }
        runPartition(0);
        for (int slot = 1 + dfsSpawned; slot < dfsThreads; ++slot)
            runPartition(slot);
        for (auto& thread : dfsThreadPool)
            thread->join();
        for (const auto& error : dfsErrors)
            if (error) std::rethrow_exception(error);
        for (auto& dfsInfoSlot : dfsInfos) {
            dfsInfoSlot.sharedTargetFloor = nullptr;
            dfsInfoSlot.sharedPowerFloor = nullptr;
        }
        info.mergeByBranchOrder(dfsInfos, config.limit);
        return;
    }

    throw std::runtime_error("Unknown algorithm: " + std::to_string(int(algorithm)));
}


uint64_t BaseDeckRecommend::calcDeckHash(const std::vector<const CardDetail*>& deck) {
    int card_num = (int)deck.size();
    std::array<int, 5> v{};
    for (int i = 0; i < card_num; ++i)
        v[i] = deck[i]->cardId;
    std::sort(v.begin() + 1, v.begin() + card_num);
    constexpr uint64_t base = 10007;
    uint64_t hash = 0;
    for (int i = 0; i < card_num; ++i) 
        hash = hash * base + v[i];
    return hash;
};


/*
获取当前卡组的最佳排列
*/
BestPermutationResult BaseDeckRecommend::getBestPermutation(
    DeckCalculator& deckCalculator,
    const std::vector<const CardDetail*> &deckCards,
    SupportDeckMap& supportCards,
    const std::function<Score(const DeckScoreDetail &)> &scoreFunc,
    int honorBonus,
    std::optional<int> eventType,
    std::optional<int> eventId,
    int liveType,
    const DeckRecommendConfig& config
) const {
    bool bestSkillAsLeader = config.bestSkillAsLeader;
    // 存在固定队长角色则不允许把技能最强的换到队长
    if (config.fixedCharacters.size()) bestSkillAsLeader = false;
    // 终章活动不允许把技能最强的换到队长
    if (eventId.has_value() && isFinalChapterEvent(eventId.value())) bestSkillAsLeader = false;
    struct BestPermutationContext {
        const std::function<Score(const DeckScoreDetail &)> &scoreFunc;
        const DeckRecommendConfig& config;
        BestPermutationResult result{};
        double maxValue{};
    } context{scoreFunc, config};
    const auto consumeState = [context = &context](
        int power,
        double eventBonus,
        double supportDeckBonus,
        const std::array<double, 5>& skillScoreUps,
        int leaderCardId,
        int statusMask,
        double multiLiveScoreUp
    ) {
        DeckScoreDetail scoreDetail{
            .power = {.total = power},
            .eventBonus = eventBonus,
            .supportDeckBonus = supportDeckBonus,
            .skillScoreUps = skillScoreUps,
            .cardCount = 5,
            .multiLiveScoreUp = multiLiveScoreUp,
        };
        const auto score = context->scoreFunc(scoreDetail);
        const double value = score.score + score.liveScore * 1e-7;

        context->result.maxTargetValue = std::max(context->result.maxTargetValue, value);
        context->result.maxMultiLiveScoreUp = std::max(
            context->result.maxMultiLiveScoreUp, multiLiveScoreUp
        );
        if (multiLiveScoreUp < context->config.multiScoreUpLowerBound)
            return;

        if (value > context->maxValue) {
            context->maxValue = value;
            RecommendCandidate candidate{
                .score = score,
                .multiLiveScoreUp = multiLiveScoreUp,
                .power = power,
                .leaderCardId = leaderCardId,
                .statusMask = statusMask,
            };
            if (context->config.target == RecommendTarget::Mysekai) {
                candidate.targetValue = score.mysekaiInternalPoint;
                candidate.resultScore = 0;
            }
            else {
                candidate.resultScore = score.score;
                if (context->config.target == RecommendTarget::Power)
                    candidate.targetValue = candidate.power + double(score.score) / SCORE_MAX;
                else if (context->config.target == RecommendTarget::Skill)
                    candidate.targetValue = multiLiveScoreUp + double(score.score) / SCORE_MAX;
                else
                    candidate.targetValue = score.score + double(score.liveScore) / SCORE_MAX;
            }
            context->result.bestCandidate = candidate;
        }
    };

    if (config.target == RecommendTarget::Score &&
        deckCards.size() == 5 && Enums::LiveType::isMulti(liveType)) {
        deckCalculator.forEachMultiLiveScoreState(
            deckCards,
            supportCards,
            [&consumeState](const MultiLiveScoreStateView& state) {
                consumeState(
                    state.power,
                    state.eventBonus,
                    state.supportDeckBonus,
                    state.skillScoreUps,
                    state.leaderCardId,
                    state.statusMask,
                    state.multiLiveScoreUp
                );
            },
            honorBonus,
            eventType,
            eventId,
            config.skillReferenceChooseStrategy,
            config.keepAfterTrainingState,
            bestSkillAsLeader
        );
    }
    else {
        deckCalculator.forEachDeckState(
            deckCards,
            supportCards,
            [&consumeState](const DeckStateView& state) {
                std::array<double, 5> skillScoreUps{};
                for (int pos = 0; pos < 5; ++pos)
                    skillScoreUps[pos] = state.skills[state.order[pos]].scoreUp;
                consumeState(
                    state.power.total.total,
                    state.eventBonus.totalBonus,
                    state.supportDeckBonus,
                    skillScoreUps,
                    state.cardDetails[state.order[0]]->cardId,
                    state.statusMask,
                    state.multiLiveScoreUp
                );
            },
            honorBonus,
            eventType,
            eventId,
            config.skillReferenceChooseStrategy,
            config.keepAfterTrainingState,
            bestSkillAsLeader,
            /*slimPower=*/true
        );
    }
    return context.result;
}

RecommendDeck BaseDeckRecommend::materializeCandidate(
    DeckCalculator& deckCalculator,
    const std::vector<const CardDetail*>& deckCards,
    SupportDeckMap& supportCards,
    int honorBonus,
    std::optional<int> eventType,
    std::optional<int> eventId,
    const DeckRecommendConfig& config,
    const RecommendCandidate& candidate
) const {
    bool bestSkillAsLeader = config.bestSkillAsLeader;
    if (config.fixedCharacters.size()) bestSkillAsLeader = false;
    if (eventId.has_value() && isFinalChapterEvent(eventId.value())) bestSkillAsLeader = false;

    auto deckDetails = deckCalculator.getDeckDetailByCards(
        deckCards,
        supportCards,
        honorBonus,
        eventType,
        eventId,
        config.skillReferenceChooseStrategy,
        config.keepAfterTrainingState,
        bestSkillAsLeader,
        candidate.statusMask
    );
    return RecommendDeck(deckDetails.front(), config.target, candidate.score);
}


UserCard BaseDeckRecommend::makeVirtualUserCard(int cardId) const
{
    UserCard uc;
    uc.cardId = cardId;
    uc.level = 1;
    uc.skillLevel = 1;
    uc.masterRank = 0;
    uc.specialTrainingStatus = Enums::SpecialTrainingStatus::not_doing;

    auto& c = findOrThrow(this->dataProvider.masterData->cards, [&](const Card& it) {
        return it.id == cardId;
    }, [&]() { return "Card not found for cardId=" + std::to_string(cardId); });
    bool hasSpecialTraining = c.cardRarityType == Enums::CardRarityType::rarity_3
                            || c.cardRarityType == Enums::CardRarityType::rarity_4;
    uc.defaultImage = hasSpecialTraining ? Enums::DefaultImage::special_training : Enums::DefaultImage::original;

    for (auto& ep : this->dataProvider.masterData->cardEpisodes)
        if (ep.cardId == cardId) {
            UserCardEpisodes uce{};
            uce.cardEpisodeId = ep.id;
            uce.scenarioStatus = 0;
            uc.episodes.push_back(uce);
        }
    return uc;
}

void BaseDeckRecommend::addEventBonusCardsToPool(
    int eventId,
    std::vector<UserCard>& userCards,
    DeckRecommendConfig& config
) const
{
    for (const auto& eventCard : this->dataProvider.masterData->eventCards) {
        if (eventCard.eventId != eventId || eventCard.bonusRate <= 0)
            continue;
        // 显式指定的单卡配置优先，这类卡完全不受全当期配置影响
        if (config.singleCardConfig.count(eventCard.cardId))
            continue;
        config.singleCardConfig[eventCard.cardId] = config.bonusCardConfig;

        const auto& card = findOrThrow(this->dataProvider.masterData->cards, [&](const Card& it) {
            return it.id == eventCard.cardId;
        }, [&]() { return "Card not found for cardId=" + std::to_string(eventCard.cardId); });

        // 支援卡组读的是卡牌原始状态而非单卡配置，所以把配置直接套用到卡牌上，
        // 让当期卡的专精/技能等级在主队伍与支援卡组两侧一致
        const auto owned = std::find_if(userCards.begin(), userCards.end(), [&](const UserCard& it) {
            return it.cardId == eventCard.cardId;
        });
        if (owned != userCards.end()) {
            *owned = this->cardService.applyCardConfig(*owned, card, config.bonusCardConfig);
            continue;
        }
        // 未拥有的生成虚拟卡加入卡池
        userCards.push_back(this->cardService.applyCardConfig(
            makeVirtualUserCard(eventCard.cardId), card, config.bonusCardConfig
        ));
    }
}

RecommendResult BaseDeckRecommend::recommendHighScoreDeck(
    const std::vector<UserCard> &userCards,
    ScoreFunction scoreFunc,
    const DeckRecommendConfig &config,
    int liveType,
    const EventConfig &eventConfig)
{
    this->dataProvider.init();

    // 暂不支持同时指定固定卡牌和固定角色
    if (config.fixedCards.size() && config.fixedCharacters.size())
        throw std::runtime_error("Cannot set both fixed cards and fixed characters");
    // 挑战live不允许指定固定角色
    if (Enums::LiveType::isChallenge(liveType) && config.fixedCharacters.size())
        throw std::runtime_error("Cannot set fixed characters in challenge live");

    auto musicMeta = this->liveCalculator.getMusicMeta(config.musicId, config.musicDiff);

    auto areaItemLevels = areaItemService.getAreaItemLevels();

    std::optional<double> scoreUpLimit = std::nullopt;
    // 终章技能加分上限为140
    if (isFinalChapterEvent(eventConfig.eventId) && !Enums::LiveType::isChallenge(liveType))
        scoreUpLimit = 140.0;

    auto cards = cardCalculator.batchGetCardDetail(
        userCards, config.cardConfig, config.singleCardConfig, 
        eventConfig, areaItemLevels, scoreUpLimit
    );

    // 归类支援卡组；索引只需覆盖前 32 位，实际最多取 25 张并排除 5 张主卡。
    SupportDeckMap supportCards{};
    int maxSupportCardId = 0;
    if (isFinalChapterEvent(eventConfig.eventId) ||
        eventConfig.eventType == Enums::EventType::world_bloom) {
        for (const auto& card : userCards)
            maxSupportCardId = std::max(maxSupportCardId, card.cardId);
    }
    const auto buildSupportCards = [&](int specialCharacterId) {
        SupportDeckCards result;
        result.cards.reserve(userCards.size());
        for (const auto& card : userCards) {
            result.cards.push_back(this->cardCalculator.getSupportDeckCard(
                card, eventConfig.eventId, specialCharacterId
            ));
        }
        std::sort(result.cards.begin(), result.cards.end(), [](const SupportDeckCard& a, const SupportDeckCard& b) {
            return a.bonus > b.bonus;
        });
        result.topRankByCardId.assign(maxSupportCardId + 1, 32);
        const auto indexedCount = std::min<std::size_t>(result.cards.size(), 32);
        for (std::size_t i = 0; i < indexedCount; ++i)
            result.topRankByCardId[result.cards[i].cardId] = static_cast<uint8_t>(i);
        return result;
    };
    if (isFinalChapterEvent(eventConfig.eventId)) {
        // 终章对每个角色都算一个支援卡组排序
        for (int i = 1; i <= 26; i++)
            supportCards[i] = buildSupportCards(i);
    } else if(eventConfig.eventType == Enums::EventType::world_bloom) {
        // 普通wl只算一个支援卡组排序
        supportCards[0] = buildSupportCards(eventConfig.specialCharacterId);
    }

    // 过滤箱活的卡，不上其它组合的
    if (eventConfig.eventUnit && config.filterOtherUnit) {
        std::vector<CardDetail> newCards{};
        for (const auto& card : cards) {
            if (card.unitMask == (uint16_t{1} << Enums::Unit::piapro)
                || (card.unitMask & (uint16_t{1} << eventConfig.eventUnit))) {
                newCards.push_back(card);
            }
        }
        cards = std::move(newCards);
    }

    // 获取固定卡牌
    std::vector<CardDetail> fixedCards{};
    for (auto card_id : config.fixedCards) {
        // 从当前卡牌中找到对应的卡牌
        auto it = std::find_if(cards.begin(), cards.end(), [&](const CardDetail& card) {
            return card.cardId == card_id;
        });
        if (it != cards.end()) {
            fixedCards.push_back(*it);
        } else {
            // 找不到的情况下，生成一个初始养成情况的卡牌
            auto uc = makeVirtualUserCard(card_id);
            auto card = cardCalculator.batchGetCardDetail(
                {uc}, config.cardConfig, config.singleCardConfig,
                eventConfig, areaItemLevels, scoreUpLimit
            );
            if (card.size() > 0) {
                fixedCards.push_back(card[0]);
                cards.push_back(card[0]);
            } else {
                throw std::runtime_error("Failed to generate virtual card for fixed card id: " + std::to_string(card_id));
            }
        }
    }
    // 检查固定卡牌是否有效
    if (fixedCards.size()) {
        std::set<int> fixedCardIds{};
        std::set<int> fixedCardCharacterIds{};
        for (const auto& card : fixedCards) {
            fixedCardIds.insert(card.cardId);
            fixedCardCharacterIds.insert(card.characterId);
        }
        if (int(fixedCards.size()) > config.member) {
            throw std::runtime_error("Fixed cards size is larger than member size");
        }
        if (fixedCardIds.size() != fixedCards.size()) {
            throw std::runtime_error("Fixed cards have duplicate cards");
        }
        if (Enums::LiveType::isChallenge(liveType)) {
            if (fixedCardCharacterIds.size() != 1 || fixedCards[0].characterId != cards[0].characterId) {
                throw std::runtime_error("Fixed cards have invalid characters");
            }
        } else {
            if (fixedCardCharacterIds.size() != fixedCards.size()) {
                throw std::runtime_error("Fixed cards have duplicate characters");
            }
        }
    }

    auto honorBonus = deckCalculator.getHonorBonusPower();

    RecommendResult result{};
    auto& ans = result.decks;
    std::vector<CardDetail> cardDetails{};
    // 只用得到上一轮筛选出的卡牌数量，不必留一份 CardDetail 副本（804 张卡 1.17MiB）
    size_t preCardCount = 0;
    auto sf = [&scoreFunc, &musicMeta](const DeckScoreDetail& deckDetail) { return scoreFunc(musicMeta, deckDetail); };

    RecommendCalcInfo calcInfo{};
    calcInfo.start_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    calcInfo.timeout = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(config.timeout_ms)).count();

    // 分数上界整枝的适用条件：分数目标、非挑战Live。
    // 单人/自动分数按位取用各卡技能，上界只有排序后的支配关系，指定技能顺序时按位
    // 比较不成立，故排除；多人分数只由实效聚合值决定，指定顺序也安全。
    auto& scoreBound = calcInfo.scoreBound;
    scoreBound.enabled =
        config.target == RecommendTarget::Score &&
        !Enums::LiveType::isChallenge(liveType) &&
        (Enums::LiveType::isMulti(liveType) || config.liveSkillOrder != LiveSkillOrder::specific);
    if (scoreBound.enabled) {
        for (const auto& card : cards) {
            if (card.maxEventBonus.value_or(0.0) > 0.0) {
                scoreBound.hasEventBonus = true;
                break;
            }
        }
    }
    if (scoreBound.enabled && eventConfig.eventType == Enums::EventType::world_bloom) {
        // 异色加成取加成表最大档
        for (const auto& it : this->dataProvider.masterData->worldBloomDifferentAttributeBonuses)
            scoreBound.diffAttrBonus = std::max(scoreBound.diffAttrBonus, it.bonusRate);
        // 终章2的shuffle unit bonus同样是队伍级加成，按最大档计入上界
        if (eventConfig.eventId == finalChapter2EventId)
            scoreBound.diffAttrBonus += 50.0;
        // 异色加成也要计入加成上界
        scoreBound.hasEventBonus |= scoreBound.diffAttrBonus > 0.0;
        // 支援加成取每组排序后前 N 张之和的最大值；实际值还要排除主队伍卡牌，只会更小
        int supportDeckCount = deckCalculator.getWorldBloomSupportDeckCount(eventConfig.eventId);
        for (const auto& [characterId, sortedSupportCards] : supportCards) {
            double bonus = 0;
            int count = std::min(supportDeckCount, int(sortedSupportCards.cards.size()));
            for (int i = 0; i < count; ++i)
                bonus += sortedSupportCards.cards[i].bonus;
            scoreBound.supportDeckBonus = std::max(scoreBound.supportDeckBonus, bonus);
        }
    }

    // 组合算法：algorithms 非空时忽略 algorithm，否则退化为单算法
    std::vector<RecommendAlgorithm> algorithms = config.algorithms;
    if (algorithms.empty())
        algorithms.push_back(config.algorithm);
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    // 单线程 WASM 构建没有线程支持，并行退化为串行
    const bool runAlgorithmsInParallel = false;
#else
    const bool runAlgorithmsInParallel = config.parallelAlgorithms && algorithms.size() > 1;
#endif
    // 只运行一个算法时来源无需跨算法汇总
    calcInfo.trackAlgorithmSources = algorithms.size() > 1;

    // 指定活动加成组卡
    if (config.target == RecommendTarget::Bonus) {
        if (eventConfig.eventType == 0)
            throw std::runtime_error("Bonus target requires event");
        if (std::any_of(algorithms.begin(), algorithms.end(),
                [](RecommendAlgorithm it) { return it != RecommendAlgorithm::DFS; }))
            throw std::runtime_error("Bonus target only supports DFS algorithm");

        // WL和普通活动采用不同代码
        calcInfo.currentAlgorithmMask = recommendAlgorithmBit(RecommendAlgorithm::DFS);
        const long long bonusStartNs = steadyNowNs();
        if (eventConfig.eventType != Enums::EventType::world_bloom) {
            findTargetBonusCardsDFS(
                liveType, config, cards, sf, calcInfo,
                config.limit, config.member, eventConfig.eventType, eventConfig.eventId
            );
        }
        else {
            findWorldBloomTargetBonusCardsDFS(
                liveType, config, cards, sf, calcInfo,
                config.limit, config.member, eventConfig.eventType, eventConfig.eventId
            );
        }
        result.algorithmNs[int(RecommendAlgorithm::DFS)] += steadyNowNs() - bonusStartNs;

        while (calcInfo.deckQueue.size()) {
            ans.emplace_back(calcInfo.deckQueue.top());
            ans.back().algorithmMask = calcInfo.sourceMaskOf(ans.back());
            calcInfo.deckQueue.pop();
        }
        // 按照活动加成从小到大排序，同加成按分数从小到大排序
        std::sort(ans.begin(), ans.end(), [](const RecommendDeck& a, const RecommendDeck& b) {
            return std::tuple(-a.eventBonus.value_or(0), a.targetValue) > std::tuple(-b.eventBonus.value_or(0), b.targetValue);
        });
        return result;
    }

    // 最优化组卡
    auto sortByStrength = [&config](std::vector<CardDetail>& target) {
        // 卡牌大致按强度排序，保证dfs先遍历强度高的卡组
        if (config.target == RecommendTarget::Skill) {
            std::sort(target.begin(), target.end(), [](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(a.skill.max, a.skill.min, a.cardId) > std::make_tuple(b.skill.max, b.skill.min, b.cardId);
            });
        } else {
            std::sort(target.begin(), target.end(), [](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(a.power.max, a.power.min, a.cardId) > std::make_tuple(b.power.max, b.power.min, b.cardId);
            });
        }
    };

    const bool usesDfs = std::any_of(algorithms.begin(), algorithms.end(),
        [](RecommendAlgorithm it) { return it == RecommendAlgorithm::DFS; });
    const bool usesRandomized = std::any_of(algorithms.begin(), algorithms.end(),
        [](RecommendAlgorithm it) { return it != RecommendAlgorithm::DFS; });
    // 随机化算法用全部卡牌，DFS 用按优先级筛选后的卡牌；两者的卡池都只排一次
    std::vector<CardDetail> randomizedPool{};
    if (usesRandomized) {
        randomizedPool = cards;
        sortByStrength(randomizedPool);
    }

    while (true) {
        size_t cardCount = cards.size();
        if (usesDfs) {
            // DFS 为了优化性能，会根据活动加成和卡牌稀有度优先级筛选卡牌
            cardDetails = filterCardPriority(liveType, eventConfig.eventType, cards, preCardCount, config.member);
            cardCount = cardDetails.size();
        }
        // 随机化算法不需要过滤，直接用全部卡牌排序后的 randomizedPool
        if (cardCount == preCardCount) {
            if (ans.empty())    // 如果所有卡牌都上阵了还是组不出队伍，就报错
                throw std::runtime_error("Cannot recommend any deck in " + std::to_string(cards.size()) + " cards");
            else    // 返回上次组出的队伍
                break;
        }
        preCardCount = cardCount;
        std::vector<CardDetail> cardsSortedByStrength{};
        if (usesDfs) {
            cardsSortedByStrength = cardDetails;
            sortByStrength(cardsSortedByStrength);
        }

        auto poolFor = [&](RecommendAlgorithm algorithm) -> const std::vector<CardDetail>& {
            return algorithm == RecommendAlgorithm::DFS ? cardsSortedByStrength : randomizedPool;
        };

        if (runAlgorithmsInParallel) {
            // 并行：每个算法一份独立的引擎、支援卡组与计算信息，结束后合并结果
            std::vector<BaseDeckRecommend> engines(algorithms.size(), *this);
            std::vector<SupportDeckMap> algorithmSupportCards(
                algorithms.size(), supportCards
            );
            std::vector<RecommendCalcInfo> infos(algorithms.size(), calcInfo);
            std::vector<std::exception_ptr> errors(algorithms.size());
            std::vector<long long> elapsedNs(algorithms.size(), 0);
            std::vector<std::thread> threads{};
            threads.reserve(algorithms.size());
            for (size_t i = 0; i < algorithms.size(); ++i) {
                infos[i].currentAlgorithmMask = recommendAlgorithmBit(algorithms[i]);
                threads.emplace_back([&, i] {
                    const long long startNs = steadyNowNs();
                    try {
                        engines[i].runRecommendAlgorithm(
                            algorithms[i], liveType, config, eventConfig, poolFor(algorithms[i]),
                            algorithmSupportCards[i], sf, infos[i], honorBonus, fixedCards
                        );
                    }
                    catch (...) {
                        errors[i] = std::current_exception();
                    }
                    elapsedNs[i] = steadyNowNs() - startNs;
                });
            }
            for (auto& thread : threads)
                thread.join();
            for (size_t i = 0; i < algorithms.size(); ++i)
                result.algorithmNs[int(algorithms[i])] += elapsedNs[i];
            for (const auto& error : errors)
                if (error) std::rethrow_exception(error);
            for (const auto& info : infos)
                calcInfo.merge(info, config.limit);
        }
        else {
            // 串行：共用同一份结果，后跑的算法可以直接用已有最优解剪枝
            for (const auto algorithm : algorithms) {
                calcInfo.currentAlgorithmMask = recommendAlgorithmBit(algorithm);
                const long long startNs = steadyNowNs();
                runRecommendAlgorithm(
                    algorithm, liveType, config, eventConfig, poolFor(algorithm),
                    supportCards, sf, calcInfo, honorBonus, fixedCards
                );
                result.algorithmNs[int(algorithm)] += steadyNowNs() - startNs;
            }
        }

        ans.clear();
        auto q = calcInfo.deckQueue;
        while (q.size()) {
            ans.emplace_back(q.top());
            ans.back().algorithmMask = calcInfo.sourceMaskOf(ans.back());
            q.pop();
        }
        std::reverse(ans.begin(), ans.end());
        if (int(ans.size()) >= config.limit || calcInfo.isTimeout())
            break;
    }

    return result;
}
