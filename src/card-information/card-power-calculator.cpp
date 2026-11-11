#include "card-information/card-power-calculator.h"

CardPowerDetailMap CardPowerCalculator::getCardPower(
    const UserCard &userCard, 
    const Card &card, 
    const std::vector<int> &cardUnits, 
    const std::vector<AreaItemLevel> &userAreaItemLevels, 
    bool hasCanvasBonus, 
    const std::vector<MysekaiGateBonus> &userGateBonuses,
    std::optional<int> fixtureBonusLimit,
    std::vector<DeckCardPowerDetail>* multiUnitPower
)
{
    auto ret = CardPowerDetailMap();
    BasePower basePower = getBasePower(userCard, card, hasCanvasBonus);
    int characterBonus = getCharacterBonusPower(basePower, card.characterId);
    int fixtureBonus = getFixtureBonusPower(basePower, card.characterId, fixtureBonusLimit);
    int gateBonus = getGateBonusPower(basePower, userGateBonuses, cardUnits);
    for (auto unit : cardUnits) {
        // 同组合、同属性
        DeckCardPowerDetail power = getPower(card, basePower, characterBonus, fixtureBonus, gateBonus, userAreaItemLevels, unit, true, true);
        ret.set(unit, 5, 5, power.total, power);
        // 同组合、混属性
        power = getPower(card, basePower, characterBonus, fixtureBonus, gateBonus, userAreaItemLevels, unit, true, false);
        ret.set(unit, 5, 1, power.total, power);
        // 混组合、同属性
        power = getPower(card, basePower, characterBonus, fixtureBonus, gateBonus, userAreaItemLevels, unit, false, true);
        ret.set(unit, 1, 5, power.total, power);
        // 混组合、混属性
        power = getPower(card, basePower, characterBonus, fixtureBonus, gateBonus, userAreaItemLevels, unit, false, false);
        ret.set(unit, 1, 1, power.total, power);
    }
    bool hasMultiUnitEffect = multiUnitPower != nullptr && std::any_of(userAreaItemLevels.begin(), userAreaItemLevels.end(), [](const AreaItemLevel& it) {
        return it.targetUnit == Enums::Unit::multi_unit;
    });
    if (hasMultiUnitEffect) {
        // getCardUnits: [支援组合(可选), 角色组合]
        int characterUnit = cardUnits.back();
        int supportUnit = cardUnits.size() > 1 ? cardUnits.front() : Enums::Unit::none;
        int base = sumPower(basePower);
        multiUnitPower->assign(8, DeckCardPowerDetail{});
        for (int mask = 0; mask < 8; ++mask) {
            bool characterUnitAllMatch = mask & 4;
            bool supportUnitAllMatch = mask & 2;
            bool attrAllMatch = mask & 1;
            int areaItemBonus = getMultiUnitAreaItemBonusPower(
                userAreaItemLevels, basePower, card, characterUnit, supportUnit,
                characterUnitAllMatch, supportUnitAllMatch, attrAllMatch
            );
            auto& power = (*multiUnitPower)[mask];
            power = DeckCardPowerDetail{
                base,
                areaItemBonus,
                characterBonus,
                fixtureBonus,
                gateBonus,
                base + areaItemBonus + characterBonus + fixtureBonus + gateBonus
            };
            // 搜索和卡牌强弱比较的范围必须包含多组合编成。
            ret.max = std::max(ret.max, power.total);
            ret.min = std::min(ret.min, power.total);
        }
    }
    ret.finalize();
    return ret;
}

DeckCardPowerDetail CardPowerCalculator::getPower(const Card &card, const BasePower &basePower, int characterBonus, int fixtureBonus, int gateBonus, const std::vector<AreaItemLevel> &userAreaItemLevels, int unit, bool sameUnit, bool sameAttr)
{
    int base = sumPower(basePower);
    int areaItemBonus = getAreaItemBonusPower(userAreaItemLevels, basePower, card.characterId, unit, sameUnit, card.attr, sameAttr);
    int total = base + areaItemBonus + characterBonus + fixtureBonus + gateBonus;
    return DeckCardPowerDetail{
        base,
        areaItemBonus,
        characterBonus,
        fixtureBonus,
        gateBonus,
        total
    };
}

