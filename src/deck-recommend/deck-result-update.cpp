#include "deck-recommend/deck-result-update.h"

bool RecommendDeck::operator>(const RecommendDeck &other) const
{
    // 先按目标值
    if (targetValue != other.targetValue) return targetValue > other.targetValue;
    // 目标值一样，按C位CardID
    return cards[0].cardId < other.cards[0].cardId;
}

uint64_t getRecommendDeckHash(const RecommendDeck &deck)
{
    // 计算卡组的哈希值
    // 如果分数或者综合不一样，说明肯定不是同一队
    // 如果C位不一样，也不认为是同一队
    uint64_t hash = 0;
    constexpr uint64_t base = 10007;
    hash = hash * base + deck.score;
    // liveScore 必须入键：排序键是 score + liveScore/SCORE_MAX，同一套卡的两个
    // 状态完全可能 score/综合/C位 全同而 liveScore 不同，那是真正不同的结果
    // （优劣由 liveScore 决出）。漏掉它会让 wouldUpdate 把更优的那个当成重复
    // 直接拒掉，且拒掉哪些取决于 limit，表现为放大 limit 反而更差。
    hash = hash * base + deck.liveScore;
    hash = hash * base + deck.power.total;
    hash = hash * base + deck.cards[0].cardId;
    return hash;
}

void RecommendCalcInfo::update(const RecommendDeck &deck, int limit)
{
    // 如果已经足够，判断是否劣于当前最差的
    if (int(deckQueue.size()) >= limit && deckQueue.top() > deck)
        return;

    // 判断是否已经存在，已存在的只补记来源算法
    uint64_t hash = getRecommendDeckHash(deck);
    if (deckQueueHashSet.count(hash)) {
        deckSourceMasks[hash] |= currentAlgorithmMask;
        return;
    }
    deckQueueHashSet.insert(hash);
    deckSourceMasks[hash] = currentAlgorithmMask;
    if (currentTopLevelBranch >= 0)
        deckBranchOrigin[hash] = {currentTopLevelBranch, updateSequence++};

    deckQueue.push(deck);
    while (int(deckQueue.size()) > limit) {
        deckQueue.pop();
    }
    // 队列装满后把局部第 N 名发布给同批线程；只单调升
    if (int(deckQueue.size()) >= limit)
        publishSharedFloor();
}

void RecommendCalcInfo::publishSharedFloor()
{
    if (deckQueue.empty()) return;
    const auto& worst = deckQueue.top();
    if (sharedTargetFloor) {
        double seen = sharedTargetFloor->load(std::memory_order_relaxed);
        while (worst.targetValue > seen &&
               !sharedTargetFloor->compare_exchange_weak(
                   seen, worst.targetValue, std::memory_order_relaxed)) {
        }
    }
    if (sharedPowerFloor) {
        const int power = worst.power.total;
        int seen = sharedPowerFloor->load(std::memory_order_relaxed);
        while (power > seen &&
               !sharedPowerFloor->compare_exchange_weak(
                   seen, power, std::memory_order_relaxed)) {
        }
    }
}

void RecommendCalcInfo::merge(const RecommendCalcInfo &other, int limit)
{
    auto queue = other.deckQueue;
    const uint32_t savedMask = currentAlgorithmMask;
    while (queue.size()) {
        // 沿用对方记录的来源算法，而不是本轮正在运行的算法
        currentAlgorithmMask = other.sourceMaskOf(queue.top());
        update(queue.top(), limit);
        queue.pop();
    }
    currentAlgorithmMask = savedMask;
    is_timeout |= other.is_timeout;
}

void RecommendCalcInfo::mergeByBranchOrder(
    const std::vector<RecommendCalcInfo>& others, int limit
) {
    // 把各线程的卡组按 (顶层分支下标, 线程内序号) 排序后依次入队，
    // 复现串行的访问次序，从而让并列候选的取舍与串行一致。
    struct Entry {
        int branch;
        uint32_t seq;
        const RecommendDeck* deck;
        uint32_t mask;
        bool operator<(const Entry& o) const {
            if (branch != o.branch) return branch < o.branch;
            return seq < o.seq;
        }
    };
    std::vector<RecommendDeck> owned{};
    std::vector<Entry> entries{};
    for (const auto& other : others) {
        auto queue = other.deckQueue;
        while (queue.size()) {
            owned.push_back(queue.top());
            queue.pop();
        }
    }
    entries.reserve(owned.size());
    std::size_t at = 0;
    for (const auto& other : others) {
        auto queue = other.deckQueue;
        while (queue.size()) {
            const auto& deck = owned[at++];
            const uint64_t hash = getRecommendDeckHash(deck);
            const auto it = other.deckBranchOrigin.find(hash);
            entries.push_back(Entry{
                it != other.deckBranchOrigin.end() ? it->second.first : 0,
                it != other.deckBranchOrigin.end() ? it->second.second : 0,
                &deck,
                other.sourceMaskOf(deck),
            });
            queue.pop();
        }
    }
    std::sort(entries.begin(), entries.end());
    const uint32_t savedMask = currentAlgorithmMask;
    for (const auto& entry : entries) {
        currentAlgorithmMask = entry.mask;
        update(*entry.deck, limit);
    }
    currentAlgorithmMask = savedMask;
    for (const auto& other : others)
        is_timeout |= other.is_timeout;
}

uint32_t RecommendCalcInfo::sourceMaskOf(const RecommendDeck &deck) const
{
    auto it = deckSourceMasks.find(getRecommendDeckHash(deck));
    return it == deckSourceMasks.end() ? 0 : it->second;
}

bool RecommendCalcInfo::wouldUpdate(const RecommendCandidate& candidate, int limit)
{
    if (int(deckQueue.size()) >= limit) {
        const auto& worst = deckQueue.top();
        if (worst.targetValue > candidate.targetValue ||
            (worst.targetValue == candidate.targetValue && worst.cards[0].cardId < candidate.leaderCardId)) {
            return false;
        }
    }

    uint64_t hash = 0;
    constexpr uint64_t base = 10007;
    hash = hash * base + candidate.resultScore;
    // 与 getRecommendDeckHash 同键；两侧键不一致会让去重整体失效，必须同步改
    hash = hash * base + candidate.score.liveScore;
    hash = hash * base + candidate.power;
    hash = hash * base + candidate.leaderCardId;
    if (!deckQueueHashSet.count(hash))
        return true;
    // 同一队已经有了，不必再展开，但当前算法也算找到了它
    if (trackAlgorithmSources)
        deckSourceMasks[hash] |= currentAlgorithmMask;
    return false;
}

bool RecommendCalcInfo::isTimeout()
{
    if (++timeout_check_count % 256 != 0) 
        return is_timeout;
    if (std::chrono::high_resolution_clock::now().time_since_epoch().count() - start_ts > timeout) 
        is_timeout = true;
    return is_timeout;
}
