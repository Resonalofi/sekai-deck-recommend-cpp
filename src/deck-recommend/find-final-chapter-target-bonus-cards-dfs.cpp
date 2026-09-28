#include "deck-recommend/base-deck-recommend.h"

#include <bit>


namespace {

// 终章里加成计算完全等价的一组卡：同角色、同属性、同组合，且各项加成相同
struct FinalChapterBonusGroup {
    int bonus;          // 非队长位置的加成*2（角色、突破、当期）
    int leaderBonus;    // 担任队长时额外的加成*2（章节称号、当期队长）
    int limitedBonus;   // 当期加成*2，终章1五张全是当期卡时第5张的不生效
    int chara;
    int attr;
    int unit;           // 终章2组合数加成的计数组合
    std::vector<const CardDetail*> cards;
};

// 命中目标的一套卡：各组下标（搜索顺序）与能凑出该加成的队长位置
struct FinalChapterBonusResult {
    std::vector<int> groups;
    std::vector<int> leaders;
};

// 与请求相关、与搜索节点无关的量
struct FinalChapterBonusContext {
    std::vector<FinalChapterBonusGroup> groups; // 按非队长加成升序
    std::array<int, 6> diffAttr{};              // WL异色加成*2，按属性数
    std::array<int, 6> shuffleUnit{};           // 终章2组合数加成*2，按组合数
    bool limitedCap = false;                    // 终章1满5人时最多生效4个当期
    int maxLimited = 0;
    int minLeader = 0;
    int maxLeader = 0;
    uint32_t requiredCharacters = 0;
    std::map<int, int> fixedGroupByCharacter{}; // 固定卡牌所在的组，该角色只能取这一组
    int leaderCharacter = 0;                    // 固定角色的第一位即队长
};

bool isHalfPercentMultiple(double value) {
    return std::abs(std::round(value * 2) - value * 2) <= 1e-6;
}

// 已有 count 种、最终最多 maxCount 种时可能取到的最低与最高档
std::pair<int, int> tierRange(const std::array<int, 6>& tiers, int count, int maxCount) {
    int low = std::numeric_limits<int>::max();
    int high = std::numeric_limits<int>::min();
    const int last = std::min(maxCount, int(tiers.size()) - 1);
    for (int tier = std::max(count, 1); tier <= last; ++tier) {
        low = std::min(low, tiers[tier]);
        high = std::max(high, tiers[tier]);
    }
    return {low, high};
}

// 队长排第一位，其余保持搜索顺序；当期上限的判定与物化都按这个次序
std::vector<int> leaderFirst(const std::vector<int>& groups, int leaderPosition) {
    std::vector<int> ordered{groups[leaderPosition]};
    for (int i = 0; i < int(groups.size()); ++i)
        if (i != leaderPosition)
            ordered.push_back(groups[i]);
    return ordered;
}

class FinalChapterBonusSearch {
    const FinalChapterBonusContext& context;
    const DeckRecommendConfig& config;
    RecommendCalcInfo& dfsInfo;
    std::set<int>& targets;
    const std::vector<int>& pool;           // 本层可选的组下标，按加成升序
    std::vector<uint32_t> suffixCharas{};   // pool[i..] 覆盖的角色
    std::vector<uint32_t> suffixAttrs{};    // pool[i..] 覆盖的属性
    std::vector<uint32_t> suffixUnits{};    // pool[i..] 覆盖的组合
    std::vector<int> current{};