BasePower CardPowerCalculator::getBasePower(const UserCard &userCard, const Card &card, bool hasMysekaiCanvas)
{
    auto& cardEpisodes = dataProvider.masterData->cardEpisodes;
    auto& masterLessons = dataProvider.masterData->masterLessons;

    BasePower ret = {0, 0, 0};
    // 等级
    if (userCard.level > 0 && userCard.level <= static_cast<int>(card.levelPowers.size()))
        ret = card.levelPowers[userCard.level - 1];
    // 觉醒
    if (userCard.specialTrainingStatus == Enums::SpecialTrainingStatus::done) {
        ret[0] += card.specialTrainingPower1BonusFixed;
        ret[1] += card.specialTrainingPower2BonusFixed;
        ret[2] += card.specialTrainingPower3BonusFixed;
    }
    // 剧情
    for (auto& it : userCard.episodes) {
        if (it.scenarioStatus == Enums::ScenarioStatus::already_read) {
            const auto& episode = findOrThrow(cardEpisodes, [&](auto& e) {
                return e.id == it.cardEpisodeId;
            }, [&]() { return "Card episode not found for cardId=" + std::to_string(card.id) + " episodeId=" + std::to_string(it.cardEpisodeId); });
            ret[0] += episode.power1BonusFixed;
            ret[1] += episode.power2BonusFixed;
            ret[2] += episode.power3BonusFixed;
        }
    }
    // 突破
    for (auto& it : masterLessons) {
        if (it.cardRarityType == card.cardRarityType && it.masterRank <= userCard.masterRank) {
            ret[0] += it.power1BonusFixed;
            ret[1] += it.power2BonusFixed;
            ret[2] += it.power3BonusFixed;
        }
    }
    // 从5.1.0版本开始，画布加成直接算进基础综合力中
    if (hasMysekaiCanvas) {
        auto& cardMysekaiCanvasBonuses = dataProvider.masterData->cardMysekaiCanvasBonuses;
        const auto& canvasBonus = findOrThrow(cardMysekaiCanvasBonuses, [&](auto& it) {
            return it.cardRarityType == card.cardRarityType;
        }, [&]() { return "Card mysekai canvas bonus not found for cardRarityType=" + std::to_string(card.cardRarityType); });
        ret[0] += canvasBonus.power1BonusFixed;
        ret[1] += canvasBonus.power2BonusFixed;
        ret[2] += canvasBonus.power3BonusFixed;
    }
    return ret;
}

int CardPowerCalculator::getAreaItemBonusPower(const std::vector<AreaItemLevel> &userAreaItemLevels, const BasePower &basePower, int characterId, int unit, bool sameUnit, int attr, bool sameAttr)
{
    // GetAreaItemBuffList adds rates within each category first. Its surviving
    // category order is then used by GetAreaItemPowerBy's float32 accumulation.
    std::array<std::array<float, 3>, 4> groups{};
    std::array<bool, 4> present{};
    std::array<int, 4> order{};
    int groupCount = 0;
    for (const auto& it : userAreaItemLevels) {
        int group;
        bool allMatch = false;
        if (it.targetGameCharacterId != 0) {
            if (it.targetGameCharacterId != characterId) continue;
            group = 0;
        } else if (it.targetUnit != Enums::Unit::any) {
            if (it.targetUnit != unit) continue;
            group = 1;
            allMatch = sameUnit;
        } else if (it.targetCardAttr != Enums::Attr::any) {
            if (it.targetCardAttr != attr) continue;
            group = 2;
            allMatch = sameAttr;
        } else {
            group = 3;
        }
        if (!present[group]) {
            present[group] = true;
            order[groupCount++] = group;
        }
        const float rates[3] = {
            float(allMatch ? it.power1AllMatchBonusRate : it.power1BonusRate),
            float(allMatch ? it.power2AllMatchBonusRate : it.power2BonusRate),
            float(allMatch ? it.power3AllMatchBonusRate : it.power3BonusRate),
        };
        for (int i = 0; i < 3; ++i)
            groups[group][i] += rates[i];
    }
    if (dataProvider.region == Region::CN || dataProvider.region == Region::TW || dataProvider.region == Region::KR) {
        // CN/TW/KR 6.4 round each category rate to hundredths (ties to even),
        // sum integer products across all three parameters, then divide once.
        int scaledTotal = 0;
        for (int i = 0; i < 3; ++i)
            for (int index = 0; index < groupCount; ++index)
                scaledTotal += basePower[i] * int(std::nearbyint(groups[order[index]][i] * 100.0f));
        return scaledTotal / 10000;
    }
    int total = 0;
    for (int i = 0; i < 3; ++i) {
        float bonus = 0;
        for (int index = 0; index < groupCount; ++index) {
            const int group = order[index];
            const float rate = groups[group][i] * 0.01f;
            const float addition = rate * float(basePower[i]);
            bonus += addition;
        }
        total += int(std::floor(bonus));
    }
    return total;
}

