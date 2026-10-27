/* Shared, deterministic skirmish skill profiles. No engine state or stat handicaps.
 * Levels are stable save/network values: Easy=0, Normal=1, Hard=2, Expert=3.
 * All time budgets are simulation ticks (15 ticks/second), never wall time.
 */
#ifndef OPENCNC3D_FAIR_AI_H
#define OPENCNC3D_FAIR_AI_H

#include <stdint.h>

namespace fair_ai {
enum { Easy = 0, Normal = 1, Hard = 2, Expert = 3, LevelCount = 4, Seats = 8 };
struct Profile {
    int think_ticks;
    int tactical_ticks;
    int scout_ticks;
    int attack_ticks;
    int squad_min;
    int reserve_percent;
    int cash_reserve;
    int scouts;
    int counter_weight;
    int retreat_percent;
    int max_orders;
    int expansion_refineries;
    int attack_groups;
};
inline int Clamp(int level) { return level < Easy ? Easy : level > Expert ? Expert : level; }
inline Profile const& Get(int level)
{
    static Profile const profiles[LevelCount] = {
        {45, 30, 450, 2700, 4, 35, 600, 1, 25, 25, 3, 2, 1},
        {30, 15, 300, 1800, 6, 25, 500, 1, 60, 30, 6, 3, 1},
        {15, 10, 180, 1200, 8, 20, 500, 2, 85, 35, 10, 4, 2},
        {10, 6, 120, 900, 10, 20, 500, 2, 100, 40, 14, 4, 3}
    };
    return profiles[Clamp(level)];
}
inline char const* Name(int level)
{
    static char const* const names[LevelCount] = {"Easy", "Normal", "Hard", "Expert"};
    return names[Clamp(level)];
}
/* A separate stream prevents planning from consuming combat/resource randomness. */
inline uint32_t Random(uint32_t& state, uint32_t limit)
{
    if (!state) state = 0x6d2b79f5u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return limit ? state % limit : 0;
}
inline uint32_t HashWord(uint32_t hash, uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        hash = (hash ^ (value & 255u)) * 16777619u;
        value >>= 8;
    }
    return hash;
}
/* Change the revision when controller semantics change, even if ABI does not. */
inline uint32_t PolicyHash()
{
    uint32_t hash = HashWord(2166136261u, 0x20261019u);
    for (int level = 0; level < LevelCount; ++level) {
        Profile const& p = Get(level);
        const int values[] = {p.think_ticks,p.tactical_ticks,p.scout_ticks,p.attack_ticks,
            p.squad_min,p.reserve_percent,p.cash_reserve,p.scouts,p.counter_weight,
            p.retreat_percent,p.max_orders,p.expansion_refineries,p.attack_groups};
        for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
            hash = HashWord(hash, static_cast<uint32_t>(values[i]));
    }
    return hash;
}
}
#endif