    // 卡组已满：尝试每个成员作队长，记录命中的目标
    bool collect(int bonus, uint32_t charas, uint32_t attrs, uint32_t units, bool allLimited) {
        if ((charas & context.requiredCharacters) != context.requiredCharacters)
            return true;

        const int deckBonus = bonus
            + context.diffAttr[std::popcount(attrs)]
            + context.shuffleUnit[std::popcount(units)];
        // (加成, 队长位置)；绝大多数卡组一个目标都不中，不会分配
        std::vector<std::pair<int, int>> hits{};
        for (int position = 0; position < int(current.size()); ++position) {
            const auto& leader = context.groups[current[position]];
            if (context.leaderCharacter && leader.chara != context.leaderCharacter)
                continue;
            int total = deckBonus + leader.leaderBonus;
            if (context.limitedCap && allLimited)
                total -= context.groups[leaderFirst(current, position).back()].limitedBonus;
            if (targets.count(total))
                hits.emplace_back(total, position);
        }

        // 同一加成的多个队长只算一个结果，物化时再按分数挑
        std::sort(hits.begin(), hits.end());
        for (std::size_t i = 0; i < hits.size(); ) {
            const int total = hits[i].first;
            FinalChapterBonusResult found{current, {}};
            for (; i < hits.size() && hits[i].first == total; ++i)
                found.leaders.push_back(hits[i].second);
            auto& totalResults = result[total];
            totalResults.push_back(std::move(found));
            if (int(totalResults.size()) == config.limit)
                targets.erase(total);
        }
        return targets.size() > 0;
    }

    bool dfs(std::size_t start, int bonus, uint32_t charas, uint32_t attrs, uint32_t units, bool allLimited) {
        const int rest = config.member - int(current.size());
        if (rest == 0)
            return collect(bonus, charas, attrs, units, allLimited);

        // 超过时间，退出
        if (dfsInfo.isTimeout())
            return false;

        // 剩下的角色凑不满，或必选角色已经取不到，剪枝
        const uint32_t available = suffixCharas[start] & ~charas;
        const uint32_t missing = context.requiredCharacters & ~charas;
        if (std::popcount(available) < rest || std::popcount(missing) > rest || (missing & ~available))
            return true;

        // 剩下 rest 张能取的最低和最高加成。按加成顺序每个角色只取第一次出现的组，
        // 即各角色最低（最高）的组，取满 rest 个角色就是不同角色约束下的确切最值
        int lowestBonus = 0, highestBonus = 0;
        uint32_t seen = charas;
        for (std::size_t i = start, taken = 0; i < pool.size() && int(taken) < rest; ++i) {
            const auto& group = context.groups[pool[i]];
            const uint32_t charaBit = uint32_t{1} << group.chara;
            if (seen & charaBit) continue;
            seen |= charaBit;
            lowestBonus += group.bonus, ++taken;
        }
        seen = charas;
        for (std::size_t i = pool.size(), taken = 0; i > start && int(taken) < rest; ) {
            const auto& group = context.groups[pool[--i]];
            const uint32_t charaBit = uint32_t{1} << group.chara;
            if (seen & charaBit) continue;
            seen |= charaBit;
            highestBonus += group.bonus, ++taken;
        }
        // 属性、组合数最多再增加 rest 种，且不超过剩余卡牌覆盖的种类；
        // 队长加成取全局最低/最高；当期上限只可能扣掉一次
        const int attrCount = std::popcount(attrs), unitCount = std::popcount(units);
        const auto [attrLow, attrHigh] = tierRange(context.diffAttr, attrCount,
            std::min(attrCount + rest, std::popcount(attrs | suffixAttrs[start])));
        const auto [unitLow, unitHigh] = tierRange(context.shuffleUnit, unitCount,
            std::min(unitCount + rest, std::popcount(units | suffixUnits[start])));
        const int penalty = context.limitedCap && allLimited ? context.maxLimited : 0;
        const int floor = attrLow + unitLow + context.minLeader - penalty;
        if (bonus + lowestBonus + floor > *targets.rbegin()
         || bonus + highestBonus + attrHigh + unitHigh + context.maxLeader < *targets.begin())
            return true;

        // 搜索剩下卡牌，保证组下标递增
        for (std::size_t i = start; i < pool.size(); ++i) {
            const int index = pool[i];
            const auto& group = context.groups[index];
            // 之后的组加成只会更高，剩下 rest 张都按本组加成算也已超过目标
            if (bonus + rest * group.bonus + floor > *targets.rbegin())
                break;
            const uint32_t charaBit = uint32_t{1} << group.chara;
            if (charas & charaBit) continue;   // 跳过重复角色

            current.push_back(index);
            const bool cont = dfs(
                i + 1, bonus + group.bonus, charas | charaBit,
                attrs | (uint32_t{1} << group.attr),
                units | (uint32_t{1} << group.unit),
                allLimited && group.limitedBonus > 0
            );
            current.pop_back();
            if (!cont) return false;
        }
        return true;
    }

public:
    std::map<int, std::vector<FinalChapterBonusResult>> result{};

