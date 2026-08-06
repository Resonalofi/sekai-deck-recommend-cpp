#include "data-provider/master-data.h"
#include "data-provider/static-data.h"

#include <fstream>
#include <iostream>
#include <exception>
#include <limits>
#include <unordered_set>
#include <vector>
#include "master-data.h"


namespace {

class CardJsonSax final : public nlohmann::json_sax<json> {
    struct Frame {
        json value;
        std::string key;
        bool keep;
        bool object;
    };

    static const std::unordered_set<std::string> keptKeys;

    std::vector<Frame> frames;
    std::vector<Card> cards;
    bool rootArrayStarted = false;
    std::exception_ptr conversionError;

    bool keepNextValue() const {
        if (frames.empty())
            return true;
        const auto& parent = frames.back();
        return parent.keep && (!parent.object || keptKeys.contains(parent.key));
    }

    void finishValue(json value, bool keep) {
        if (frames.empty()) {
            if (keep && conversionError == nullptr) {
                try {
                    cards.push_back(Card::fromJson(value));
                }
                catch (...) {
                    conversionError = std::current_exception();
                }
            }
            return;
        }

        auto& parent = frames.back();
        if (keep) {
            if (parent.object)
                parent.value[parent.key] = std::move(value);
            else
                parent.value.push_back(std::move(value));
        }
        if (parent.object)
            parent.key.clear();
    }

    bool primitive(json value) {
        const bool keep = keepNextValue();
        finishValue(std::move(value), keep);
        return true;
    }

    bool startContainer(bool object, std::size_t elements) {
        if (!rootArrayStarted && frames.empty() && !object) {
            rootArrayStarted = true;
            if (elements != std::numeric_limits<std::size_t>::max())
                cards.reserve(elements);
            return true;
        }

        const bool keep = keepNextValue();
        json value = object ? json::object() : json::array();
        if (keep && elements != std::numeric_limits<std::size_t>::max()) {
            if (object)
                value.get_ref<json::object_t&>().clear();
            else
                value.get_ref<json::array_t&>().reserve(elements);
        }
        frames.push_back({std::move(value), {}, keep, object});
        return true;
    }

    bool endContainer(bool object) {
        if (frames.empty())
            return !object && rootArrayStarted;
        Frame frame = std::move(frames.back());
        frames.pop_back();
        finishValue(std::move(frame.value), frame.keep);
        return true;
    }

public:
    bool null() override { return primitive(nullptr); }
    bool boolean(bool value) override { return primitive(value); }
    bool number_integer(number_integer_t value) override { return primitive(value); }
    bool number_unsigned(number_unsigned_t value) override { return primitive(value); }
    bool number_float(number_float_t value, const string_t&) override { return primitive(value); }
    bool string(string_t& value) override { return primitive(std::move(value)); }
    bool binary(binary_t& value) override { return primitive(std::move(value)); }
    bool start_object(std::size_t elements) override { return startContainer(true, elements); }
    bool key(string_t& value) override {
        frames.back().key = std::move(value);
        return true;
    }
    bool end_object() override { return endContainer(true); }
    bool start_array(std::size_t elements) override { return startContainer(false, elements); }
    bool end_array() override { return endContainer(false); }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }

    std::vector<Card> takeCards() { return std::move(cards); }
    bool hasRootArray() const { return rootArrayStarted; }
    void rethrowConversionError() const {
        if (conversionError != nullptr)
            std::rethrow_exception(conversionError);
    }
};

const std::unordered_set<std::string> CardJsonSax::keptKeys = {
    "id", "seq", "characterId", "cardRarityType",
    "specialTrainingPower1BonusFixed", "specialTrainingPower2BonusFixed",
    "specialTrainingPower3BonusFixed", "attr", "supportUnit", "skillId",
    "specialTrainingSkillId", "cardParameters", "cardLevel",
    "cardParameterType", "power", "param1", "param2", "param3",
};