int CardPowerCalculator::getMultiUnitAreaItemBonusPower(
    const std::vector<AreaItemLevel> &userAreaItemLevels,
    const BasePower &basePower,
    const Card &card,
    int characterUnit,
    int supportUnit,
    bool characterUnitAllMatch,
    bool supportUnitAllMatch,
    bool attrAllMatch
)
{
    // JP 7.1 CardUtility.GetAreaItemBuffList 启用多组合加成时：
    // 每行效果归入一个桶（角色/多组合/组合/支援组合/属性/全体），
    // 组合与支援组合同时存在时去掉三维比率和较小的一个（相等保留组合），
    // 之后若有多组合加成：剩下的组合桶的全员匹配额外加成>=多组合加成则去掉多组合加成，
    // 否则该组合桶回退为普通比率。最终按存活桶顺序做float32计算并逐维取整。
    enum Bucket : int { None = 0, Character, Multi, UnitBucket, SupportBucket, AttrBucket, AnyBucket, BucketCount };
    struct RowPick {
        Bucket bucket;
        bool allMatch;
    };
    std::array<std::array<float, 3>, BucketCount> buff{};
    std::array<std::array<float, 3>, BucketCount> baseBuff{};
    std::array<bool, BucketCount> present{};
    std::array<Bucket, BucketCount> order{};
    int groupCount = 0;

    for (size_t i = 0; i < userAreaItemLevels.size(); ++i) {
        const auto& it = userAreaItemLevels[i];
        RowPick pick{None, false};
        if (it.targetGameCharacterId != 0) {
            if (it.targetGameCharacterId == card.characterId)
                pick.bucket = Character;
        } else if (it.targetUnit == Enums::Unit::multi_unit) {
            pick.bucket = Multi;
        } else if (it.targetUnit != Enums::Unit::any) {
            if (it.targetUnit == characterUnit) {
                pick = {UnitBucket, characterUnitAllMatch};
            } else if (supportUnit != Enums::Unit::none && it.targetUnit == supportUnit) {
                pick = {SupportBucket, supportUnitAllMatch};
            }
        } else if (it.targetCardAttr != Enums::Attr::any) {
            if (it.targetCardAttr == card.attr)
                pick = {AttrBucket, attrAllMatch};
        } else {
            pick.bucket = AnyBucket;
        }
        if (pick.bucket == None)
            continue;
        float normal[3] = {float(it.power1BonusRate), float(it.power2BonusRate), float(it.power3BonusRate)};
        float allMatch[3] = {float(it.power1AllMatchBonusRate), float(it.power2AllMatchBonusRate), float(it.power3AllMatchBonusRate)};
        if (!present[pick.bucket]) {
            present[pick.bucket] = true;
            order[groupCount++] = pick.bucket;
        }
        for (int k = 0; k < 3; ++k) {
            buff[pick.bucket][k] += pick.allMatch ? allMatch[k] : normal[k];
            baseBuff[pick.bucket][k] += normal[k];
        }
    }

    auto sum3 = [](const std::array<float, 3>& v) { return (v[0] + v[1]) + v[2]; };
    std::array<bool, BucketCount> dropped{};
    std::array<bool, BucketCount> useBaseRate{};
    if (present[UnitBucket] && present[SupportBucket]) {
        if (sum3(buff[UnitBucket]) < sum3(buff[SupportBucket]))
            dropped[UnitBucket] = true;
        else
            dropped[SupportBucket] = true;
    }
    if (present[Multi]) {
        Bucket k = None;
        if (present[UnitBucket] && !dropped[UnitBucket])
            k = UnitBucket;
        else if (present[SupportBucket] && !dropped[SupportBucket])
            k = SupportBucket;
        if (k != None) {
            float extra = ((buff[k][0] - baseBuff[k][0]) + (buff[k][1] - baseBuff[k][1])) + (buff[k][2] - baseBuff[k][2]);
            if (extra >= sum3(buff[Multi]))
                dropped[Multi] = true;
            else
                useBaseRate[k] = true;
        }
    }

    // Multi-unit effects are activated by master data, including when they
    // become available in CN/TW/KR; retain each region's rate arithmetic.
    if (dataProvider.region == Region::CN || dataProvider.region == Region::TW || dataProvider.region == Region::KR) {
        int scaledTotal = 0;
        for (int k = 0; k < 3; ++k) {
            for (int index = 0; index < groupCount; ++index) {
                const auto bucket = order[index];
                if (dropped[bucket]) continue;
                const float percent = useBaseRate[bucket] ? baseBuff[bucket][k] : buff[bucket][k];
                scaledTotal += basePower[k] * int(std::nearbyint(percent * 100.0f));
            }
        }
        return scaledTotal / 10000;
    }
    int total = 0;
    for (int k = 0; k < 3; ++k) {
        float bonus = 0;
        for (int index = 0; index < groupCount; ++index) {
            const auto bucket = order[index];
            if (dropped[bucket]) continue;
            const float percent = useBaseRate[bucket] ? baseBuff[bucket][k] : buff[bucket][k];
            const float rate = percent * 0.01f;
            const float addition = rate * float(basePower[k]);
            bonus += addition;
        }
        total += int(std::floor(bonus));
    }
    return total;
}

