#ifndef DECK_SIMULATION_H
#define DECK_SIMULATION_H

#include "data-provider/data-provider.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>

enum class AreaItemSimulationMode {
    Current,
    Empty,
    Max,
    Uniform,
    Minimum,
};

struct AreaItemSimulationConfig {
    AreaItemSimulationMode mode = AreaItemSimulationMode::Current;
    std::optional<int> level = std::nullopt;
    std::unordered_map<int, int> itemLevels = {};
};

enum class CharacterRankSimulationMode {
    Current,
    Max,
    Fixed,
};

struct CharacterRankSimulationConfig {
    CharacterRankSimulationMode mode = CharacterRankSimulationMode::Current;
    std::optional<int> level = std::nullopt;
    std::unordered_map<int, int> overrides = {};
};

struct HonorSimulationOverride {
    std::optional<bool> enabled = std::nullopt;
    std::optional<int> level = std::nullopt;
};

struct HonorSimulationConfig {
    bool useCurrent = true;
    std::unordered_map<int, HonorSimulationOverride> overrides = {};
};

struct MysekaiFixtureSimulationConfig {
    bool useCurrent = true;
    // Public API uses percent units; UserData stores 0.1 percent units.
    std::unordered_map<int, double> bonusRatePercent = {};
};

struct MysekaiGateSimulationOverride {
    std::optional<bool> enabled = std::nullopt;
    std::optional<int> level = std::nullopt;
};

struct MysekaiGateSimulationConfig {
    bool useCurrent = true;
    std::unordered_map<int, MysekaiGateSimulationOverride> overrides = {};
};

struct MysekaiCanvasSimulationConfig {
    bool useCurrent = true;
    std::unordered_set<int> include = {};
    std::unordered_set<int> exclude = {};
};

struct MysekaiSimulationConfig {
    std::optional<MysekaiFixtureSimulationConfig> fixtures = std::nullopt;
    std::optional<MysekaiGateSimulationConfig> gates = std::nullopt;
    std::optional<MysekaiCanvasSimulationConfig> canvas = std::nullopt;
};

void applyUserDataOverrides(
    UserData& userData,
    const MasterData& masterData,
    const std::optional<AreaItemSimulationConfig>& areaItems,
    const std::optional<CharacterRankSimulationConfig>& characterRanks,
    const std::optional<HonorSimulationConfig>& honors,
    const std::optional<MysekaiSimulationConfig>& mysekai
);

#endif