std::vector<Card> loadCardsFromString(const std::string& input) {
    CardJsonSax sax;
    if (!json::sax_parse(input, &sax))
        return Card::fromJsonList(json::parse(input));

    if (!sax.hasRootArray())
        return Card::fromJsonList(json::parse(input));
    sax.rethrowConversionError();
    return sax.takeCards();
}

}

MasterData::MasterData()
    : MasterData(std::make_shared<MasterDataCore>()) {}

MasterData::MasterData(std::shared_ptr<MasterDataCore> core)
    : storage(std::move(core)),
      areaItemLevels(storage->areaItemLevels),
      areaItems(storage->areaItems),
      areas(storage->areas),
      cardEpisodes(storage->cardEpisodes),
      cards(storage->cards),
      cardMysekaiCanvasBonuses(storage->cardMysekaiCanvasBonuses),
      cardRarities(storage->cardRarities),
      characterRanks(storage->characterRanks),
      eventCards(storage->eventCards),
      eventDeckBonuses(storage->eventDeckBonuses),
      eventExchangeSummaries(storage->eventExchangeSummaries),
      events(storage->events),
      eventItems(storage->eventItems),
      eventRarityBonusRates(storage->eventRarityBonusRates),
      gameCharacters(storage->gameCharacters),
      gameCharacterUnits(storage->gameCharacterUnits),
      masterLessons(storage->masterLessons),
      musicDifficulties(storage->musicDifficulties),
      musics(storage->musics),
      musicVocals(storage->musicVocals),
      mysekaiFixtureGameCharacterGroups(storage->mysekaiFixtureGameCharacterGroups),
      mysekaiFixtureGameCharacterGroupPerformanceBonuses(storage->mysekaiFixtureGameCharacterGroupPerformanceBonuses),
      mysekaiGates(storage->mysekaiGates),
      mysekaiGateLevels(storage->mysekaiGateLevels),
      shopItems(storage->shopItems),
      skills(storage->skills),
      worldBloomDifferentAttributeBonuses(storage->worldBloomDifferentAttributeBonuses),
      worldBlooms(storage->worldBlooms),
      worldBloomSupportDeckUnitEventLimitedBonuses(storage->worldBloomSupportDeckUnitEventLimitedBonuses),
      worldBloomSupportDeckBonusesWL1(storage->worldBloomSupportDeckBonusesWL1),
      worldBloomSupportDeckBonusesWL2(storage->worldBloomSupportDeckBonusesWL2),
      worldBloomSupportDeckBonusesWL3(storage->worldBloomSupportDeckBonusesWL3) {}

std::shared_ptr<MasterDataCore> MasterData::sharedCore() const {
    return storage;
}


const std::vector<std::string> requiredMasterDataKeys = {
    "areaItemLevels",
    "areaItems",
    "areas",
    "cardEpisodes",
    "cards",
    "cardRarities",
    "characterRanks",
    "eventCards",
    "eventDeckBonuses",
    "eventExchangeSummaries",
    "events",
    "eventItems",
    "eventRarityBonusRates",
    "gameCharacters",
    "gameCharacterUnits",
    "honors",
    "masterLessons",
    "musicDifficulties",
    "musics",
    "musicVocals",
    "shopItems",
    "skills",
    "worldBloomDifferentAttributeBonuses",
    "worldBlooms"
};
const std::vector<std::string> notRequiredMasterDataKeys = {
    "worldBloomSupportDeckUnitEventLimitedBonuses",
    "cardMysekaiCanvasBonuses",
    "mysekaiFixtureGameCharacterGroups",
    "mysekaiFixtureGameCharacterGroupPerformanceBonuses",
    "mysekaiGates",
    "mysekaiGateLevels"
};


void loadMasterDataJsonFromFile(std::map<std::string, json>& jsons, const std::string& baseDir, const std::string& key) {
    try {
        std::string filePath = baseDir + "/" + key + ".json";
        std::ifstream file(filePath);
        if (!file.is_open()) {
            jsons.erase(key);
            return;
        }
        json j;
        file >> j;
        file.close();
        jsons[key] = j;
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Failed to load master data from file: " + key + ", error: " + e.what());
    }
}