int CardPowerCalculator::getCharacterBonusPower(const BasePower &basePower, int characterId)
{
    auto& characterRanks = dataProvider.masterData->characterRanks;
    auto& userCharacters = dataProvider.userData->userCharacters;

    const auto& userCharacter = findOrThrow(userCharacters, [&](auto& it) {
        return it.characterId == characterId;
    }, [&]() { return "User character not found for characterId=" + std::to_string(characterId); });
    const auto& characterRank = findOrThrow(characterRanks, [&](auto& it) {
        return it.characterId == userCharacter.characterId &&
               it.characterRank == userCharacter.characterRank;
    }, [&]() { return "Character rank not found for characterId=" + std::to_string(userCharacter.characterId) + " rank=" + std::to_string(userCharacter.characterRank); });
    double rates[3] = {
        characterRank.power1BonusRate,
        characterRank.power2BonusRate,
        characterRank.power3BonusRate
    };
    if (dataProvider.region == Region::CN || dataProvider.region == Region::TW || dataProvider.region == Region::KR) {
        int scaledTotal = 0;
        for (int i = 0; i < 3; ++i)
            scaledTotal += basePower[i] * int(std::nearbyint(float(rates[i]) * 100.0f));
        return scaledTotal / 10000;
    }
    int total = 0;
    for (int i = 0; i < 3; ++i) {
        total += std::floor(float(rates[i]) * float(0.01) * float(basePower[i]));
    }
    return total;
}

int CardPowerCalculator::getFixtureBonusPower(const BasePower &basePower, int characterId, std::optional<int> limit)
{
    auto& userFixtureBonuses = dataProvider.userData->userMysekaiFixtureGameCharacterPerformanceBonuses;
    const auto bonus = std::find_if(userFixtureBonuses.begin(), userFixtureBonuses.end(), [characterId](const auto& row) {
        return row.gameCharacterId == characterId;
    });
    if (bonus == userFixtureBonuses.end())
        return 0;

    const auto& master = *dataProvider.masterData;
    if (master.fixtureBonusLimit)
        limit = limit ? std::min(*limit, *master.fixtureBonusLimit) : master.fixtureBonusLimit;
    // The client returns a zero limit when neither limit table has an entry.
    if (!limit)
        return 0;

    const float percent = float(bonus->totalBonusRate) / master.fixtureBonusRateScale;
    const float fraction = percent / 100.0f;
    const float limitPercent = float(*limit) / master.fixtureBonusRateScale;
    // GetFixtureBonusLimitRate: round to 3 decimals (ties to even), then
    // truncate to 2; its caller converts the resulting float32 percent to a ratio.
    double integral;
    const double remainder = std::modf(double(limitPercent) * 1000.0, &integral);
    if (remainder > 0.5 || (remainder == 0.5 && std::fmod(integral, 2.0) != 0))
        ++integral;
    const float rounded = float(integral / 1000.0);
    const float truncated = float(std::trunc(double(rounded) * 100.0) / 100.0);
    const float capped = std::min(fraction, truncated / 100.0f);
    const float total = capped * float(sumPower(basePower));
    return int(std::floor(total));
}

int CardPowerCalculator::getGateBonusPower(const BasePower &basePower, const std::vector<MysekaiGateBonus> &userGateBonuses, const std::vector<int> &cardUnits)
{
    // getCardUnits 的第一项是应援组合（如有），否则是角色原始组合。
    const int unit = cardUnits.front();
    const MysekaiGateBonus* selected = nullptr;
    for (const auto& bonus : userGateBonuses) {
        if (unit == Enums::Unit::piapro) {
            if (selected == nullptr || bonus.level > selected->level)
                selected = &bonus;
        } else if (bonus.unit == unit) {
            selected = &bonus;
            break;
        }
    }
    const float powerBonusRate = selected == nullptr ? 0.0f : float(selected->powerBonusRate);
    const float fraction = powerBonusRate / 100.0f;
    const float total = fraction * float(sumPower(basePower));
    // GetGateBonus truncates the float32 product to two decimal places before
    // FloorToInt. The decimal conversion itself uses double, as in the IPA.
    const float truncated = float(std::trunc(double(total) * 100.0) / 100.0);
    return int(std::floor(truncated));
}

int CardPowerCalculator::sumPower(const BasePower &power)
{
    return power[0] + power[1] + power[2];
}
