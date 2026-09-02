#include "deck-recommend/deck-simulation.h"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace {

std::map<int, int> maxAreaItemLevels(const MasterData& masterData) {
    std::map<int, int> result;
    for (const auto& item : masterData.areaItemLevels)
        result[item.areaItemId] = std::max(result[item.areaItemId], item.level);
    return result;
}

void applyAreaItemSimulation(
    UserData& userData,
    const MasterData& masterData,
    const AreaItemSimulationConfig& config
) {
    const auto maxLevels = maxAreaItemLevels(masterData);

    const auto validateLevel = [&maxLevels](int itemId, int level) {
        const auto maxIt = maxLevels.find(itemId);
        if (maxIt == maxLevels.end())
            throw std::invalid_argument("Area item not found for areaItemId=" + std::to_string(itemId));
        if (level < 0 || level > maxIt->second)
            throw std::invalid_argument(
                "Invalid area item level for areaItemId=" + std::to_string(itemId) +
                ": " + std::to_string(level)
            );
    };
    const auto setItemLevel = [&userData](int itemId, int level, bool minimum) {
        bool found = false;
        for (auto& area : userData.userAreas) {
            for (auto& item : area.areaItems) {
                if (item.areaItemId != itemId)
                    continue;
                item.level = minimum ? std::max(item.level, level) : level;
                found = true;
            }
        }
        if (!found && level > 0) {
            if (userData.userAreas.empty())
                userData.userAreas.push_back(UserArea{});
            userData.userAreas.front().areaItems.push_back(UserAreaItems{itemId, level});
        }
        if (level == 0) {
            for (auto& area : userData.userAreas) {
                std::erase_if(area.areaItems, [itemId](const UserAreaItems& item) {
                    return item.areaItemId == itemId;
                });
            }
        }
    };
    const auto clearItems = [&userData]() {
        for (auto& area : userData.userAreas)
            area.areaItems.clear();
    };

    if (config.mode == AreaItemSimulationMode::Empty) {
        clearItems();
    }
    else if (config.mode == AreaItemSimulationMode::Max) {
        clearItems();
        for (const auto& [itemId, level] : maxLevels)
            setItemLevel(itemId, level, false);
    }
    else if (config.mode == AreaItemSimulationMode::Uniform) {
        if (!config.level.has_value())
            throw std::invalid_argument("area_item_config.level is required for uniform mode");
        if (config.level.value() < 0)
            throw std::invalid_argument("Invalid area item level: " + std::to_string(config.level.value()));
        clearItems();
        for (const auto& [itemId, maxLevel] : maxLevels) {
            if (config.level.value() > maxLevel)
                throw std::invalid_argument("Invalid area item level: " + std::to_string(config.level.value()));
            setItemLevel(itemId, config.level.value(), false);
        }
    }
    else if (config.mode == AreaItemSimulationMode::Minimum) {
        if (!config.level.has_value())
            throw std::invalid_argument("area_item_config.level is required for minimum mode");
        if (config.level.value() < 0)
            throw std::invalid_argument("Invalid area item level: " + std::to_string(config.level.value()));
        for (const auto& [itemId, maxLevel] : maxLevels) {
            if (config.level.value() > maxLevel)
                throw std::invalid_argument("Invalid area item level: " + std::to_string(config.level.value()));
            setItemLevel(itemId, config.level.value(), true);
        }
    }

    for (const auto& [itemId, level] : config.itemLevels) {
        validateLevel(itemId, level);
        setItemLevel(itemId, level, false);
    }
}

std::map<int, int> maxCharacterRanks(const MasterData& masterData) {
    std::map<int, int> result;
    for (const auto& rank : masterData.characterRanks)
        result[rank.characterId] = std::max(result[rank.characterId], rank.characterRank);
    return result;
}

void validateCharacterRank(
    const MasterData& masterData,
    int characterId,
    int rank
) {
    const auto it = std::find_if(
        masterData.characterRanks.begin(), masterData.characterRanks.end(),
        [characterId, rank](const CharacterRank& item) {
            return item.characterId == characterId && item.characterRank == rank;
        }
    );
    if (it == masterData.characterRanks.end())
        throw std::invalid_argument(
            "Character rank not found for characterId=" + std::to_string(characterId) +
            " rank=" + std::to_string(rank)
        );
}