void addFinalChapterEventIfNeeded(MasterData& md) {
    bool hasFinalChapter = false;
    for (const auto& e : md.events) {
        if (e.id == finalChapterEventId) {
            hasFinalChapter = true;
            break;
        }
    }
    if (!hasFinalChapter) {
        // 活动本身
        Event event;
        event.id = finalChapterEventId;
        event.eventType = Enums::EventType::world_bloom;
        md.events.push_back(event);

        // 角色加成
        for (auto& gameCharacterUnit : md.gameCharacterUnits) {
            EventDeckBonus bonus;
            bonus.eventId = finalChapterEventId;
            bonus.gameCharacterUnitId = gameCharacterUnit.id;
            bonus.bonusRate = 5.0;
            bonus.cardAttr = Enums::Attr::null;
            md.eventDeckBonuses.push_back(bonus);
        }

        // wl2限定卡牌加成
        const std::set<int> worldBloomEventIds = { 163, 167, 170, 171, 176, 179 };
        std::vector<EventCard> newEventCards{};
        for (const auto& eventCard : md.eventCards) {
            if (worldBloomEventIds.count(eventCard.eventId)) {
                auto newEventCard = eventCard;
                newEventCard.eventId = finalChapterEventId;
                newEventCard.bonusRate = 25.0;
                newEventCards.push_back(newEventCard);
            }
        }
        md.eventCards.insert(md.eventCards.end(), newEventCards.begin(), newEventCards.end());

        // 支援里的wl1限定卡牌加成
        std::vector<WorldBloomSupportDeckUnitEventLimitedBonus> newLimitedBonuses{};
        for (const auto& limitedBonus : md.worldBloomSupportDeckUnitEventLimitedBonuses) {
            auto newLimitBonus = limitedBonus;
            newLimitBonus.eventId = finalChapterEventId;
            newLimitedBonuses.push_back(newLimitBonus);
        }
        md.worldBloomSupportDeckUnitEventLimitedBonuses.insert(
            md.worldBloomSupportDeckUnitEventLimitedBonuses.end(),
            newLimitedBonuses.begin(),
            newLimitedBonuses.end()
        );
    }
}


template <typename T>
std::vector<T> loadMasterData(std::map<std::string, json>& jsons, const std::string& key, bool required = true) {
    if (!jsons.count(key)) {
        if (required) {
            throw std::runtime_error("master data key not found: " + key);
        } else {
            std::cerr << "[sekai-deck-recommend-cpp] warning: master data key not found: " + key << std::endl;
            return {};
        }
    }
    return T::fromJsonList(jsons.at(key));
}

template <typename T>
std::vector<T> loadMasterDataFromString(
    std::map<std::string, std::string>& data,
    const std::string& key,
    bool required = true
) {
    auto it = data.find(key);
    if (it == data.end()) {
        if (required)
            throw std::runtime_error("master data key not found: " + key);
        std::cerr << "[sekai-deck-recommend-cpp] warning: master data key not found: " + key << std::endl;
        return {};
    }

    json parsed;
    try {
        parsed = json::parse(it->second);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Failed to load master data from string: " + key + ", error: " + e.what());
    }
    data.erase(it);
    auto result = T::fromJsonList(parsed);
    return result;
}

void MasterData::loadHonorsFromStrings(std::map<std::string, std::string>& data) {
    this->baseDir.clear();
    this->honors = loadMasterDataFromString<Honor>(data, "honors");
}

