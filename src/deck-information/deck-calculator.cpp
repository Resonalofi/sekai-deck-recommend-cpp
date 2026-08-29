#include "deck-information/deck-calculator.h"
#include "common/timer.h"
#include "deck-calculator.h"
#include <bit>


namespace {

struct PreparedScoreSkill {
    const DeckCardSkillDetail* detail;
    double scoreUp;
};

template<typename Consumer>
void forEachSkillScoreState(
    const std::vector<const CardDetail*>& cardDetails,
    const std::array<int, 16>& unitCounts,
    SkillReferenceChooseStrategy skillReferenceChooseStrategy,
    bool keepAfterTrainingState,
    bool bestSkillAsLeader,
    Consumer&& consume
) {
    const int cardCount = static_cast<int>(cardDetails.size());
    int unitCount = 0;
    for (int count : unitCounts)
        unitCount += count != 0;

    DeckCardSkillDetail emptySkill{};
    std::array<std::array<PreparedScoreSkill, 2>, 5> preparedSkills{};
    int doubleSkillMask = 0;
    int needEnumerateStatusMask = 0;
    for (int i = 0; i < cardCount; ++i) {
        const auto& card = *cardDetails[i];
        auto& before = preparedSkills[i][0];
        auto& after = preparedSkills[i][1];
        before.detail = &emptySkill;
        after.detail = &emptySkill;

        for (auto units = card.unitMask; units; units &= units - 1) {
            const int unit = std::countr_zero(units);
            const auto& current = card.skill.get(unit, unitCounts[unit], 1);
            if (current.scoreUp > after.scoreUp)
                after = {&current, current.scoreUp};
        }

        bool needEnumerate = false;
        const auto& referenceSkill = card.skill.get(Enums::Unit::ref, 1, 1);
        const double referenceScoreUp = referenceSkill.scoreUp + referenceSkill.scoreUpReferenceMax;
        if (referenceSkill.skillId != after.detail->skillId && referenceScoreUp > before.scoreUp) {
            before = {&referenceSkill, referenceScoreUp};
            needEnumerate = true;
        }

        const auto& differentUnitSkill = card.skill.get(Enums::Unit::diff, unitCount - 1, 1);
        if (differentUnitSkill.skillId != after.detail->skillId &&
            differentUnitSkill.scoreUp > before.scoreUp) {
            before = {&differentUnitSkill, differentUnitSkill.scoreUp};
            needEnumerate = false;
        }

        if (before.detail->skillId)
            doubleSkillMask |= 1 << i;

        if (keepAfterTrainingState) {
            if (card.defaultImage != Enums::DefaultImage::special_training &&
                after.detail->isAfterTraining) {
                after = before;
            }
        }
        else if (needEnumerate) {
            needEnumerateStatusMask |= 1 << i;
        }
        else if (before.scoreUp > after.scoreUp) {
            after = before;
        }
    }

    std::array<const DeckCardSkillDetail*, 5> selectedSkills{};
    std::array<double, 5> scoreValues{};
    std::array<double, 5> referenceInputs{};
    std::array<int, 5> order{};
    std::array<double, 4> memberSkillMaxs{};
    std::array<std::pair<int, int>, 32> scoreUps;
    int scoreUpCount = 0;
    for (int mask = needEnumerateStatusMask; mask >= 0;
         mask = mask ? (mask - 1) & needEnumerateStatusMask : -1) {
        for (int i = 0; i < cardCount; ++i) {
            const auto& selected = preparedSkills[i][(mask & (1 << i)) ? 0 : 1];
            selectedSkills[i] = selected.detail;
            scoreValues[i] = selected.scoreUp;
            referenceInputs[i] = selected.scoreUp;
        }

        for (int i = 0; i < cardCount; ++i) {
            const auto& skill = *selectedSkills[i];
            if (!skill.hasScoreUpReference)
                continue;

            scoreValues[i] -= skill.scoreUpReferenceMax;
            int memberSkillCount = 0;
            for (int j = 0; j < cardCount; ++j) {
                if (i == j)
                    continue;
                double value = referenceInputs[j];
                value = std::min(
                    std::floor(value * skill.scoreUpReferenceRate / 100.0),
                    skill.scoreUpReferenceMax
                );
                memberSkillMaxs[memberSkillCount++] = value;
            }

            double chosenSkillMax = 0.0;
            if (skillReferenceChooseStrategy == SkillReferenceChooseStrategy::Max) {
                chosenSkillMax = *std::max_element(
                    memberSkillMaxs.begin(), memberSkillMaxs.begin() + memberSkillCount
                );
            }
            else if (skillReferenceChooseStrategy == SkillReferenceChooseStrategy::Min) {
                chosenSkillMax = *std::min_element(
                    memberSkillMaxs.begin(), memberSkillMaxs.begin() + memberSkillCount
                );
            }
            else if (skillReferenceChooseStrategy == SkillReferenceChooseStrategy::Average) {
                chosenSkillMax = std::accumulate(
                    memberSkillMaxs.begin(), memberSkillMaxs.begin() + memberSkillCount, 0.0
                ) / memberSkillCount;
            }
            scoreValues[i] += chosenSkillMax;
        }

        std::iota(order.begin(), order.begin() + cardCount, 0);
        if (bestSkillAsLeader) {
            const int bestIndex = std::max_element(
                order.begin(), order.begin() + cardCount,
                [&scoreValues, &cardDetails](int x, int y) {
                    return std::tuple(scoreValues[x], -cardDetails[x]->cardId) <
                        std::tuple(scoreValues[y], -cardDetails[y]->cardId);
                }
            ) - order.begin();
            if (bestIndex != 0)
                std::swap(order[0], order[bestIndex]);
        }
        else {
            std::sort(
                order.begin() + 1, order.begin() + cardCount,
                [&cardDetails](int x, int y) {
                    return cardDetails[x]->cardId < cardDetails[y]->cardId;
                }
            );
        }

        double leaderScoreUp = 0.0;
        double otherScoreUpSum = 0.0;
        for (int index : order) {
            if (index == 0)
                leaderScoreUp = scoreValues[index];
            else
                otherScoreUpSum += scoreValues[index];
        }
        bool skip = false;
        for (int i = 0; i < scoreUpCount; ++i) {
            const auto& scoreUp = scoreUps[i];
            if (scoreUp.first >= leaderScoreUp && scoreUp.second >= otherScoreUpSum) {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;
        scoreUps[scoreUpCount++] = {leaderScoreUp, otherScoreUpSum};

        double multiLiveScoreUp = scoreValues[order[0]];
        for (int i = 1; i < cardCount; ++i)
            multiLiveScoreUp += scoreValues[order[i]] * 0.2;

        consume(
            selectedSkills,
            scoreValues,
            referenceInputs,
            order,
            doubleSkillMask,
            mask,
            multiLiveScoreUp
        );
    }
}

}


DeckBonusInfo DeckCalculator::getDeckBonus(
    const std::vector<const CardDetail *> &deckCards, 
    std::optional<int> eventType,
    std::optional<int> eventId
) 
{
    DeckBonusInfo ret{};

    // 如果没有预处理好活动加成，则返回空
    for (const auto &card : deckCards) 
        if (!card->maxEventBonus.has_value()) {
            return ret;
        }

    // 正常加成
    for (size_t i = 0; i < deckCards.size(); ++i)
        ret.cardBonus[i] = deckCards[i]->maxEventBonus.value();

    // 终章机制
    if (eventId.has_value() && isFinalChapterEvent(eventId.value())) {
        // 不是队长的角色扣掉1k牌加成和队长当期加成
        for (int i = 1; i < (int)deckCards.size(); i++) {
            ret.cardBonus[i] -= deckCards[i]->leaderHonorEventBonus.value_or(0.0);
            ret.cardBonus[i] -= deckCards[i]->leaderLimitEventBonus.value_or(0.0);
        }
        // 终章1最多生效4个当期；终章2上限放宽到5个，即全队生效、无需扣减
        if (eventId.value() == finalChapterEventId) {
            int limitedEventBonusNum = 0;
            for (int i = 0; i < (int)deckCards.size(); i++) {
                if (deckCards[i]->limitedEventBonus.value_or(0.) > 0) {
                    if(++limitedEventBonusNum == 5) {
                        // 去掉最后一个当期加成
                        ret.cardBonus[i] -= deckCards[i]->limitedEventBonus.value();
                        break;
                    }
                }
            }
        }
    }

    // WL异色加成
    if (eventType == Enums::EventType::world_bloom) 
    {
        auto& worldBloomDifferentAttributeBonuses = this->dataProvider.masterData->worldBloomDifferentAttributeBonuses;
        bool attr_vis[10] = {};
        for (const auto &card : deckCards) 
            attr_vis[card->attr] = true;
        int attr_count = 0;
        for (int i = 0; i < 10; ++i) 
            attr_count += attr_vis[i];
        auto it = findOrThrow(worldBloomDifferentAttributeBonuses, [&](const auto &it) {
            return it.attributeCount == attr_count;
        }, [&]() { return "World bloom different attribute bonus not found for attributeCount=" + std::to_string(attr_count); });
        ret.diffAttrBonus = it.bonusRate;
    }

    // 终章2按编成内组合数发挥shuffle unit bonus；V家一律按“バーチャル・シンガー”计，不按支援组合计
    if (eventId.value_or(0) == finalChapter2EventId) {
        uint16_t units = 0;
        for (const auto &card : deckCards) {
            const int unit = card->characterId >= 21
                ? Enums::Unit::piapro
                : std::countr_zero(card->unitMask);
            units |= uint16_t{1} << unit;
        }
        const int unitCount = std::popcount(units);
        ret.shuffleUnitBonus = unitCount >= 5 ? 50.0
                             : unitCount == 4 ? 30.0
                             : unitCount == 3 ? 10.0
                             : 0.0;
    }

    ret.totalBonus = ret.diffAttrBonus + ret.shuffleUnitBonus + std::accumulate(
        ret.cardBonus.begin(),
        ret.cardBonus.begin() + deckCards.size(),
        0.0
    );
    return ret;
}

SupportDeckBonus DeckCalculator::getSupportDeckBonus(
    const std::vector<const CardDetail*> &deckCards, 
    const SupportDeckCards& supportCards,
    int supportDeckCount
)
{
    std::uint32_t excludedRanks = 0;
    for (const auto* card : deckCards) {
        if (card->cardId >= 0 && static_cast<std::size_t>(card->cardId) < supportCards.topRankByCardId.size()) {
            const auto rank = supportCards.topRankByCardId[card->cardId];
            if (rank < 32)
                excludedRanks |= std::uint32_t{1} << rank;
        }
    }

    double bonus = 0;
    int count = 0;
    
    std::vector<CardDetail> cards{};
    for (std::size_t i = 0; i < supportCards.cards.size(); ++i) {
        // 支援卡组的卡不能和主队伍重复，需要排除掉
        if (i < 32 && (excludedRanks & (std::uint32_t{1} << i)))
            continue;
        const auto& card = supportCards.cards[i];
        bonus += card.bonus;
        count++;
        if (count >= supportDeckCount) return { bonus, cards };
    }
    // 就算组不出完整的支援卡组也得返回
    return { bonus, cards };
}

int DeckCalculator::getHonorBonusPower()
{
    auto& userHonors = this->dataProvider.userData->userHonors;
    int bonus = 0;
    for (const auto &userHonor : userHonors) {
        auto* honor = this->dataProvider.masterData->findHonorById(userHonor.honorId);
        if (honor == nullptr)
            throw ElementNoFoundError("Honor not found for honorId=" + std::to_string(userHonor.honorId));
        auto& levelIt = findOrThrow(honor->levels, [&](const auto &it) {
            return it.level == userHonor.level; 
        }, [&]() { return "Honor level not found for honorId=" + std::to_string(userHonor.honorId) + " level=" + std::to_string(userHonor.level); });
        bonus += levelIt.bonus;
    }
    return bonus;
}

DeckPowerCalculation DeckCalculator::getDeckPowerByCards(
    const std::vector<const CardDetail*>& cardDetails,
    int honorBonus
)
{
    DeckPowerCalculation result{};
    int attrMap[16] = {};
    for (const auto* card : cardDetails) {
        ++attrMap[card->attr];
        for (auto units = card->unitMask; units; units &= units - 1) {
            const auto unit = std::countr_zero(units);
            ++result.unitCounts[unit];
        }
    }
    for (size_t i = 0; i < cardDetails.size(); ++i) {
        const auto& card = *cardDetails[i];
        DeckCardPowerDetail power{};
        for (auto units = card.unitMask; units; units &= units - 1) {
            const auto unit = std::countr_zero(units);
            const auto& current = card.power.get(unit, result.unitCounts[unit], attrMap[card.attr]);
            if (current.total > power.total)
                power = current;
        }
        result.cards[i] = power;
        result.total.base += power.base;
        result.total.areaItemBonus += power.areaItemBonus;
        result.total.characterBonus += power.characterBonus;
        result.total.fixtureBonus += power.fixtureBonus;
        result.total.gateBonus += power.gateBonus;
        result.total.total += power.total;
    }
    result.total.total += honorBonus;
    return result;
}


int DeckCalculator::getDeckTotalPowerByCards(
    const std::vector<const CardDetail*>& cardDetails,
    int honorBonus
)
{
    const bool fullDeck = cardDetails.size() == 5;
    uint16_t commonUnitMask = fullDeck ? (uint16_t{1} << UNIT_MAX) - 1 : 0;
    const int deckAttr = fullDeck ? cardDetails[0]->attr : 0;
    bool allSameAttr = fullDeck;
    for (const auto* card : cardDetails) {
        commonUnitMask &= card->unitMask;
        allSameAttr &= card->attr == deckAttr;
    }

    int total = honorBonus;
    if (commonUnitMask == 0) {
        for (const auto* card : cardDetails) {
            total += std::max(
                card->powerTotals[0][allSameAttr],
                card->powerTotals[1][allSameAttr]
            );
        }
        return total;
    }

    for (const auto* card : cardDetails) {
        int cardPower = 0;
        int unitIndex = 0;
        for (auto units = card->unitMask; units; units &= units - 1, ++unitIndex) {
            const auto unit = std::countr_zero(units);
            const int state = (commonUnitMask & (uint16_t{1} << unit) ? 2 : 0) + allSameAttr;
            cardPower = std::max(cardPower, card->powerTotals[unitIndex][state]);
        }
        total += cardPower;
    }
    return total;
}


void DeckCalculator::forEachDeckState(
    const std::vector<const CardDetail*> &cardDetails,
    SupportDeckMap& supportCards,
    const DeckStateConsumer& consume,
    int honorBonus,
    std::optional<int> eventType,
    std::optional<int> eventId,
    SkillReferenceChooseStrategy skillReferenceChooseStrategy,
    bool keepAfterTrainingState,
    bool bestSkillAsLeader,
    bool slimPower
)
{
    // 活动加成
    auto eventBonusInfo = getDeckBonus(cardDetails, eventType, eventId);

    // 支援加成
    SupportDeckBonus supportDeckBonus{};
    if (supportCards.size()) {
        SupportDeckCards* pSupportCards = nullptr;
        if (isFinalChapterEvent(eventId.value_or(0)))
            pSupportCards = &supportCards[cardDetails[0]->characterId]; // 终章支援角色为队长角色
        else
            pSupportCards = &(supportCards.begin()->second);    // 普通wl只会处理出一组支援卡牌列表
        supportDeckBonus = this->getSupportDeckBonus(
            cardDetails, *pSupportCards,
            this->getWorldBloomSupportDeckCount(eventId.value_or(0))
        );
    }

    // 评分路径只消费 power.total.total 与 unitCounts，跳过分项综合力物化；
    // powerTotals[槽][状态] 与 power.get(unit, 组合数==5?5:1, 属性数==5?5:1).total 逐值等价
    DeckPowerCalculation powerCalculation{};
    if (slimPower) {
        int attrMap[16] = {};
        for (const auto* card : cardDetails) {
            ++attrMap[card->attr];
            for (auto units = card->unitMask; units; units &= units - 1)
                ++powerCalculation.unitCounts[std::countr_zero(units)];
        }
        int total = honorBonus;
        for (const auto* card : cardDetails) {
            const int attrState = attrMap[card->attr] == 5 ? 1 : 0;
            int best = 0;
            int slot = 0;
            for (auto units = card->unitMask; units; units &= units - 1, ++slot) {
                const auto unit = std::countr_zero(units);
                const int state = (powerCalculation.unitCounts[unit] == 5 ? 2 : 0) + attrState;
                best = std::max(best, card->powerTotals[slot][state]);
            }
            total += best;
        }
        powerCalculation.total.total = total;
    }
    else {
        powerCalculation = getDeckPowerByCards(cardDetails, honorBonus);
    }
    if (eventType == Enums::EventType::world_bloom &&
        this->dataProvider.masterData->getWorldBloomEventTurn(eventId.value_or(0)) == 3) {
        powerCalculation.total.total = std::min(powerCalculation.total.total, 336000);
    }

    const int cardCount = static_cast<int>(cardDetails.size());
    forEachSkillScoreState(
        cardDetails,
        powerCalculation.unitCounts,
        skillReferenceChooseStrategy,
        keepAfterTrainingState,
        bestSkillAsLeader,
        [&](const auto& selectedSkills,
            const auto& scoreValues,
            const auto& referenceInputs,
            const auto& order,
            int doubleSkillMask,
            int statusMask,
            double multiLiveScoreUp) {
            std::array<DeckCardSkillDetail, 5> skills{};
            for (int i = 0; i < cardCount; ++i) {
                skills[i] = *selectedSkills[i];
                skills[i].scoreUp = scoreValues[i];
                skills[i].scoreUpToReference = referenceInputs[i];
            }
            consume(DeckStateView{
                cardDetails,
                eventBonusInfo,
                supportDeckBonus.bonus,
                powerCalculation,
                skills,
                order,
                cardCount,
                doubleSkillMask,
                statusMask,
                multiLiveScoreUp,
            });
        }
    );
}


void DeckCalculator::forEachMultiLiveScoreState(
    const std::vector<const CardDetail*>& cardDetails,
    SupportDeckMap& supportCards,
    const MultiLiveScoreStateConsumer& consume,
    int honorBonus,
    std::optional<int> eventType,
    std::optional<int> eventId,
    SkillReferenceChooseStrategy skillReferenceChooseStrategy,
    bool keepAfterTrainingState,
    bool bestSkillAsLeader
) {
    const auto eventBonus = getDeckBonus(cardDetails, eventType, eventId).totalBonus;

    double supportDeckBonus = 0.0;
    if (!supportCards.empty()) {
        SupportDeckCards* selectedSupportCards = nullptr;
        if (isFinalChapterEvent(eventId.value_or(0)))
            selectedSupportCards = &supportCards[cardDetails[0]->characterId];
        else
            selectedSupportCards = &supportCards.begin()->second;
        supportDeckBonus = getSupportDeckBonus(
            cardDetails,
            *selectedSupportCards,
            getWorldBloomSupportDeckCount(eventId.value_or(0))
        ).bonus;
    }

    int attrMap[16] = {};
    std::array<int, 16> unitCounts{};
    for (const auto* card : cardDetails) {
        ++attrMap[card->attr];
        for (auto units = card->unitMask; units; units &= units - 1)
            ++unitCounts[std::countr_zero(units)];
    }
    int power = honorBonus;
    for (const auto* card : cardDetails) {
        const int attrState = attrMap[card->attr] == 5 ? 1 : 0;
        int best = 0;
        int slot = 0;
        for (auto units = card->unitMask; units; units &= units - 1, ++slot) {
            const int unit = std::countr_zero(units);
            const int state = (unitCounts[unit] == 5 ? 2 : 0) + attrState;
            best = std::max(best, card->powerTotals[slot][state]);
        }
        power += best;
    }
    if (eventType == Enums::EventType::world_bloom &&
        dataProvider.masterData->getWorldBloomEventTurn(eventId.value_or(0)) == 3) {
        power = std::min(power, 336000);
    }

    forEachSkillScoreState(
        cardDetails,
        unitCounts,
        skillReferenceChooseStrategy,
        keepAfterTrainingState,
        bestSkillAsLeader,
        [&](const auto&,
            const auto& scoreValues,
            const auto&,
            const auto& order,
            int,
            int statusMask,
            double multiLiveScoreUp) {
            std::array<double, 5> orderedSkillScoreUps{};
            for (int i = 0; i < 5; ++i)
                orderedSkillScoreUps[i] = scoreValues[order[i]];
            consume(MultiLiveScoreStateView{
                power,
                eventBonus,
                supportDeckBonus,
                orderedSkillScoreUps,
                cardDetails[order[0]]->cardId,
                statusMask,
                multiLiveScoreUp,
            });
        }
    );
}


std::vector<DeckDetail> DeckCalculator::getDeckDetailByCards(
    const std::vector<const CardDetail*> &cardDetails,
    SupportDeckMap& supportCards,
    int honorBonus,
    std::optional<int> eventType,
    std::optional<int> eventId,
    SkillReferenceChooseStrategy skillReferenceChooseStrategy,
    bool keepAfterTrainingState,
    bool bestSkillAsLeader,
    std::optional<int> selectedStatusMask
)
{
    std::vector<DeckDetail> ret{};
    forEachDeckState(
        cardDetails,
        supportCards,
        [&](const DeckStateView& state) {
            if (selectedStatusMask.has_value() && state.statusMask != selectedStatusMask.value())
                return;

            std::vector<DeckCardDetail> cards{};
            cards.reserve(state.cardCount);
            for (int pos = 0; pos < state.cardCount; ++pos) {
                int i = state.order[pos];
                const auto& cardDetail = *state.cardDetails[i];

                int defaultImage = cardDetail.defaultImage;
                if (state.doubleSkillMask & (1 << i)) {
                    defaultImage = state.skills[i].isAfterTraining
                        ? Enums::DefaultImage::special_training
                        : Enums::DefaultImage::original;
                }

                cards.push_back(DeckCardDetail{
                    cardDetail.cardId,
                    cardDetail.level,
                    cardDetail.skillLevel,
                    cardDetail.masterRank,
                    state.power.cards[i],
                    state.eventBonus.cardBonus[i],
                    state.skills[i],
                    cardDetail.episode1Read,
                    cardDetail.episode2Read,
                    cardDetail.afterTraining,
                    defaultImage,
                    cardDetail.hasCanvasBonus,
                });
            }

            ret.push_back(DeckDetail{
                .power = state.power.total,
                .eventBonus = state.eventBonus.totalBonus,
                .supportDeckBonus = state.supportDeckBonus,
                .supportDeckCards = std::nullopt,
                .cards = std::move(cards),
                .multiLiveScoreUp = state.multiLiveScoreUp,
            });
        },
        honorBonus,
        eventType,
        eventId,
        skillReferenceChooseStrategy,
        keepAfterTrainingState,
        bestSkillAsLeader
    );

    return ret;
}


int DeckCalculator::getWorldBloomSupportDeckCount(int eventId) const
{
    int turn = this->dataProvider.masterData->getWorldBloomEventTurn(eventId);
    // wl1 12, wl2 20, wl3 25
    return turn == 1 ? 12 : turn == 2 ? 20 : 25;
}
