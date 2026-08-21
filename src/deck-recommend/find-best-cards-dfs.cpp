#include "deck-recommend/base-deck-recommend.h"

#include <bit>
#include <numeric>


namespace {

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
bool cannotBeatFifthCardScoreBound(
    const CardDetail* card,
    int honorBonus,
    const std::function<Score(const DeckScoreDetail&)>& scoreFunc,
    const RecommendCalcInfo& dfsInfo
) {
    const auto& deckCards = dfsInfo.deckCards;
    const bool allSameAttr = dfsInfo.deckAllSameAttr && card->attr == dfsInfo.deckAttr;
    const auto commonUnitMask = dfsInfo.deckCommonUnitMask & card->unitMask;
    int maxPower = honorBonus;
    if (commonUnitMask == 0) {
        maxPower += dfsInfo.deckMixedUnitPowerTotals[allSameAttr];
        maxPower += std::max(
            card->powerTotals[0][allSameAttr],
            card->powerTotals[1][allSameAttr]
        );
    }
    else {
        const auto addCardPower = [&](const CardDetail* deckCard) {
            int cardPower = 0;
            int unitIndex = 0;
            for (auto units = deckCard->unitMask;
                 units;
                 units &= units - 1, ++unitIndex) {
                const int unit = std::countr_zero(units);
                if (commonUnitMask & (uint16_t{1} << unit)) {
                    cardPower = std::max(
                        cardPower,
                        deckCard->powerTotals[unitIndex][2 + allSameAttr]
                    );
                }
            }
            maxPower += cardPower;
        };
        for (const auto* deckCard : deckCards)
            addCardPower(deckCard);
        addCardPower(card);
    }

    std::array<double, 5> skillBounds{};
    int skillIndex = 0;
    double bonusBound = dfsInfo.scoreBound.diffAttrBonus;
    for (const auto* deckCard : deckCards) {
        skillBounds[skillIndex++] = static_cast<double>(deckCard->skill.max) + 1.0;
        bonusBound += deckCard->maxEventBonus.value_or(0.0);
    }
    skillBounds[skillIndex] = static_cast<double>(card->skill.max) + 1.0;
    bonusBound += card->maxEventBonus.value_or(0.0);
    std::sort(skillBounds.begin(), skillBounds.end(), std::greater<>());

    DeckScoreDetail upperBound{};
    upperBound.power.total = maxPower;
    upperBound.eventBonus = bonusBound;
    upperBound.supportDeckBonus = dfsInfo.scoreBound.supportDeckBonus;
    upperBound.skillScoreUps = skillBounds;
    upperBound.cardCount = 5;
    upperBound.multiLiveScoreUp = skillBounds.front() + 0.2 * std::accumulate(
        skillBounds.begin() + 1, skillBounds.end(), 0.0
    );
    const auto score = scoreFunc(upperBound);
    const double targetValue = score.score + double(score.liveScore) / SCORE_MAX;
    return targetValue < dfsInfo.deckQueue.top().targetValue;
}

}  // namespace


void DfsScoreBoundIndex::build(
    const std::vector<const CardDetail*>& cards,
    std::vector<int>& powers
) {
    powers.assign(attrCount * UNIT_MAX * characterCount, 0);
    skills.fill(0.0);
    bonuses.fill(0.0);
    attrs = 0;
    units = 0;

    const auto updatePower = [&](int attr, int unit, int character, int value) {
        auto& current = powers[(attr * UNIT_MAX + unit) * characterCount + character];
        current = std::max(current, value);
    };

    for (const auto* card : cards) {
        const int character = card->characterId;
        attrs |= uint16_t{1} << card->attr;
        units |= card->unitMask;
        skills[character] = std::max(
            skills[character], static_cast<double>(card->skill.max) + 1.0
        );
        bonuses[character] = std::max(
            bonuses[character], card->maxEventBonus.value_or(0.0)
        );

        updatePower(0, 0, character, std::max(
            card->powerTotals[0][0], card->powerTotals[1][0]
        ));
        updatePower(card->attr, 0, character, std::max(
            card->powerTotals[0][1], card->powerTotals[1][1]
        ));
        for (auto cardUnits = card->unitMask; cardUnits; cardUnits &= cardUnits - 1) {
            const int unit = std::countr_zero(cardUnits);
            updatePower(0, unit, character, std::max(
                card->powerTotals[0][2], card->powerTotals[1][2]
            ));
            updatePower(card->attr, unit, character, std::max(
                card->powerTotals[0][3], card->powerTotals[1][3]
            ));
        }
    }
}


