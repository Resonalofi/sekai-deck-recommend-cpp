#ifndef MASTER_DATA_PROVIDER_H
#define MASTER_DATA_PROVIDER_H

#include <map>
#include <memory>
#include <string_view>

#include "data-provider/master-data-types.h"


constexpr int finalChapterEventId = 180;

// 主数据 JSON 原文按 key 索引；只持有调用方内存的视图，解析期间不再复制一份
using MasterDataStrings = std::map<std::string, std::string_view>;

extern const std::vector<std::string> requiredMasterDataKeys;
extern const std::vector<std::string> notRequiredMasterDataKeys;

struct MasterDataCore {
    std::vector<AreaItemLevel> areaItemLevels;
    std::vector<AreaItem> areaItems;
    std::vector<Area> areas;
    std::vector<CardEpisode> cardEpisodes;
    std::vector<Card> cards;
    std::vector<CardMysekaiCanvasBonus> cardMysekaiCanvasBonuses;
    std::vector<CardRarity> cardRarities;
    std::vector<CharacterRank> characterRanks;
    std::vector<EventCard> eventCards;
    std::vector<EventDeckBonus> eventDeckBonuses;
    std::vector<EventExchangeSummary> eventExchangeSummaries;
    std::vector<Event> events;
    std::vector<EventItem> eventItems;
    std::vector<EventRarityBonusRate> eventRarityBonusRates;
    std::vector<GameCharacter> gameCharacters;
    std::vector<GameCharacterUnit> gameCharacterUnits;
    std::vector<MasterLesson> masterLessons;
    std::vector<MusicDifficulty> musicDifficulties;
    std::vector<Music> musics;
    std::vector<MusicVocal> musicVocals;
    std::vector<MysekaiFixtureGameCharacterGroup> mysekaiFixtureGameCharacterGroups;
    std::vector<MysekaiFixtureGameCharacterGroupPerformanceBonus> mysekaiFixtureGameCharacterGroupPerformanceBonuses;
    std::vector<MysekaiGate> mysekaiGates;
    std::vector<MysekaiGateLevel> mysekaiGateLevels;
    std::vector<ShopItem> shopItems;
    std::vector<Skill> skills;
    std::vector<WorldBloomDifferentAttributeBonus> worldBloomDifferentAttributeBonuses;
    std::vector<WorldBloom> worldBlooms;
    std::vector<WorldBloomSupportDeckUnitEventLimitedBonus> worldBloomSupportDeckUnitEventLimitedBonuses;
    std::vector<WorldBloomSupportDeckBonus> worldBloomSupportDeckBonusesWL1;
    std::vector<WorldBloomSupportDeckBonus> worldBloomSupportDeckBonusesWL2;
    std::vector<WorldBloomSupportDeckBonus> worldBloomSupportDeckBonusesWL3;
};

class MasterData {

private:

    std::shared_ptr<MasterDataCore> storage;

    void addFakeEvent(int eventType);
    void finishLoad();

public:
    MasterData();
    explicit MasterData(std::shared_ptr<MasterDataCore> storage);

    std::string baseDir;

    std::vector<AreaItemLevel>& areaItemLevels;
    std::vector<AreaItem>& areaItems;
    std::vector<Area>& areas;
    std::vector<CardEpisode>& cardEpisodes;
    std::vector<Card>& cards;
    std::vector<CardMysekaiCanvasBonus>& cardMysekaiCanvasBonuses;
    std::vector<CardRarity>& cardRarities;
    std::vector<CharacterRank>& characterRanks;
    std::vector<EventCard>& eventCards;
    std::vector<EventDeckBonus>& eventDeckBonuses;
    std::vector<EventExchangeSummary>& eventExchangeSummaries;
    std::vector<Event>& events;
    std::vector<EventItem>& eventItems;
    std::vector<EventRarityBonusRate>& eventRarityBonusRates;
    std::vector<GameCharacter>& gameCharacters;
    std::vector<GameCharacterUnit>& gameCharacterUnits;
    std::vector<Honor> honors;
    std::vector<MasterLesson>& masterLessons;
    std::vector<MusicDifficulty>& musicDifficulties;
    std::vector<Music>& musics;
    std::vector<MusicVocal>& musicVocals;
    std::vector<MysekaiFixtureGameCharacterGroup>& mysekaiFixtureGameCharacterGroups;
    std::vector<MysekaiFixtureGameCharacterGroupPerformanceBonus>& mysekaiFixtureGameCharacterGroupPerformanceBonuses;
    std::vector<MysekaiGate>& mysekaiGates;
    std::vector<MysekaiGateLevel>& mysekaiGateLevels;
    std::vector<ShopItem>& shopItems;
    std::vector<Skill>& skills;
    std::vector<WorldBloomDifferentAttributeBonus>& worldBloomDifferentAttributeBonuses;
    std::vector<WorldBloom>& worldBlooms;
    std::vector<WorldBloomSupportDeckUnitEventLimitedBonus>& worldBloomSupportDeckUnitEventLimitedBonuses;
    std::vector<WorldBloomSupportDeckBonus>& worldBloomSupportDeckBonusesWL1;
    std::vector<WorldBloomSupportDeckBonus>& worldBloomSupportDeckBonusesWL2;
    std::vector<WorldBloomSupportDeckBonus>& worldBloomSupportDeckBonusesWL3;

    std::shared_ptr<MasterDataCore> sharedCore() const;

    void loadFromJsons(std::map<std::string, json>& jsons);

    void loadFromFiles(const std::string& baseDir);

    void loadFromStrings(MasterDataStrings& data);

    void loadHonorsFromStrings(MasterDataStrings& data);

    int getNoEventFakeEventId(int eventType) const;

    int getUnitAttrFakeEventId(int eventType, int unit, int attr) const;

    int getWorldBloomFakeEventId(int worldBloomTurn, int unitOrGroup) const;

    int getWorldBloomEventTurn(int eventId) const;

};

#endif // MASTER_DATA_PROVIDER_H