    FinalChapterBonusSearch(
        const FinalChapterBonusContext& context,
        const DeckRecommendConfig& config,
        RecommendCalcInfo& dfsInfo,
        std::set<int>& targets,
        const std::vector<int>& pool
    ) : context(context), config(config), dfsInfo(dfsInfo), targets(targets), pool(pool) {
        suffixCharas.assign(pool.size() + 1, 0);
        suffixAttrs.assign(pool.size() + 1, 0);
        suffixUnits.assign(pool.size() + 1, 0);
        for (std::size_t i = pool.size(); i > 0; --i) {
            const auto& group = context.groups[pool[i - 1]];
            suffixCharas[i - 1] = suffixCharas[i] | (uint32_t{1} << group.chara);
            suffixAttrs[i - 1] = suffixAttrs[i] | (uint32_t{1} << group.attr);
            suffixUnits[i - 1] = suffixUnits[i] | (uint32_t{1} << group.unit);
        }
    }

    void run() {
        dfs(0, 0, 0, 0, 0, true);
    }
};

}


void BaseDeckRecommend::findFinalChapterTargetBonusCardsDFS(
    int liveType,
    const DeckRecommendConfig &config,
    const std::vector<CardDetail> &cardDetails,
    const std::function<Score(const DeckScoreDetail &)> &scoreFunc,
    RecommendCalcInfo &dfsInfo,
    int limit,
    int member,
    std::optional<int> eventType,
    std::optional<int> eventId,
    const std::vector<CardDetail>& fixedCards
)
{
    if (!isFinalChapterEvent(eventId.value_or(0))) {
        // 该函数只用于WL终章
        throw std::runtime_error("this func is only used for final chapter event");
    }

    std::vector<int> bonusList = config.bonusList;
    for (auto& x : bonusList) x *= 2;
    if (bonusList.empty())
        throw std::runtime_error("Bonus list is empty");
    // 剩余的组卡目标
    std::set<int> targets(bonusList.begin(), bonusList.end());

    // 按终章加成计算用到的全部量归组，map 的字典序即按非队长加成升序
    FinalChapterBonusContext context{};
    std::set<int> fixedCardIds{};
    std::map<int, const CardDetail*> fixedCardByCharacter{};
    for (const auto& card : fixedCards) {
        fixedCardIds.insert(card.cardId);
        fixedCardByCharacter[card.characterId] = &card;
    }
    std::map<std::tuple<int, int, int, int, int, int>, std::vector<const CardDetail*>> groupedCards;
    for (const auto &card : cardDetails) {
        const double eventBonus = card.maxEventBonus.value_or(0.0);
        if (eventBonus <= 0 && !fixedCardIds.count(card.cardId))
            continue;
        const double leaderBonus =
            card.leaderHonorEventBonus.value_or(0.0) + card.leaderLimitEventBonus.value_or(0.0);
        const double limitedBonus = card.limitedEventBonus.value_or(0.0);
        if (!isHalfPercentMultiple(eventBonus) || !isHalfPercentMultiple(leaderBonus) ||
            !isHalfPercentMultiple(limitedBonus))
            continue;
        groupedCards[{
            int(std::round((eventBonus - leaderBonus) * 2)),
            card.characterId,
            card.attr,
            int(std::round(leaderBonus * 2)),
            int(std::round(limitedBonus * 2)),
            getFinalChapter2ShuffleUnit(card),
        }].push_back(&card);
    }
    for (auto& [key, cards] : groupedCards) {
        std::sort(cards.begin(), cards.end(), [](const CardDetail *a, const CardDetail *b) {
            return std::tuple(a->skill.max, a->power.max, a->cardId)
                 > std::tuple(b->skill.max, b->power.max, b->cardId);
        });
        const auto [bonus, chara, attr, leaderBonus, limitedBonus, unit] = key;
        context.groups.push_back({bonus, leaderBonus, limitedBonus, chara, attr, unit, std::move(cards)});
    }

    // 卡组级加成
    for (const auto &bonus : this->dataProvider.masterData->worldBloomDifferentAttributeBonuses)
        if (bonus.attributeCount >= 0 && bonus.attributeCount < 6)
            context.diffAttr[bonus.attributeCount] = std::round(bonus.bonusRate * 2);
    if (eventId.value() == finalChapter2EventId)
        for (int count = 0; count < 6; ++count)
            context.shuffleUnit[count] = std::round(getFinalChapter2ShuffleUnitBonus(count) * 2);
    context.limitedCap = eventId.value() == finalChapterEventId && config.member == 5;
    for (const auto& group : context.groups) {
        context.maxLimited = std::max(context.maxLimited, group.limitedBonus);
        context.minLeader = std::min(context.minLeader, group.leaderBonus);
        context.maxLeader = std::max(context.maxLeader, group.leaderBonus);
    }

    // 固定角色与固定卡牌
    for (const auto characterId : config.fixedCharacters)
        context.requiredCharacters |= uint32_t{1} << characterId;
    if (!config.fixedCharacters.empty())
        context.leaderCharacter = config.fixedCharacters.front();
    for (const auto& card : fixedCards) {
        context.requiredCharacters |= uint32_t{1} << card.characterId;
        const auto group = std::find_if(context.groups.begin(), context.groups.end(), [&](const auto& it) {
            return std::any_of(it.cards.begin(), it.cards.end(), [&](const CardDetail* c) {
                return c->cardId == card.cardId;
            });
        });
        // 固定卡牌的加成不是0.5的整数倍，组不出任何目标
        if (group == context.groups.end())
            return;
        context.fixedGroupByCharacter[card.characterId] = int(group - context.groups.begin());
    }

    // 与普通WL一样分层搜索：先各个组合（含V家）各自组卡，最后一层不限组合
    for (int layer = 0; layer <= 5 && !targets.empty(); ++layer) {
        std::vector<int> pool{};
        for (int i = 0; i < int(context.groups.size()); ++i) {
            const int chara = context.groups[i].chara;
            if (layer != 5 && chara <= 20 && (chara - 1) / 4 != layer)
                continue;
            const auto fixed = context.fixedGroupByCharacter.find(chara);
            if (fixed != context.fixedGroupByCharacter.end() && fixed->second != i)
                continue;
            pool.push_back(i);
        }
        FinalChapterBonusSearch search(context, config, dfsInfo, targets, pool);
        search.run();

        // 取卡：同一套卡的各个可行队长分别求最优，留分数最高的
        for (auto& [bonus, bonusResult] : search.result) {
            for (auto& found : bonusResult) {
                std::optional<RecommendDeck> bestDeck{};
                for (const int leaderPosition : found.leaders) {
                    std::vector<std::vector<const CardDetail*>> cardGroups{};
                    for (const int index : leaderFirst(found.groups, leaderPosition)) {
                        const auto& group = context.groups[index];
                        auto fixed = fixedCardByCharacter.find(group.chara);
                        if (fixed != fixedCardByCharacter.end())
                            cardGroups.push_back({fixed->second});
                        else
                            cardGroups.push_back(group.cards);
                    }
                    auto deckRes = findBestBonusCardCombination(
                        liveType, config, cardGroups, scoreFunc, eventType, eventId
                    );
                    if (!deckRes.has_value())
                        continue;
                    // 需要验证加成正确
                    if (std::abs(deckRes->eventBonus.value_or(0) * 2 - bonus) >= 1e-6) {
                        std::cerr << "Warning: Final chapter bonus mismatch, expected "
                                << bonus / 2.0 << ", got "
                                << deckRes->eventBonus.value_or(0) << std::endl;
                        continue;
                    }
                    if (!bestDeck.has_value() || deckRes.value() > bestDeck.value())
                        bestDeck = std::move(deckRes);
                }
                if (bestDeck.has_value())
                    dfsInfo.update(bestDeck.value(), 1e9);
            }
        }
    }
}