void MasterData::loadFromJsons(std::map<std::string, json>& jsons) {
    this->areaItemLevels = loadMasterData<AreaItemLevel>(jsons, "areaItemLevels");
    this->areaItems = loadMasterData<AreaItem>(jsons, "areaItems");
    this->areas = loadMasterData<Area>(jsons, "areas");
    this->cardEpisodes = loadMasterData<CardEpisode>(jsons, "cardEpisodes");
    this->cards = loadMasterData<Card>(jsons, "cards");
    this->cardRarities = loadMasterData<CardRarity>(jsons, "cardRarities");
    this->characterRanks = loadMasterData<CharacterRank>(jsons, "characterRanks");
    this->eventCards = loadMasterData<EventCard>(jsons, "eventCards");
    this->eventDeckBonuses = loadMasterData<EventDeckBonus>(jsons, "eventDeckBonuses");
    this->eventExchangeSummaries = loadMasterData<EventExchangeSummary>(jsons, "eventExchangeSummaries");
    this->events = loadMasterData<Event>(jsons, "events");
    this->eventItems = loadMasterData<EventItem>(jsons, "eventItems");
    this->eventRarityBonusRates = loadMasterData<EventRarityBonusRate>(jsons, "eventRarityBonusRates");
    this->gameCharacters = loadMasterData<GameCharacter>(jsons, "gameCharacters");
    this->gameCharacterUnits = loadMasterData<GameCharacterUnit>(jsons, "gameCharacterUnits");
    this->honors = loadMasterData<Honor>(jsons, "honors");
    this->masterLessons = loadMasterData<MasterLesson>(jsons, "masterLessons");
    this->musicDifficulties = loadMasterData<MusicDifficulty>(jsons, "musicDifficulties");
    this->musics = loadMasterData<Music>(jsons, "musics");
    this->musicVocals = loadMasterData<MusicVocal>(jsons, "musicVocals");
    this->shopItems = loadMasterData<ShopItem>(jsons, "shopItems");
    this->skills = loadMasterData<Skill>(jsons, "skills");
    this->worldBloomDifferentAttributeBonuses = loadMasterData<WorldBloomDifferentAttributeBonus>(jsons, "worldBloomDifferentAttributeBonuses");
    this->worldBlooms = loadMasterData<WorldBloom>(jsons, "worldBlooms");

    this->worldBloomSupportDeckUnitEventLimitedBonuses = loadMasterData<WorldBloomSupportDeckUnitEventLimitedBonus>(jsons, "worldBloomSupportDeckUnitEventLimitedBonuses", false);
    this->cardMysekaiCanvasBonuses = loadMasterData<CardMysekaiCanvasBonus>(jsons, "cardMysekaiCanvasBonuses", false);
    this->mysekaiFixtureGameCharacterGroups = loadMasterData<MysekaiFixtureGameCharacterGroup>(jsons, "mysekaiFixtureGameCharacterGroups", false);
    this->mysekaiFixtureGameCharacterGroupPerformanceBonuses = loadMasterData<MysekaiFixtureGameCharacterGroupPerformanceBonus>(jsons, "mysekaiFixtureGameCharacterGroupPerformanceBonuses", false);
    this->mysekaiGates = loadMasterData<MysekaiGate>(jsons, "mysekaiGates", false);
    this->mysekaiGateLevels = loadMasterData<MysekaiGateLevel>(jsons, "mysekaiGateLevels", false);

    finishLoad();
}

void MasterData::finishLoad() {
    std::map<std::string, json> tmp{};
    loadMasterDataJsonFromFile(tmp, getStaticDataDir(), "worldBloomSupportDeckBonusesWL1");
    loadMasterDataJsonFromFile(tmp, getStaticDataDir(), "worldBloomSupportDeckBonusesWL2");
    loadMasterDataJsonFromFile(tmp, getStaticDataDir(), "worldBloomSupportDeckBonusesWL3");
    this->worldBloomSupportDeckBonusesWL1 = loadMasterData<WorldBloomSupportDeckBonus>(tmp, "worldBloomSupportDeckBonusesWL1");
    this->worldBloomSupportDeckBonusesWL2 = loadMasterData<WorldBloomSupportDeckBonus>(tmp, "worldBloomSupportDeckBonusesWL2");
    this->worldBloomSupportDeckBonusesWL3 = loadMasterData<WorldBloomSupportDeckBonus>(tmp, "worldBloomSupportDeckBonusesWL3");

    addFakeEvent(Enums::EventType::world_bloom);
    addFakeEvent(Enums::EventType::marathon);
    addFakeEvent(Enums::EventType::cheerful);
    addFinalChapterEventIfNeeded(*this);
}

