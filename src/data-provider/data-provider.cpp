#include "data-provider.h"

#include <algorithm>
#include <string_view>

namespace {

// 从 assetbundleName 里取 <prefix><字段>_cp<章节> 的两段。
// 称号命名来自主数据，格式不符时返回 nullopt 让调用方跳过该称号；
// 解析失败不应抛异常，否则会连带整个组卡请求一起失败。
struct HonorChapterRef {
    std::string_view field;     // prefix 与 "_cp" 之间的内容
    int chapter = 0;            // "_cp" 之后的一位数字
};

std::optional<HonorChapterRef> parseHonorChapterRef(
    std::string_view name,
    std::string_view prefix
) {
    const auto prefixPos = name.find(prefix);
    if (prefixPos == std::string_view::npos)
        return std::nullopt;

    const auto fieldPos = prefixPos + prefix.size();
    const auto markerPos = name.find("_cp", fieldPos);
    if (markerPos == std::string_view::npos || markerPos <= fieldPos)
        return std::nullopt;

    const auto chapterPos = markerPos + 3;
    if (chapterPos >= name.size())
        return std::nullopt;
    const char chapterDigit = name[chapterPos];
    if (chapterDigit < '0' || chapterDigit > '9')
        return std::nullopt;

    return HonorChapterRef{
        name.substr(fieldPos, markerPos - fieldPos),
        chapterDigit - '0',
    };
}

// 纯数字且长度合理时转 int，否则 nullopt（避免 std::stoi 抛 invalid_argument/out_of_range）
std::optional<int> parseSmallInt(std::string_view text) {
    if (text.empty() || text.size() > 3)
        return std::nullopt;
    int value = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9')
            return std::nullopt;
        value = value * 10 + (digit - '0');
    }
    return value;
}

}  // namespace

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
            if (const auto ref = parseHonorChapterRef(honor->assetbundleName, "wl_2nd_")) {
                const auto unitIt = unitCharacters.find(std::string(ref->field));
                if (unitIt != unitCharacters.end()) {
                    const auto& characters = unitIt->second;
                    for (auto& item : masterData->worldBlooms) {
                        // 只匹配真实WL2章节；假活动的章节按角色ID升序合成，顺序与真实章节不同
                        if (characters.count(item.gameCharacterId) && item.chapterNo == ref->chapter
                            && item.eventId > 140 && item.eventId < 1000) {
                            userData->userCharacterFinalChapterHonorEventBonusMap[item.gameCharacterId] = 50.0;
                        }
                    }
                }
            }

            // 终章2：wl_3rd_part<第几场>_cp<章节>章节排名称号（2025年以前的章节称号不生效）
            if (const auto ref = parseHonorChapterRef(honor->assetbundleName, "wl_3rd_part")) {
                const auto part = parseSmallInt(ref->field);
                if (part.has_value() && *part >= 1 && *part <= int(wl3ChapterEvents.size())) {
                    const int chapterEventId = wl3ChapterEvents[*part - 1]->id;
                    for (auto& item : masterData->worldBlooms) {
                        if (item.eventId == chapterEventId && item.chapterNo == ref->chapter) {
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