void applyCharacterRankSimulation(
    UserData& userData,
    const MasterData& masterData,
    const CharacterRankSimulationConfig& config
) {
    const auto maxRanks = maxCharacterRanks(masterData);
    std::map<int, int> effective;
    for (const auto& character : userData.userCharacters)
        effective[character.characterId] = character.characterRank;

    if (config.mode == CharacterRankSimulationMode::Fixed) {
        if (!config.level.has_value())
            throw std::invalid_argument("character_rank_config.level is required for fixed mode");
        for (const auto& [characterId, maxRank] : maxRanks) {
            if (config.level.value() > maxRank)
                throw std::invalid_argument("Invalid character rank: " + std::to_string(config.level.value()));
            validateCharacterRank(masterData, characterId, config.level.value());
            effective[characterId] = config.level.value();
        }
    }
    else if (config.mode == CharacterRankSimulationMode::Max) {
        effective = maxRanks;
    }

    for (const auto& [characterId, rank] : config.overrides) {
        if (!maxRanks.count(characterId))
            throw std::invalid_argument("Character not found for characterId=" + std::to_string(characterId));
        validateCharacterRank(masterData, characterId, rank);
        effective[characterId] = rank;
    }

    std::map<int, UserCharacter> existing;
    for (const auto& character : userData.userCharacters)
        existing[character.characterId] = character;
    userData.userCharacters.clear();
    for (const auto& [characterId, rank] : effective) {
        auto character = existing.count(characterId)
            ? existing.at(characterId)
            : UserCharacter{};
        character.characterId = characterId;
        character.characterRank = rank;
        userData.userCharacters.push_back(character);
    }
}

const Honor* findHonor(const MasterData& masterData, int honorId) {
    const auto it = std::find_if(
        masterData.honors.begin(), masterData.honors.end(),
        [honorId](const Honor& honor) { return honor.id == honorId; }
    );
    return it == masterData.honors.end() ? nullptr : &*it;
}

void validateHonorLevel(const Honor& honor, int level) {
    const auto it = std::find_if(
        honor.levels.begin(), honor.levels.end(),
        [level](const HonorLevel& item) { return item.level == level; }
    );
    if (it == honor.levels.end())
        throw std::invalid_argument(
            "Honor level not found for honorId=" + std::to_string(honor.id) +
            " level=" + std::to_string(level)
        );
}

void applyHonorSimulation(
    UserData& userData,
    const MasterData& masterData,
    const HonorSimulationConfig& config
) {
    std::map<int, UserHonor> effective;
    if (config.useCurrent)
        for (const auto& honor : userData.userHonors)
            effective[honor.honorId] = honor;

    for (const auto& [honorId, overrideConfig] : config.overrides) {
        const auto* honor = findHonor(masterData, honorId);
        if (honor == nullptr)
            throw std::invalid_argument("Honor not found for honorId=" + std::to_string(honorId));

        if (overrideConfig.enabled.has_value() && !overrideConfig.enabled.value()) {
            effective.erase(honorId);
            continue;
        }

        const auto existing = effective.find(honorId);
        if (existing == effective.end() &&
            (!overrideConfig.enabled.has_value() || !overrideConfig.enabled.value())) {
            throw std::invalid_argument(
                "Honor simulation for a new honor requires enabled=true: " +
                std::to_string(honorId)
            );
        }
        if (existing == effective.end()) {
            if (!overrideConfig.level.has_value())
                throw std::invalid_argument(
                    "Honor level is required when simulating honorId=" + std::to_string(honorId)
                );
            effective[honorId] = UserHonor{honorId, overrideConfig.level.value()};
        }
        else if (overrideConfig.level.has_value()) {
            existing->second.level = overrideConfig.level.value();
        }
        validateHonorLevel(*honor, effective.at(honorId).level);
    }

    userData.userHonors.clear();
    for (const auto& [honorId, honor] : effective)
        userData.userHonors.push_back(honor);
}

