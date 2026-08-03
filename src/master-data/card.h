#ifndef CARD_H
#define CARD_H

#include "common/collection-utils.h"
#include <array>

struct Card {
    int id = 0;
    int seq = 0;
    int characterId = 0;
    int cardRarityType;
    int specialTrainingPower1BonusFixed = 0;
    int specialTrainingPower2BonusFixed = 0;
    int specialTrainingPower3BonusFixed = 0;
    int attr;
    int supportUnit;
    int skillId = 0;
    int specialTrainingSkillId = 0;
    std::vector<std::array<int, 3>> levelPowers;

    static inline Card fromJson(const json& item) {
        Card card;
        card.id = item.value("id", 0);
        card.seq = item.value("seq", 0);
        card.characterId = item.value("characterId", 0);
        card.cardRarityType = mapEnum(EnumMap::cardRarityType, item.value("cardRarityType", ""));
        card.specialTrainingPower1BonusFixed = item.value("specialTrainingPower1BonusFixed", 0);
        card.specialTrainingPower2BonusFixed = item.value("specialTrainingPower2BonusFixed", 0);
        card.specialTrainingPower3BonusFixed = item.value("specialTrainingPower3BonusFixed", 0);
        card.attr = mapEnum(EnumMap::attr, item.value("attr", ""));
        card.supportUnit = mapEnum(EnumMap::unit, item.value("supportUnit", ""));
        card.skillId = item.value("skillId", 0);
        card.specialTrainingSkillId = item.value("specialTrainingSkillId", 0);

        if (item["cardParameters"].is_array()) {
            // 日服格式 card param
            card.levelPowers.reserve(item["cardParameters"].size() / 3);
            for (const auto& parameter : item["cardParameters"]) {
                int level = parameter.value("cardLevel", 0);
                int type = mapEnum(EnumMap::cardParameterType, parameter.value("cardParameterType", ""));
                int power = parameter.value("power", 0);
                if (level <= 0)
                    continue;
                if (card.levelPowers.size() < static_cast<size_t>(level))
                    card.levelPowers.resize(level);
                if (type == Enums::CardParameterType::param1)
                    card.levelPowers[level - 1][0] = power;
                else if (type == Enums::CardParameterType::param2)
                    card.levelPowers[level - 1][1] = power;
                else if (type == Enums::CardParameterType::param3)
                    card.levelPowers[level - 1][2] = power;
            }
        } else {
            // 国服格式 card param
            const std::array<std::string, 3> keys = { "param1", "param2", "param3" };
            for (size_t type = 0; type < keys.size(); ++type) {
                const auto& powers = item["cardParameters"][keys[type]];
                if (card.levelPowers.size() < powers.size())
                    card.levelPowers.resize(powers.size());
                for (size_t level = 0; level < powers.size(); ++level) {
                    card.levelPowers[level][type] = powers[level];
                }
            }
        }
        return card;
    }

    static inline std::vector<Card> fromJsonList(const json& jsonData) {
        std::vector<Card> cards;
        cards.reserve(jsonData.size());
        for (const auto& item : jsonData) {
            cards.push_back(fromJson(item));
        }
        return cards;
    }
};

#endif // CARD_H
