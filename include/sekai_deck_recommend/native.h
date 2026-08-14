#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sekai_deck_recommend {

class NativeEngine {
public:
    NativeEngine();
    NativeEngine(const NativeEngine& other);
    NativeEngine(NativeEngine&& other) noexcept;
    NativeEngine& operator=(const NativeEngine& other);
    NativeEngine& operator=(NativeEngine&& other) noexcept;
    ~NativeEngine();

    void updateMasterdata(
        std::map<std::string, std::string> data,
        const std::string& region,
        const std::optional<std::string>& sharedRegion = std::nullopt
    );

    void updateMusicmetas(
        const std::string& data,
        const std::string& region,
        const std::optional<std::string>& sharedRegion = std::nullopt
    );

    nlohmann::json recommend(const nlohmann::json& options) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

void initializeDataPath(const std::string& path);
const std::vector<std::string>& requiredMasterdataKeys();
const std::vector<std::string>& optionalMasterdataKeys();

}