void MasterData::loadFromFiles(const std::string& baseDir) {
    this->baseDir = baseDir;
    std::map<std::string, json> jsons;
    for (const auto& key : requiredMasterDataKeys) 
        loadMasterDataJsonFromFile(jsons, baseDir, key);
    for (const auto& key : notRequiredMasterDataKeys) 
        loadMasterDataJsonFromFile(jsons, baseDir, key);
    loadFromJsons(jsons);
}

void MasterData::loadFromStrings(std::map<std::string, std::string>& data) {
    this->baseDir.clear();
    this->areaItemLevels = loadMasterDataFromString<AreaItemLevel>(data, "areaItemLevels");
    this->areaItems = loadMasterDataFromString<AreaItem>(data, "areaItems");
    this->areas = loadMasterDataFromString<Area>(data, "areas");
    this->cardEpisodes = loadMasterDataFromString<CardEpisode>(data, "cardEpisodes");
    auto cardsData = data.find("cards");
    if (cardsData == data.end())
        throw std::runtime_error("master data key not found: cards");
    try {
        this->cards = loadCardsFromString(cardsData->second);
    }
    catch (const json::parse_error& e) {
        throw std::runtime_error("Failed to load master data from string: cards, error: " + std::string(e.what()));
    }
    data.erase(cardsData);
    this->cardRarities = loadMasterDataFromString<CardRarity>(data, "cardRarities");
    this->characterRanks = loadMasterDataFromString<CharacterRank>(data, "characterRanks");
    this->eventCards = loadMasterDataFromString<EventCard>(data, "eventCards");
    this->eventDeckBonuses = loadMasterDataFromString<EventDeckBonus>(data, "eventDeckBonuses");
    this->eventExchangeSummaries = loadMasterDataFromString<EventExchangeSummary>(data, "eventExchangeSummaries");
    this->events = loadMasterDataFromString<Event>(data, "events");
    this->eventItems = loadMasterDataFromString<EventItem>(data, "eventItems");
    this->eventRarityBonusRates = loadMasterDataFromString<EventRarityBonusRate>(data, "eventRarityBonusRates");
    this->gameCharacters = loadMasterDataFromString<GameCharacter>(data, "gameCharacters");
    this->gameCharacterUnits = loadMasterDataFromString<GameCharacterUnit>(data, "gameCharacterUnits");
    this->honors = loadMasterDataFromString<Honor>(data, "honors");
    this->masterLessons = loadMasterDataFromString<MasterLesson>(data, "masterLessons");
    this->musicDifficulties = loadMasterDataFromString<MusicDifficulty>(data, "musicDifficulties");
    this->musics = loadMasterDataFromString<Music>(data, "musics");
    this->musicVocals = loadMasterDataFromString<MusicVocal>(data, "musicVocals");
    this->shopItems = loadMasterDataFromString<ShopItem>(data, "shopItems");
    this->skills = loadMasterDataFromString<Skill>(data, "skills");
    this->worldBloomDifferentAttributeBonuses = loadMasterDataFromString<WorldBloomDifferentAttributeBonus>(data, "worldBloomDifferentAttributeBonuses");
    this->worldBlooms = loadMasterDataFromString<WorldBloom>(data, "worldBlooms");
    this->worldBloomSupportDeckUnitEventLimitedBonuses = loadMasterDataFromString<WorldBloomSupportDeckUnitEventLimitedBonus>(data, "worldBloomSupportDeckUnitEventLimitedBonuses", false);
    this->cardMysekaiCanvasBonuses = loadMasterDataFromString<CardMysekaiCanvasBonus>(data, "cardMysekaiCanvasBonuses", false);
    this->mysekaiFixtureGameCharacterGroups = loadMasterDataFromString<MysekaiFixtureGameCharacterGroup>(data, "mysekaiFixtureGameCharacterGroups", false);
    this->mysekaiFixtureGameCharacterGroupPerformanceBonuses = loadMasterDataFromString<MysekaiFixtureGameCharacterGroupPerformanceBonus>(data, "mysekaiFixtureGameCharacterGroupPerformanceBonuses", false);
    this->mysekaiGates = loadMasterDataFromString<MysekaiGate>(data, "mysekaiGates", false);
    this->mysekaiGateLevels = loadMasterDataFromString<MysekaiGateLevel>(data, "mysekaiGateLevels", false);

    finishLoad();
}


