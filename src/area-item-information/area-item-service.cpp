#include "area-item-information/area-item-service.h"

std::vector<AreaItemLevel> AreaItemService::getAreaItemLevels()
{
    auto& userAreas = this->dataProvider.userData->userAreas;
    std::vector<AreaItemLevel> areaItemLevels{};
    for (const auto& userArea : userAreas) {
        for (const auto& areaItem : userArea.areaItems) {
            const auto rows = this->getAreaItemLevel(areaItem.areaItemId, areaItem.level);
            areaItemLevels.insert(areaItemLevels.end(), rows.begin(), rows.end());
        }
    }
    return areaItemLevels;
}

std::vector<AreaItemLevel> AreaItemService::getAreaItemLevel(int areaItemId, int level)
{
    std::vector<AreaItemLevel> rows;
    // 同一道具、同等级可有多条效果，按 master 顺序参与逐维累加。
    for (const auto& row : dataProvider.masterData->areaItemLevels) {
        if (row.areaItemId == areaItemId && row.level == level)
            rows.push_back(row);
    }
    if (rows.empty())
        throw ElementNoFoundError("Area item level not found for areaItemId=" + std::to_string(areaItemId) + " level=" + std::to_string(level));
    return rows;
}

std::vector<AreaItemLevel> AreaItemService::getAreaItemNextLevel(const AreaItem &areaItem, std::optional<AreaItemLevel> areaItemLevel)
{
    int maxLevel = 0;
    for (const auto& row : dataProvider.masterData->areaItemLevels) {
        if (row.areaItemId == areaItem.id)
            maxLevel = std::max(maxLevel, row.level);
    }
    int level = areaItemLevel.has_value() ? std::min(areaItemLevel->level + 1, maxLevel) : 1;
    return this->getAreaItemLevel(areaItem.id, level);
}

ShopItem AreaItemService::getShopItem(const AreaItemLevel &areaItemLevel)
{
    auto& shopItems = this->dataProvider.masterData->shopItems;
    // 56号独立编号；原道具按1-10、11-15、16-20级分段编号。
    int id = areaItemLevel.areaItemId == 56 ? 2100 + areaItemLevel.level
        : areaItemLevel.level <= 10 ? 1000 + (areaItemLevel.areaItemId - 1) * 10 + areaItemLevel.level
        : areaItemLevel.level <= 15 ? 1550 + (areaItemLevel.areaItemId - 1) * 5 + areaItemLevel.level - 10
        : 1825 + (areaItemLevel.areaItemId - 1) * 5 + areaItemLevel.level - 15;
    return findOrThrow(shopItems, [&](const ShopItem& it) {
        return it.id == id;
    }, [&]() { return "Shop item not found for areaItemId=" + std::to_string(areaItemLevel.areaItemId) + " level=" + std::to_string(areaItemLevel.level); } );
}
