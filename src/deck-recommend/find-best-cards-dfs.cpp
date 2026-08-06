#include "deck-recommend/base-deck-recommend.h"

#include <numeric>


void BaseDeckRecommend::findBestCardsDFS(
    int liveType,
    const DeckRecommendConfig& cfg,
    const std::vector<const CardDetail*> &cardDetails,
    std::map<int, std::vector<SupportDeckCard>>& supportCards,
    const std::function<Score(const DeckScoreDetail &)> &scoreFunc,
    RecommendCalcInfo& dfsInfo,
    int limit, 
    bool isChallengeLive, 
    int member, 
    int honorBonus, 
    std::optional<int> eventType,
    std::optional<int> eventId,
    bool isNoEvent,
    const std::vector<CardDetail>& fixedCards,
    bool applySameUnitOrAttrPrune
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

    // 无活动多人分数对综合力和技能单调；分别取严格上界后仍落后才可整枝。
    if (cfg.target == RecommendTarget::Score && isNoEvent && !isChallengeLive &&
        Enums::LiveType::isMulti(liveType) &&
        int(dfsInfo.deckQueue.size()) >= limit) {
        const int remaining = member - static_cast<int>(deckCards.size());
        std::vector<double> skillBounds;
        skillBounds.reserve(member);
        for (const auto* card : deckCards) {
            // skill.max 是截断后的整数比较值，+1 才能覆盖小数技能值。
            skillBounds.push_back(static_cast<double>(card->skill.max) + 1.0);
        }

        auto completionPower = [&](std::optional<int> requiredAttr, std::optional<int> requiredUnit) -> std::optional<int> {
            auto cardPower = [&](const CardDetail& card) {
                int power = 0;
                for (auto units = card.unitMask; units; units &= units - 1) {
                    const auto unit = std::countr_zero(units);
                    power = std::max(power, card.power.get(
                        unit, requiredUnit.has_value() ? 5 : 1,
                        requiredAttr.has_value() ? 5 : 1
                    ).total);
                }
                return power;
            };

            int power = honorBonus;
            for (const auto* card : deckCards) {
                if ((requiredAttr.has_value() && card->attr != requiredAttr.value()) ||
                    (requiredUnit.has_value() &&
                        !(card->unitMask & (uint16_t{1} << requiredUnit.value())))) {
                    return std::nullopt;
                }
                power += cardPower(*card);
            }

            std::array<int, 32> maxPowerByCharacter{};
            for (const auto* card : cardDetails) {
                if (!deckCharacters.test(card->characterId) &&
                    (!requiredAttr.has_value() || card->attr == requiredAttr.value()) &&
                    (!requiredUnit.has_value() ||
                        (card->unitMask & (uint16_t{1} << requiredUnit.value())))) {
                    maxPowerByCharacter[card->characterId] = std::max(
                        maxPowerByCharacter[card->characterId], cardPower(*card)
                    );
                }
            }
            std::vector<int> availablePowers;
            for (const auto powerValue : maxPowerByCharacter) {
                if (powerValue > 0)
                    availablePowers.push_back(powerValue);
            }
            if (static_cast<int>(availablePowers.size()) < remaining)
                return std::nullopt;
            std::sort(availablePowers.begin(), availablePowers.end(), std::greater<>());
            return power + std::accumulate(
                availablePowers.begin(), availablePowers.begin() + remaining, 0
            );
        };

        std::vector<std::optional<int>> attrs{std::nullopt};
        if (deckCards.empty()) {
            std::array<bool, 16> seenAttrs{};
            for (const auto* card : cardDetails)
                seenAttrs[card->attr] = true;
            for (int attr = 0; attr < static_cast<int>(seenAttrs.size()); ++attr) {
                if (seenAttrs[attr])
                    attrs.push_back(attr);
            }
        }
        else if (dfsInfo.deckAllSameAttr) {
            attrs.push_back(dfsInfo.deckAttr);
        }

        std::vector<std::optional<int>> units{std::nullopt};
        auto possibleUnits = dfsInfo.deckCommonUnitMask;
        if (deckCards.empty()) {
            for (const auto* card : cardDetails)
                possibleUnits |= card->unitMask;
        }
        for (; possibleUnits; possibleUnits &= possibleUnits - 1)
            units.push_back(std::countr_zero(possibleUnits));

        int maxPower = 0;
        for (const auto attr : attrs) {
            for (const auto unit : units) {
                const auto power = completionPower(attr, unit);
                if (power.has_value())
                    maxPower = std::max(maxPower, power.value());
            }
        }
        if (maxPower == 0)
            return;

        std::array<double, 32> maxSkillByCharacter{};
        for (const auto* card : cardDetails) {
            if (!deckCharacters.test(card->characterId)) {
                maxSkillByCharacter[card->characterId] = std::max(
                    maxSkillByCharacter[card->characterId],
                    static_cast<double>(card->skill.max) + 1.0
                );
            }
        }
        std::vector<double> availableSkillBounds;
        for (const auto maxSkill : maxSkillByCharacter) {
            if (maxSkill > 0.0)
                availableSkillBounds.push_back(maxSkill);
        }
        if (static_cast<int>(availableSkillBounds.size()) < remaining) {
            return;
        }
        std::sort(availableSkillBounds.begin(), availableSkillBounds.end(), std::greater<>());
        skillBounds.insert(
            skillBounds.end(), availableSkillBounds.begin(),
            availableSkillBounds.begin() + remaining
        );
        std::sort(skillBounds.begin(), skillBounds.end(), std::greater<>());

        DeckScoreDetail upperBound{};
        upperBound.power.total = maxPower;
        upperBound.eventBonus = 0.0;
        upperBound.cardCount = member;
        std::copy(skillBounds.begin(), skillBounds.end(), upperBound.skillScoreUps.begin());
        upperBound.multiLiveScoreUp = skillBounds.front();
        upperBound.multiLiveScoreUp += 0.2 * std::accumulate(
            skillBounds.begin() + 1, skillBounds.end(), 0.0
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
    std::vector<const CardDetail*> compatibleCards;
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
        }

        findBestCardsDFS(
            liveType, cfg, *nextCards, supportCards, scoreFunc, dfsInfo,
            limit, isChallengeLive, member, honorBonus, eventType, eventId, isNoEvent, fixedCards,
            applySameUnitOrAttrPrune
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