// 添加用于无活动组卡和指定团+颜色组卡的假活动
void MasterData::addFakeEvent(int eventType) {
    if (eventType == Enums::EventType::world_bloom) {
        // 模拟WL组卡
        for (int turn = 1; turn <= 2; turn++) {
            for (auto unit : Enums::Unit::specificUnits) {
                // 活动本身
                Event e;
                e.id = getWorldBloomFakeEventId(turn, unit);
                e.eventType = eventType;
                events.push_back(e);
                std::set<int> charas{};
                // 相同团的角色加成
                for (auto& charaUnit : gameCharacterUnits) {
                    if ((charaUnit.unit == unit && charaUnit.id <= 20) || (unit == Enums::Unit::piapro && charaUnit.id > 20)) {
                        EventDeckBonus b;
                        b.eventId = e.id;
                        b.gameCharacterUnitId = charaUnit.id;
                        b.cardAttr = Enums::Attr::null;
                        b.bonusRate = 25.0;
                        eventDeckBonuses.push_back(b);
                        if (charaUnit.id <= 26)
                            charas.insert(charaUnit.id);
                    }
                }
                // WL章节
                int chapterNo = 0;
                for (auto chara : charas) {
                    WorldBloom wb;
                    wb.eventId = e.id;
                    wb.gameCharacterId = chara;
                    wb.chapterNo = ++chapterNo;
                    worldBlooms.push_back(wb);
                }
                // 如果是WL2，并且已经有WL1的卡，则添加WL1卡的支援加成（从现有的复制）
                if (turn == 2) {
                    std::vector<WorldBloomSupportDeckUnitEventLimitedBonus> newBonuses{};
                    for (const auto& bonus : worldBloomSupportDeckUnitEventLimitedBonuses) {
                        if (bonus.eventId < 180 && charas.count(bonus.gameCharacterId)) {
                            auto newBonus = bonus;
                            newBonus.eventId = e.id;
                            newBonuses.push_back(newBonus);
                        }
                    }
                    worldBloomSupportDeckUnitEventLimitedBonuses.insert(
                        worldBloomSupportDeckUnitEventLimitedBonuses.end(),
                        newBonuses.begin(),
                        newBonuses.end()
                    );
                }
            }
        }

        const std::array<std::vector<int>, 5> wl3Groups = {{
            {21, 1, 6, 14, 17},
            {22, 23, 4, 5, 10, 13},
            {24, 3, 8, 9, 18},
            {26, 2, 12, 16, 20},
            {25, 7, 11, 15, 19},
        }};
        for (int groupIndex = 0; groupIndex < static_cast<int>(wl3Groups.size()); ++groupIndex) {
            const int groupId = groupIndex + 1;
            const int eventId = getWorldBloomFakeEventId(3, groupId);
            const auto& group = wl3Groups[groupIndex];
            const std::set<int> characters(group.begin(), group.end());

            Event event;
            event.id = eventId;
            event.eventType = eventType;
            events.push_back(event);

            for (const auto& characterUnit : gameCharacterUnits) {
                if (!characters.count(characterUnit.gameCharacterId))
                    continue;
                EventDeckBonus bonus;
                bonus.eventId = eventId;
                bonus.gameCharacterUnitId = characterUnit.id;
                bonus.cardAttr = Enums::Attr::null;
                bonus.bonusRate = 25.0;
                eventDeckBonuses.push_back(bonus);
            }

            int chapterNo = 0;
            for (const auto characterId : group) {
                WorldBloom worldBloom;
                worldBloom.eventId = eventId;
                worldBloom.gameCharacterId = characterId;
                worldBloom.chapterNo = ++chapterNo;
                worldBlooms.push_back(worldBloom);
            }

            std::set<int> limitedCardIds;
            for (const auto& eventCard : eventCards) {
                if (eventCard.eventId == finalChapterEventId ||
                    getWorldBloomEventTurn(eventCard.eventId) > 2 ||
                    eventCard.bonusRate <= 0) {
                    continue;
                }
                const auto eventIt = std::find_if(events.begin(), events.end(), [&](const Event& current) {
                    return current.id == eventCard.eventId;
                });
                if (eventIt == events.end() || eventIt->eventType != Enums::EventType::world_bloom)
                    continue;
                const auto cardIt = std::find_if(cards.begin(), cards.end(), [&](const Card& card) {
                    return card.id == eventCard.cardId;
                });
                if (cardIt == cards.end() || !characters.count(cardIt->characterId) ||
                    !limitedCardIds.insert(eventCard.cardId).second) {
                    continue;
                }

                WorldBloomSupportDeckUnitEventLimitedBonus bonus;
                bonus.eventId = eventId;
                bonus.gameCharacterId = cardIt->characterId;
                bonus.cardId = eventCard.cardId;
                bonus.bonusRate = 20.0;
                worldBloomSupportDeckUnitEventLimitedBonuses.push_back(bonus);
            }
        }
    }
    else {
        // 无活动组卡
        Event noEvent;
        noEvent.id = getNoEventFakeEventId(eventType);
        noEvent.eventType = eventType;
        events.push_back(noEvent);

        // 指定团名+指定颜色组卡
        for (auto unit : Enums::Unit::specificUnits) {
            for (auto attr : Enums::Attr::specificAttrs) {
                Event e;
                e.id = getUnitAttrFakeEventId(eventType, unit, attr);
                e.eventType = eventType;
                events.push_back(e);
                // 相同团的角色加成
                for (auto& charaUnit : gameCharacterUnits) {
                    if (charaUnit.unit == unit || (unit == Enums::Unit::piapro && charaUnit.id > 20)) {
                        // 同团同色
                        EventDeckBonus b;
                        b.eventId = e.id;
                        b.gameCharacterUnitId = charaUnit.id;
                        b.cardAttr = attr;
                        b.bonusRate = 50.0;
                        eventDeckBonuses.push_back(b);
                        // 同团不同色
                        EventDeckBonus b2;
                        b2.eventId = e.id;
                        b2.gameCharacterUnitId = charaUnit.id;
                        b2.cardAttr = Enums::Attr::null;
                        b2.bonusRate = 25.0;
                        eventDeckBonuses.push_back(b2);
                    }
                }
                // 不同团同色加成
                EventDeckBonus b;
                b.eventId = e.id;
                b.gameCharacterUnitId = 0;
                b.cardAttr = attr;
                b.bonusRate = 25.0;
                eventDeckBonuses.push_back(b);
            }
        }
    }
}