const int* DfsScoreBoundIndex::powerRow(
    const std::vector<int>& powers,
    int attr,
    int unit
) const {
    return &powers[(attr * UNIT_MAX + unit) * characterCount];
}


void BaseDeckRecommend::findBestCardsDFS(
    int liveType,
    const DeckRecommendConfig& cfg,
    const std::vector<const CardDetail*> &cardDetails,
    SupportDeckMap& supportCards,
    const std::function<Score(const DeckScoreDetail &)> &scoreFunc,
    RecommendCalcInfo& dfsInfo,
    int limit, 
    bool isChallengeLive, 
    int member, 
    int honorBonus, 
    std::optional<int> eventType,
    std::optional<int> eventId,
    const std::vector<CardDetail>& fixedCards,
    bool applySameUnitOrAttrPrune,
    bool useCompatibleScoreBoundIndex
)
{
    // 超时
    if (dfsInfo.isTimeout()) {
        return;
    }

    auto& deckCards = dfsInfo.deckCards;
    auto& deckCharacters = dfsInfo.deckCharacters;

    // 防止挑战Live卡的数量小于允许上场的数量导致无法组队
    if (isChallengeLive) {
        member = std::min(member, int(cardDetails.size()));
    }
    // 已经是完整卡组，计算当前卡组的值
    if (int(deckCards.size()) == member) {
        if (cfg.target == RecommendTarget::Power && int(dfsInfo.deckQueue.size()) >= limit) {
            const bool useMixedUnitPower = member != 5 || dfsInfo.deckCommonUnitMask == 0;
            const auto power = useMixedUnitPower
                ? honorBonus + dfsInfo.deckMixedUnitPowerTotals[member == 5 && dfsInfo.deckAllSameAttr]
                : this->deckCalculator.getDeckTotalPowerByCards(deckCards, honorBonus);
            if (power < dfsInfo.deckQueue.top().power.total) {
                return;
            }
        }
        auto ret = getBestPermutation(
            this->deckCalculator, deckCards, supportCards, scoreFunc, 
            honorBonus, eventType, eventId, liveType, cfg
        );
        if (ret.bestCandidate.has_value() && dfsInfo.wouldUpdate(ret.bestCandidate.value(), limit)) {
            dfsInfo.update(materializeCandidate(
                this->deckCalculator, deckCards, supportCards,
                honorBonus, eventType, eventId, cfg, ret.bestCandidate.value()
            ), limit);
        }
        return;
    }

    if (cfg.target == RecommendTarget::Power && int(dfsInfo.deckQueue.size()) >= limit) {
        int remaining = member - static_cast<int>(deckCards.size());
        int maxPower = honorBonus;
        for (const auto* card : deckCards)
            maxPower += card->power.max;

        auto availableCharacters = deckCharacters;
        for (const auto* card : cardDetails) {
            if (remaining == 0)
                break;
            if (isChallengeLive) {
                bool selected = std::any_of(deckCards.begin(), deckCards.end(), [&](const auto* deckCard) {
                    return deckCard->cardId == card->cardId;
                });
                if (selected)
                    continue;
            }
            else if (availableCharacters.test(card->characterId)) {
                continue;
            }

            maxPower += card->power.max;
            availableCharacters.set(card->characterId);
            --remaining;
        }
        if (remaining > 0 || maxPower < dfsInfo.deckQueue.top().power.total) {
            return;
        }
    }

    // 分数对综合力、技能和活动加成单调；分别取严格上界后仍落后才可整枝。
    // 适用条件与与节点无关的加成上界由调用方一次算好，见 recommendHighScoreDeck。
    if (dfsInfo.scoreBound.enabled && int(dfsInfo.deckQueue.size()) >= limit) {
        const int remaining = member - static_cast<int>(deckCards.size());

        // 最后一张卡时，按角色聚合和 partial_sort 都退化为取一个最大值。
        const bool useSingleCardBound =
            remaining == 1 && !deckCards.empty() && dfsInfo.scoreBound.hasEventBonus;
        int maxPower = 0;
        std::array<double, 5> skillBounds{};
        int skillBoundCount = 0;
        double bonusBound = 0.0;
        const auto& boundIndex = useCompatibleScoreBoundIndex
            ? dfsInfo.compatibleScoreBoundIndex
            : dfsInfo.scoreBoundIndex;
        const auto& boundPowers = useCompatibleScoreBoundIndex
            ? dfsInfo.compatibleScoreBoundPowers
            : dfsInfo.scoreBoundPowerScratch;

        if (useSingleCardBound) [[unlikely]] {
            double availableSkillBound = 0.0;
            double availableBonusBound = 0.0;
            for (int character = 0; character < DfsScoreBoundIndex::characterCount; ++character) {
                if (deckCharacters.test(character))
                    continue;
                availableSkillBound = std::max(
                    availableSkillBound, boundIndex.skills[character]
                );
                availableBonusBound = std::max(
                    availableBonusBound, boundIndex.bonuses[character]
                );
            }
            if (availableSkillBound == 0.0)
                return;

            std::array<int, 2> attrs{};
            int attrCount = 1;
            if (dfsInfo.deckAllSameAttr)
                attrs[attrCount++] = dfsInfo.deckAttr;

            std::array<int, 13> units{};
            int unitCount = 1;
            for (auto unitMask = dfsInfo.deckCommonUnitMask; unitMask; unitMask &= unitMask - 1)
                units[unitCount++] = std::countr_zero(unitMask);

            for (int a = 0; a < attrCount; ++a) {
                for (int u = 0; u < unitCount; ++u) {
                    const int state = (u > 0 ? 2 : 0) + (a > 0 ? 1 : 0);
                    int power = honorBonus;
                    for (const auto* card : deckCards)
                        power += std::max(card->powerTotals[0][state], card->powerTotals[1][state]);

                    const int* row = boundIndex.powerRow(
                        boundPowers, attrs[a], units[u]
                    );
                    int candidatePower = 0;
                    for (int character = 0; character < DfsScoreBoundIndex::characterCount; ++character) {
                        if (deckCharacters.test(character))
                            continue;
                        candidatePower = std::max(candidatePower, row[character]);
                    }
                    if (candidatePower > 0)
                        maxPower = std::max(maxPower, power + candidatePower);
                }
            }
            if (maxPower == 0)
                return;

            for (const auto* card : deckCards)
                skillBounds[skillBoundCount++] = static_cast<double>(card->skill.max) + 1.0;
            skillBounds[skillBoundCount++] = availableSkillBound;
            std::sort(skillBounds.begin(), skillBounds.begin() + skillBoundCount, std::greater<>());

            bonusBound = dfsInfo.scoreBound.diffAttrBonus;
            for (const auto* card : deckCards)
                bonusBound += card->maxEventBonus.value_or(0.0);
            bonusBound += availableBonusBound;

        }
        else {
            // 候选完成条件；attrs[0]/units[0] 代表无属性/组合要求
            std::array<int, 17> attrs{};
            int attrCount = 1;
            if (deckCards.empty()) {
                for (auto attrMask = boundIndex.attrs; attrMask; attrMask &= attrMask - 1)
                    attrs[attrCount++] = std::countr_zero(attrMask);
            }
            else if (dfsInfo.deckAllSameAttr) {
                attrs[attrCount++] = dfsInfo.deckAttr;
            }

            std::array<int, 13> units{};
            int unitCount = 1;
            auto possibleUnits = deckCards.empty()
                ? boundIndex.units
                : dfsInfo.deckCommonUnitMask;
            for (; possibleUnits; possibleUnits &= possibleUnits - 1)
                units[unitCount++] = std::countr_zero(possibleUnits);

            std::array<int, 32> availablePowers;
            for (int a = 0; a < attrCount; ++a) {
                for (int u = 0; u < unitCount; ++u) {
                    const int state = (u > 0 ? 2 : 0) + (a > 0 ? 1 : 0);
                    int power = honorBonus;
                    bool deckCompatible = true;
                    for (const auto* card : deckCards) {
                        if ((a > 0 && card->attr != attrs[a]) ||
                            (u > 0 && !(card->unitMask & (uint16_t{1} << units[u])))) {
                            deckCompatible = false;
                            break;
                        }
                        power += std::max(card->powerTotals[0][state], card->powerTotals[1][state]);
                    }
                    if (!deckCompatible)
                        continue;

                    const int* row = boundIndex.powerRow(
                        boundPowers, attrs[a], units[u]
                    );
                    int availableCount = 0;
                    for (int character = 0; character < DfsScoreBoundIndex::characterCount; ++character) {
                        if (!deckCharacters.test(character) && row[character] > 0)
                            availablePowers[availableCount++] = row[character];
                    }
                    if (availableCount < remaining)
                        continue;
                    std::partial_sort(
                        availablePowers.begin(), availablePowers.begin() + remaining,
                        availablePowers.begin() + availableCount, std::greater<>()
                    );
                    maxPower = std::max(maxPower, std::accumulate(
                        availablePowers.begin(), availablePowers.begin() + remaining, power
                    ));
                }
            }
            if (maxPower == 0)
                return;

            std::array<double, 32> availableSkillBounds;
            int availableSkillCount = 0;
            for (int character = 0; character < DfsScoreBoundIndex::characterCount; ++character) {
                if (!deckCharacters.test(character) && boundIndex.skills[character] > 0.0)
                    availableSkillBounds[availableSkillCount++] = boundIndex.skills[character];
            }
            if (availableSkillCount < remaining) {
                return;
            }
            std::partial_sort(
                availableSkillBounds.begin(), availableSkillBounds.begin() + remaining,
                availableSkillBounds.begin() + availableSkillCount, std::greater<>()
            );

            for (const auto* card : deckCards)
                skillBounds[skillBoundCount++] = static_cast<double>(card->skill.max) + 1.0;
            for (int i = 0; i < remaining; ++i)
                skillBounds[skillBoundCount++] = availableSkillBounds[i];
            std::sort(skillBounds.begin(), skillBounds.begin() + skillBoundCount, std::greater<>());

            // 活动加成上界：队内卡按实际最大加成累加，空位取剩余角色中加成最大的几个。
            // 终章的扣减与当期上限、以及缺失加成时整队归零，都只会让实际值更小。
            // maxEventBonus 与综合力/技能不在同一批缓存行，无活动时整块跳过。
            if (dfsInfo.scoreBound.hasEventBonus) {
                bonusBound = dfsInfo.scoreBound.diffAttrBonus;
                for (const auto* card : deckCards)
                    bonusBound += card->maxEventBonus.value_or(0.0);
                std::array<double, 32> availableBonuses;
                int availableBonusCount = 0;
                for (int character = 0; character < DfsScoreBoundIndex::characterCount; ++character) {
                    if (!deckCharacters.test(character) && boundIndex.bonuses[character] > 0.0)
                        availableBonuses[availableBonusCount++] = boundIndex.bonuses[character];
                }
                // 加成为 0 的角色同样可选，取不满 remaining 个时余下按 0 计
                const int usedBonusCount = std::min(remaining, availableBonusCount);
                std::partial_sort(
                    availableBonuses.begin(), availableBonuses.begin() + usedBonusCount,
                    availableBonuses.begin() + availableBonusCount, std::greater<>()
                );
                bonusBound += std::accumulate(
                    availableBonuses.begin(), availableBonuses.begin() + usedBonusCount, 0.0
                );
            }

        }

        DeckScoreDetail upperBound{};
        upperBound.power.total = maxPower;
        upperBound.eventBonus = bonusBound;
        upperBound.supportDeckBonus = dfsInfo.scoreBound.supportDeckBonus;
        // 真实卡组恒以 cardCount=5 评分，member<5 时 order 末尾回落到队长位，
        // 即空位复用队长技能；上界必须用同一形状，否则会走进短卡组分支读越界。
        upperBound.cardCount = 5;
        std::copy(skillBounds.begin(), skillBounds.begin() + skillBoundCount, upperBound.skillScoreUps.begin());
        std::fill(
            upperBound.skillScoreUps.begin() + skillBoundCount,
            upperBound.skillScoreUps.end(),
            skillBounds.front()
        );
        upperBound.multiLiveScoreUp = skillBounds.front();
        upperBound.multiLiveScoreUp += 0.2 * std::accumulate(
            skillBounds.begin() + 1, skillBounds.begin() + skillBoundCount, 0.0
        );
        const auto score = scoreFunc(upperBound);
        const double targetValue = score.score + double(score.liveScore) / SCORE_MAX;
        if (targetValue < dfsInfo.deckQueue.top().targetValue) {
            return;
        }
    }

    // 非完整卡组，继续遍历所有情况
    const CardDetail* preCard = nullptr;
    auto cIndex = fixedCards.size() + cfg.fixedCharacters.size();
    // 兼容候选列表只会在深度 cIndex+1 构建一次，供整棵子树只读使用，可安全复用缓冲
    std::vector<const CardDetail*>& compatibleCards = dfsInfo.compatibleScratch;
    for (const auto* card : cardDetails) {
        if (isChallengeLive) {
            bool selected = std::any_of(deckCards.begin(), deckCards.end(), [&](const auto* deckCard) {
                return deckCard->cardId == card->cardId;
            });
            if (selected)
                continue;
        } else if (deckCharacters.test(card->characterId)) {
            continue;
        }
        // 强制角色限制（不需要考虑固定卡牌，两个参数不允许同时存在）
        if (cfg.fixedCharacters.size() > deckCards.size() && cfg.fixedCharacters[deckCards.size()] != card->characterId) {
            continue;
        }

        if (deckCards.size() >= cIndex + 2) {
            auto& last = *deckCards.back();
            bool lessThan = false;
            bool greaterThan = false;
            if (cfg.target == RecommendTarget::Score) {
                lessThan = this->cardCalculator.isCertainlyLessThan(last, *card);
                greaterThan = this->cardCalculator.isCertainlyLessThan(*card, last);
            } else if(cfg.target == RecommendTarget::Power) {
                lessThan = last.power.isCertainlyLessThan(card->power);
                greaterThan = card->power.isCertainlyLessThan(last.power);
            } else if (cfg.target == RecommendTarget::Skill) {
                lessThan = last.skill.isCertainlyLessThan(card->skill);
                greaterThan = card->skill.isCertainlyLessThan(last.skill);
            }
            // 要求生成的卡组后面4个位置按强弱排序、同强度按卡牌ID排序
            // 如果上一张卡肯定小，那就不符合顺序；
            if (lessThan) continue;
            // 在旗鼓相当的前提下（因为两两组合有四种情况，再排除掉这张卡肯定小的情况，就是旗鼓相当），要ID小
            if (!greaterThan && card->cardId > last.cardId) continue;
        }
        
        if (preCard) {
            auto& pre = *preCard;
            bool lessThan = false;

            if (cfg.target == RecommendTarget::Score) {
                lessThan = this->cardCalculator.isCertainlyLessThan(*card, pre);
            } else if (cfg.target == RecommendTarget::Power) {
                lessThan = card->power.isCertainlyLessThan(pre.power);
            } else if (cfg.target == RecommendTarget::Skill) {
                lessThan = card->skill.isCertainlyLessThan(pre.skill);
            } else if (cfg.target == RecommendTarget::Mysekai) {
                lessThan = this->cardCalculator.isCertainlyLessThan(*card, pre, true, false, true);
            }

            if (cfg.target == RecommendTarget::Score) {
                // 如果肯定比上一次选定的卡牌要弱，那么舍去，让这张卡去后面再选
                // 该优化较为激进，未考虑卡的协同效应，在计算分数最优的情况下才使用
                if (lessThan) continue;
            } else {
                // 计算实效或综合力最优时性能够用，使用较温和的优化
                if (lessThan && deckCards.size() != member - 1) continue;
            }
        }
        preCard = card;

        // 第五张卡已经固定后，用逐分量严格上界提前拒绝必败候选。
        // 这里只调用原评分函数作 oracle，最终入队仍走完整卡组状态枚举。
        if (dfsInfo.scoreBound.enabled &&
            Enums::LiveType::isMulti(liveType) &&
            member == 5 &&
            deckCards.size() == 4 &&
            int(dfsInfo.deckQueue.size()) >= limit &&
            cannotBeatFifthCardScoreBound(card, honorBonus, scoreFunc, dfsInfo))
            continue;

        // 递归，寻找所有情况
        const auto previousCommonUnitMask = dfsInfo.deckCommonUnitMask;
        const auto previousAttr = dfsInfo.deckAttr;
        const auto previousAllSameAttr = dfsInfo.deckAllSameAttr;
        if (deckCards.empty())
            dfsInfo.deckAttr = card->attr;
        else
            dfsInfo.deckAllSameAttr &= card->attr == dfsInfo.deckAttr;
        dfsInfo.deckCommonUnitMask &= card->unitMask;
        for (int sameAttr = 0; sameAttr < 2; ++sameAttr) {
            dfsInfo.deckMixedUnitPowerTotals[sameAttr] += std::max(
                card->powerTotals[0][sameAttr], card->powerTotals[1][sameAttr]
            );
        }
        deckCards.push_back(card);
        deckCharacters.flip(card->characterId);

        const auto* nextCards = &cardDetails;
        bool nextUsesCompatibleScoreBoundIndex = useCompatibleScoreBoundIndex;
        if (applySameUnitOrAttrPrune &&
            deckCards.size() == cIndex + 1 && deckCards.size() < static_cast<std::size_t>(member)) {
            compatibleCards.clear();
            compatibleCards.reserve(cardDetails.size());
            for (const auto* candidate : cardDetails) {
                if (!card->skill.isCertainlyLessThan(candidate->skill) &&
                    (candidate->attr == card->attr || (candidate->unitMask & card->unitMask))) {
                    compatibleCards.push_back(candidate);
                }
            }
            nextCards = &compatibleCards;
            if (dfsInfo.scoreBound.enabled) {
                dfsInfo.compatibleScoreBoundIndex.build(
                    compatibleCards, dfsInfo.compatibleScoreBoundPowers
                );
                nextUsesCompatibleScoreBoundIndex = true;
            }
        }

        findBestCardsDFS(
            liveType, cfg, *nextCards, supportCards, scoreFunc, dfsInfo,
            limit, isChallengeLive, member, honorBonus, eventType, eventId, fixedCards,
            applySameUnitOrAttrPrune, nextUsesCompatibleScoreBoundIndex
        );

        deckCards.pop_back();
        deckCharacters.flip(card->characterId);
        for (int sameAttr = 0; sameAttr < 2; ++sameAttr) {
            dfsInfo.deckMixedUnitPowerTotals[sameAttr] -= std::max(
                card->powerTotals[0][sameAttr], card->powerTotals[1][sameAttr]
            );
        }
        dfsInfo.deckCommonUnitMask = previousCommonUnitMask;
        dfsInfo.deckAttr = previousAttr;
        dfsInfo.deckAllSameAttr = previousAllSameAttr;
    }
}
