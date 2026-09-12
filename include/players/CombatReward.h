#ifndef COMBAT_REWARD_H
#define COMBAT_REWARD_H
#include <algorithm>
#include <cstdint>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
namespace CombatReward {
struct Totals {
    int64_t damageMilli = 0, killBonusMilli = 0, conversionMilli = 0, hpRemovedMilli = 0;
    uint64_t hits = 0, kills = 0;
    int64_t total() const { return damageMilli + killBonusMilli + conversionMilli; }
    void save(OutputStream& stream) const {
        stream.writeSint64(damageMilli); stream.writeSint64(killBonusMilli); stream.writeSint64(conversionMilli);
        stream.writeSint64(hpRemovedMilli); stream.writeUint64(hits); stream.writeUint64(kills);
    }
    void load(InputStream& stream) {
        damageMilli=stream.readSint64(); killBonusMilli=stream.readSint64(); conversionMilli=stream.readSint64();
        hpRemovedMilli=stream.readSint64(); hits=stream.readUint64(); kills=stream.readUint64();
    }
};
// HP and credit amounts use thousandths. Only actual enemy HP removed earns
// value; repeated damage callbacks on a dead object earn nothing.
inline Totals hit(int price, int64_t maxHpMilli, int64_t beforeHpMilli,
                  int64_t afterHpMilli, bool hostile, bool unitTarget) {
    Totals result;
    if (!hostile || price <= 0 || maxHpMilli <= 0 || beforeHpMilli <= 0) return result;
    const auto removed = std::max<int64_t>(0, std::min(beforeHpMilli,maxHpMilli)
        - std::max<int64_t>(0,afterHpMilli));
    if (removed == 0) return result;
    result.damageMilli = int64_t(price)*1000*removed/maxHpMilli;
    result.hpRemovedMilli = removed;
    result.hits = 1;
    if (unitTarget && afterHpMilli <= 0) {
        result.killBonusMilli = int64_t(price)*200;
        result.kills = 1;
    }
    return result;
}
}
#endif
