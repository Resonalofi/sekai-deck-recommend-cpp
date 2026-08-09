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

    deckQueue.push(deck);
    while (int(deckQueue.size()) > limit) {
        deckQueue.pop();
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
