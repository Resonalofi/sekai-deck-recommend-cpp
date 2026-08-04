#ifndef CARD_DETAIL_MAP_H
#define CARD_DETAIL_MAP_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "common/collection-utils.h"

constexpr int UNIT_MAX = 12;
constexpr int UNIT_MEMBER_MAX = 6;
constexpr int ATTR_MEMBER_MAX = 2;

/**
 * 用于记录在不同的同组合、同属性加成的情况下的综合力或加分技能
 */
template <typename T, std::size_t Capacity>
class CardDetailMap {
    static_assert(Capacity <= std::numeric_limits<uint8_t>::max());

    std::array<uint8_t, UNIT_MAX * UNIT_MEMBER_MAX * ATTR_MEMBER_MAX> indexes{};
    std::array<T, Capacity> values{};
    uint8_t size = 0;

    inline int getKey(int unit, int unitMember, int attrMember) const {
        assert(unit >= 0 && unit < UNIT_MAX);
        assert(unitMember >= 0 && unitMember < UNIT_MEMBER_MAX);
        assert(attrMember == 1 || attrMember == 5);
        return (unit * UNIT_MEMBER_MAX + unitMember) * ATTR_MEMBER_MAX + (attrMember == 5 ? 1 : 0);
    }

public:
    int min = std::numeric_limits<int>::max();
    int max = std::numeric_limits<int>::min();
    
    /**
     * 设定给定情况下的值
     * 为了减少内存消耗，人数并非在所有情况下均为实际值，可能会用1代表混组或无影响
     * @param unit 该map的类别; 组合不影响实际值的情况:any 组分:对应组合(vs可能有2种) vsbf花前:diff ocbf花前:ref 
     * @param unitMember 组合对应的人数（组分技能代表相同组数1-5; vsbf花前代表为不同组数0-2; 其他情况5为同组1为混组或无影响）
     * @param attrMember 卡牌属性对应的人数（5人为同色、1人为混色或无影响）
     * @param cmpValue 设置最小值、最大值的用于剪枝的可比较值
     * @param value 实际值
     */
    inline void set(int unit, int unitMember, int attrMember, int cmpValue, const T& value) {
        this->min = std::min(this->min, cmpValue);
        this->max = std::max(this->max, cmpValue);
        auto key = getKey(unit, unitMember, attrMember);
        auto index = indexes[key];
        if (index == 0) {
            if (size == Capacity)
                throw std::length_error("card detail map capacity exceeded");
            index = ++size;
            indexes[key] = index;
        }
        values[index - 1] = value;
    }

    /**
     * 将查询时的回退规则预解析到索引表中。
     * 必须在全部 set 完成后调用。
     */
    inline void finalize() {
        const auto exactIndexes = indexes;
        for (int unit = 0; unit < UNIT_MAX; ++unit) {
            for (int unitMember = 0; unitMember < UNIT_MEMBER_MAX; ++unitMember) {
                for (int attrMember : {1, 5}) {
                    uint8_t index = 0;

                    if (unit == Enums::Unit::diff)
                        index = exactIndexes[getKey(Enums::Unit::diff, std::min(2, unitMember), 1)];

                    if (index == 0 && unit == Enums::Unit::ref)
                        index = exactIndexes[getKey(Enums::Unit::ref, 1, 1)];

                    if (index == 0)
                        index = exactIndexes[getKey(unit, unitMember, attrMember)];

                    if (index == 0)
                        index = exactIndexes[getKey(unit, unitMember == 5 ? 5 : 1, attrMember)];

                    if (index == 0)
                        index = exactIndexes[getKey(Enums::Unit::any, 1, 1)];

                    indexes[getKey(unit, unitMember, attrMember)] = index;
                }
            }
        }
    }

    /**
     * 获取给定情况下的值
     * 会返回最合适的值，如果给定的条件与卡牌完全不符会给出异常
     * @param unit 组合
     * @param unitMember 该组合对应的人数（真实值）
     * @param attrMember 卡牌属性对应的人数（真实值）
     */
    inline T get(int unit, int unitMember, int attrMember) const {
        attrMember = (attrMember == 5 ? 5 : 1);
        auto index = indexes[getKey(unit, unitMember, attrMember)];
        if (index == 0)
            throw std::runtime_error("case not found");
        return values[index - 1];
    }

    /**
     * 是否肯定比另一个范围小
     * 如果几个维度都比其他小，这张卡可以在自动组卡时舍去
     * @param another 另一个范围
     */
    inline bool isCertainlyLessThan(const CardDetailMap& another) const {
        return this->max < another.min;
    }
};

#endif // CARD_DETAIL_MAP_H