int MasterData::getNoEventFakeEventId(int eventType) const
{
    if (eventType == Enums::EventType::world_bloom) {
        throw std::invalid_argument("Not supported event type for fake event");
    }
    return 2000000 + eventType * 100000;
}

int MasterData::getUnitAttrFakeEventId(int eventType, int unit, int attr) const
{
    if (eventType == Enums::EventType::world_bloom) {
        throw std::invalid_argument("Not supported event type for fake event");
    }
    return 1000000 + unit * 100 + attr + eventType * 100000;
}

int MasterData::getWorldBloomFakeEventId(int worldBloomTurn, int unitOrGroup) const
{
    if (worldBloomTurn < 1 || worldBloomTurn > 3) {
        throw std::invalid_argument("Invalid world bloom turn: " + std::to_string(worldBloomTurn));
    }
    if (worldBloomTurn == 3 && (unitOrGroup < 1 || unitOrGroup > 5)) {
        throw std::invalid_argument("Invalid world bloom group: " + std::to_string(unitOrGroup));
    }
    return 3000000 + (worldBloomTurn - 1) * 100000 + unitOrGroup;
}

int MasterData::getWorldBloomEventTurn(int eventId) const
{
    if (eventId > 1000) 
        return (eventId / 100000) % 10 + 1;
    if (eventId <= 140)
        return 1;
    if (eventId <= 180)
        return 2;
    return 3;
}