void applyMysekaiSimulation(
    UserData& userData,
    const MasterData& masterData,
    const MysekaiSimulationConfig& config
) {
    if (config.fixtures.has_value()) {
        std::map<int, double> rates;
        if (config.fixtures->useCurrent)
            for (const auto& bonus : userData.userMysekaiFixtureGameCharacterPerformanceBonuses)
                rates[bonus.gameCharacterId] = bonus.totalBonusRate / 10.0;
        for (const auto& [characterId, rate] : config.fixtures->bonusRatePercent) {
            const int targetCharacterId = characterId;
            if (std::none_of(masterData.gameCharacters.begin(), masterData.gameCharacters.end(),
                [targetCharacterId](const GameCharacter& item) {
                    return item.id == targetCharacterId;
                }))
                throw std::invalid_argument("Character not found for fixture simulation: " + std::to_string(characterId));
            if (rate < 0.0)
                throw std::invalid_argument("Invalid fixture bonus rate: " + std::to_string(rate));
            rates[characterId] = rate;
        }
        userData.userMysekaiFixtureGameCharacterPerformanceBonuses.clear();
        for (const auto& [characterId, rate] : rates)
            if (rate > 0.0)
                userData.userMysekaiFixtureGameCharacterPerformanceBonuses.push_back({characterId, rate * 10.0});
    }

    if (config.gates.has_value()) {
        std::map<int, UserMysekaiGate> gates;
        if (config.gates->useCurrent)
            for (const auto& gate : userData.userMysekaiGates)
                gates[gate.mysekaiGateId] = gate;

        for (const auto& [gateId, overrideConfig] : config.gates->overrides) {
            const int targetGateId = gateId;
            const auto gateIt = std::find_if(
                masterData.mysekaiGates.begin(), masterData.mysekaiGates.end(),
                [targetGateId](const MysekaiGate& item) { return item.id == targetGateId; }
            );
            if (gateIt == masterData.mysekaiGates.end())
                throw std::invalid_argument("Mysekai gate not found for mysekaiGateId=" + std::to_string(gateId));
            if (overrideConfig.enabled.has_value() && !overrideConfig.enabled.value()) {
                gates.erase(gateId);
                continue;
            }
            const auto existing = gates.find(gateId);
            if (existing == gates.end() &&
                (!overrideConfig.enabled.has_value() || !overrideConfig.enabled.value()))
                throw std::invalid_argument(
                    "Mysekai gate simulation for a new gate requires enabled=true: " +
                    std::to_string(gateId)
                );
            if (existing == gates.end()) {
                if (!overrideConfig.level.has_value())
                    throw std::invalid_argument(
                        "Gate level is required when simulating mysekaiGateId=" + std::to_string(gateId)
                    );
                gates[gateId] = UserMysekaiGate{gateId, 0, overrideConfig.level.value(), 0, false};
            }
            else if (overrideConfig.level.has_value()) {
                existing->second.mysekaiGateLevel = overrideConfig.level.value();
            }
            const int targetGateLevel = gates.at(gateId).mysekaiGateLevel;
            const auto levelIt = std::find_if(
                masterData.mysekaiGateLevels.begin(), masterData.mysekaiGateLevels.end(),
                [targetGateId, targetGateLevel](const MysekaiGateLevel& item) {
                    return item.mysekaiGateId == targetGateId && item.level == targetGateLevel;
                }
            );
            if (levelIt == masterData.mysekaiGateLevels.end())
                throw std::invalid_argument(
                    "Mysekai gate level not found for mysekaiGateId=" + std::to_string(gateId) +
                    " level=" + std::to_string(gates.at(gateId).mysekaiGateLevel)
                );
        }

        userData.userMysekaiGates.clear();
        for (const auto& [gateId, gate] : gates)
            userData.userMysekaiGates.push_back(gate);
    }

    if (config.canvas.has_value()) {
        std::set<int> cards;
        if (config.canvas->useCurrent)
            for (const auto& canvas : userData.userMysekaiCanvases)
                cards.insert(canvas.cardId);
        for (const auto cardId : config.canvas->exclude) {
            const auto cardIt = std::find_if(
                masterData.cards.begin(), masterData.cards.end(),
                [cardId](const Card& item) { return item.id == cardId; }
            );
            if (cardIt == masterData.cards.end())
                throw std::invalid_argument("Card not found for canvas simulation: " + std::to_string(cardId));
            cards.erase(cardId);
        }
        for (const auto cardId : config.canvas->include) {
            const auto cardIt = std::find_if(
                masterData.cards.begin(), masterData.cards.end(),
                [cardId](const Card& item) { return item.id == cardId; }
            );
            if (cardIt == masterData.cards.end())
                throw std::invalid_argument("Card not found for canvas simulation: " + std::to_string(cardId));
            cards.insert(cardId);
        }
        userData.userMysekaiCanvases.clear();
        for (const auto cardId : cards)
            userData.userMysekaiCanvases.push_back(UserMysekaiCanvas{0, cardId, false, 1});
    }
}

}  // namespace

void applyUserDataOverrides(
    UserData& userData,
    const MasterData& masterData,
    const std::optional<AreaItemSimulationConfig>& areaItems,
    const std::optional<CharacterRankSimulationConfig>& characterRanks,
    const std::optional<HonorSimulationConfig>& honors,
    const std::optional<MysekaiSimulationConfig>& mysekai
) {
    if (areaItems.has_value())
        applyAreaItemSimulation(userData, masterData, areaItems.value());
    if (characterRanks.has_value())
        applyCharacterRankSimulation(userData, masterData, characterRanks.value());
    if (honors.has_value())
        applyHonorSimulation(userData, masterData, honors.value());
    if (mysekai.has_value())
        applyMysekaiSimulation(userData, masterData, mysekai.value());
}
