#include "data-provider.h"

#include <algorithm>

void DataProvider::init()
{
    if (inited) return;

    std::map<std::string, std::set<int>> unitCharacters = {
        { "lightsound", {1, 2, 3, 4} },
        { "idol", {5, 6, 7, 8} },
        { "street", {9, 10, 11, 12} },
        { "themepark", {13, 14, 15, 16} },
        { "schoolrefusal", {17, 18, 19, 20} },
        { "piapro", {21, 22, 23, 24, 25, 26} },
    };

    // 按开始时间升序的真实WL3章节活动，wl_3rd_part<P>称号的P即其中第P场
    std::vector<const Event*> wl3ChapterEvents{};
    for (const auto& event : masterData->events) {
        if (event.eventType == Enums::EventType::world_bloom && event.id < 1000 &&
            event.id != finalChapter2EventId &&
            masterData->getWorldBloomEventTurn(event.id) == 3) {
            wl3ChapterEvents.push_back(&event);
        }
    }
    std::sort(wl3ChapterEvents.begin(), wl3ChapterEvents.end(), [](const Event* a, const Event* b) {
        return a->startAt < b->startAt;
    });

    // 预处理用户哪些角色有终章称号活动加成
    userData->userCharacterFinalChapterHonorEventBonusMap.clear();
    userData->userCharacterFinalChapter2HonorEventBonusMap.clear();
    for (const auto& userHonor : userData->userHonors) {
        try {
            auto* honor = masterData->findHonorById(userHonor.honorId);
            if (honor == nullptr)
                throw ElementNoFoundError("Element not found");
            if (honor->honorRarity != Enums::HonorRarity::high
             && honor->honorRarity != Enums::HonorRarity::highest)
                continue;

            // 终章1：wl_2nd_<团名>_cp<章节>章节排名称号
            auto start_idx = honor->assetbundleName.find("wl_2nd");
            if (start_idx != std::string::npos) {
                start_idx += 7;
                auto end_idx = honor->assetbundleName.find("_cp", start_idx);
                auto unit_name = honor->assetbundleName.substr(start_idx, end_idx - start_idx);
                int chapter = std::stoi(honor->assetbundleName.substr(end_idx + 3, 1));
                auto& characters = unitCharacters[unit_name];
                for (auto& item : masterData->worldBlooms) {
                    // 只匹配真实WL2章节；假活动的章节按角色ID升序合成，顺序与真实章节不同
                    if (characters.count(item.gameCharacterId) && item.chapterNo == chapter
                        && item.eventId > 140 && item.eventId < 1000) {
                        userData->userCharacterFinalChapterHonorEventBonusMap[item.gameCharacterId] = 50.0;
                    }
                }
            }

            // 终章2：wl_3rd_part<第几场>_cp<章节>章节排名称号（2025年以前的章节称号不生效）
            start_idx = honor->assetbundleName.find("wl_3rd_part");
            if (start_idx != std::string::npos) {
                start_idx += 11;
                auto end_idx = honor->assetbundleName.find("_cp", start_idx);
                int part = std::stoi(honor->assetbundleName.substr(start_idx, end_idx - start_idx));
                int chapter = std::stoi(honor->assetbundleName.substr(end_idx + 3, 1));
                if (part >= 1 && part <= int(wl3ChapterEvents.size())) {
                    const int chapterEventId = wl3ChapterEvents[part - 1]->id;
                    for (auto& item : masterData->worldBlooms) {
                        if (item.eventId == chapterEventId && item.chapterNo == chapter) {
                            userData->userCharacterFinalChapter2HonorEventBonusMap[item.gameCharacterId] = 50.0;
                        }
                    }
                }
            }
        } catch (const ElementNoFoundError& e) {
            std::cerr << "[warning] honor id " << userHonor.honorId << " appears in user data but not in master data." << std::endl;
        }
    }
}
