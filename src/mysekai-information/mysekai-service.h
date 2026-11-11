#ifndef MYSEKAI_SERVICE_H
#define MYSEKAI_SERVICE_H

#include "data-provider/data-provider.h"
#include <set>


struct MysekaiGateBonus {
    int unit;
    int level;
    double powerBonusRate;
};


class MySekaiService {

    DataProvider dataProvider;

public:

    MySekaiService(DataProvider dataProvider) : dataProvider(dataProvider) {}

    /**
     * 获得带有自定义世界画布加成的卡牌
     * 看上去一个卡牌只有加成和不加成两种状态，直接返回卡牌ID列表
     * 根据稀有度增加三维基础综合力，区域道具、角色等级、家具及大门都以此基础计算。
     */
    std::unordered_set<int> getMysekaiCanvasBonusCards();

    /**
     * 获得自定义世界的家具加成
     * 很贴心地已经由服务器算好了，直接返回就行
     * 计算逻辑：totalBonusRate的单位看上去是0.1%
     */
    std::vector<UserMysekaiFixtureGameCharacterPerformanceBonus> getMysekaiFixtureBonuses();

    /**
     * 获得自定义世界的大门加成
     * 原创角色看组合，V家有应援组合时看应援组合，否则取等级最高的大门。
     */
    std::vector<MysekaiGateBonus> getMysekaiGateBonuses();

};


#endif  // MYSEKAI_SERVICE_H
