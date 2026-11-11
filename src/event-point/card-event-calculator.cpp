#include "event-point/card-event-calculator.h"

double CardEventCalculator::getEventDeckBonus(int eventId, const Card &card)
{
    const auto& master = *dataProvider.masterData;
    const auto& character = findOrThrow(master.gameCharacters, [&](const GameCharacter& row) {
        return row.id == card.characterId;
    });
    const int unit = card.supportUnit == Enums::Unit::none ? character.unit : card.supportUnit;
    const auto& characterUnit = findOrThrow(master.gameCharacterUnits, [&](const GameCharacterUnit& row) {
        return row.gameCharacterId == card.characterId && row.unit == unit;
    });

    float bonus = 0;
    // GetEventDeckBonusRate uses the first matching master row. A character
    // match returns directly; the attribute-only branch adds the VS bonus below.
    for (const auto& row : master.eventDeckBonuses) {
        if (row.eventId != eventId)
            continue;
        if (row.gameCharacterUnitId == characterUnit.id &&
            (row.cardAttr == Enums::Attr::null || row.cardAttr == card.attr))
            return float(row.bonusRate);
        if (row.gameCharacterUnitId == 0 && row.cardAttr == card.attr) {
            bonus = float(row.bonusRate);
            break;
        }
    }

    if (character.unit == Enums::Unit::piapro && card.supportUnit == Enums::Unit::none) {
        for (const auto& row : master.eventDeckBonuses) {
            if (row.eventId != eventId || row.gameCharacterUnitId == 0 || row.cardAttr == Enums::Attr::null)
                continue;
            const auto& target = findOrThrow(master.gameCharacterUnits, [&](const GameCharacterUnit& entry) {
                return entry.id == row.gameCharacterUnitId;
            });
            if (target.gameCharacterId == card.characterId)
                return float(bonus + master.virtualSingerEventBonus);
        }
    }
    return bonus;
}

CardEventBonusInfo CardEventCalculator::getCardEventBonus(const UserCard &userCard, int eventId)
{
    auto& cards = this->dataProvider.masterData->cards;
    const auto& card = findOrThrow(cards, [&](const Card& it) {
        return it.id == userCard.cardId; 
    }, [&]() { return "Card not found for cardId=" + std::to_string(userCard.cardId); });
    auto& eventCards = this->dataProvider.masterData->eventCards;
    auto& eventRarityBonusRates = this->dataProvider.masterData->eventRarityBonusRates;

    // 无活动组卡
    if (eventId == this->dataProvider.masterData->getNoEventFakeEventId(Enums::EventType::marathon)
     || eventId == this->dataProvider.masterData->getNoEventFakeEventId(Enums::EventType::cheerful)) {
        return {};
    }

    // 计算角色、属性加成
    double basicBonus = this->getEventDeckBonus(eventId, card);
    // 计算突破等级加成
    auto masterRankBonus = findOrThrow(eventRarityBonusRates, [&](const EventRarityBonusRate& it) { 
        return it.cardRarityType == card.cardRarityType && it.masterRank == userCard.masterRank; 
    }, [&]() { return "Event Rarity Bonus Rate not found for cardRarityType=" + std::to_string(card.cardRarityType) + " masterRank=" + std::to_string(userCard.masterRank); });
    basicBonus += masterRankBonus.bonusRate;

    // 计算当期卡牌加成
    double limitedBonus = 0.0;
    for (const auto& it : eventCards) {
        if (it.eventId == eventId && it.cardId == card.id) {
            limitedBonus = it.bonusRate;
            break;
        }
    }

    // 终章机制
    if (isFinalChapterEvent(eventId)) {
        // 1k牌加成，两轮终章各认各的章节称号
        double leaderHonorBonus = eventId == finalChapterEventId
            ? this->dataProvider.userData->userCharacterFinalChapterHonorEventBonusMap[card.characterId]
            : this->dataProvider.userData->userCharacterFinalChapter2HonorEventBonusMap[card.characterId];
        // 当期队长加成
        double leaderLimitBonus = 0.0;
        for (const auto& it : eventCards) {
            if (it.eventId == eventId && it.cardId == card.id) {
                leaderLimitBonus = it.leaderBonusRate;
                break;
            }
        }
        return CardEventBonusInfo{
            .maxBonus = basicBonus + limitedBonus + leaderHonorBonus + leaderLimitBonus,
            .minBonus = basicBonus,     // 终章的当期加成和队长相关的加成都可能不生效
            .limitedBonus = limitedBonus,
            .leaderHonorBonus = leaderHonorBonus,
            .leaderLimitBonus = leaderLimitBonus,
        };
    }
    else {
        return CardEventBonusInfo{
            .maxBonus = basicBonus + limitedBonus,
            .minBonus = basicBonus + limitedBonus,
            .limitedBonus = limitedBonus,
            .leaderHonorBonus = 0.0,
            .leaderLimitBonus = 0.0,
        };
    }
}
