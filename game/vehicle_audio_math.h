#ifndef OPENCNC3D_VEHICLE_AUDIO_MATH_H
#define OPENCNC3D_VEHICLE_AUDIO_MATH_H
#include <string.h>
#include "ambience_math.h"
/* Runtime type names from TD udata.cpp. JEEP is the GDI Humvee/Jeep. */
static int vehicle_audio_clip_index(const char* type)
{
    if (!strcmp(type, "MTNK") || !strcmp(type, "HTNK") || !strcmp(type, "FTNK") ||
        !strcmp(type, "MLRS") || !strcmp(type, "MSAM")) return 9;
    if (!strcmp(type, "LTNK") || !strcmp(type, "STNK") || !strcmp(type, "ARTY") || !strcmp(type, "APC")) return 10;
    if (!strcmp(type, "HARV") || !strcmp(type, "JEEP")) return 11;
    return -1;
}
/* RED ALERT'S VEHICLES, by their RULES.INI locomotion and size, onto the three
   recordings above. The types both games share (JEEP, HARV, APC, ARTY) keep Tiberian
   Dawn's answer; MCV is Red Alert only because Tiberian Dawn's makes no sound.
     tank_drive        2TNK 3TNK 4TNK V2RL      the medium, heavy and Mammoth tanks and the
                                                V2 on its tracked chassis
     small_tank_drive  1TNK MNLY MRJ            the light tank and the light tracked
                                                minelayer and radar jammer
     vehicle_drive     TRUK MCV MGG             the wheeled supply truck, MCV and gap
                                                generator */
static int vehicle_audio_clip_index_ra(const char* type)
{
    if (!strcmp(type, "2TNK") || !strcmp(type, "3TNK") || !strcmp(type, "4TNK") ||
        !strcmp(type, "V2RL")) return 9;
    if (!strcmp(type, "1TNK") || !strcmp(type, "MNLY") || !strcmp(type, "MRJ")) return 10;
    if (!strcmp(type, "TRUK") || !strcmp(type, "MCV") || !strcmp(type, "MGG")) return 11;
    return vehicle_audio_clip_index(type);
}
/* Private audio RNG: never advance the simulation's random state. Seeded once by
   the renderer clock; successive driving starts draw fresh whole-frame offsets. */
static long vehicle_audio_random_offset(unsigned& state, long frames)
{
    if (frames <= 0) return 0;
    if (!state) state = 0x6d2b79f5u;
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return (long)(state % (unsigned long)frames);
}
static float vehicle_audio_zoom_gain(float zoom)
{
    return 0.25f + 0.75f * ambience_clamp(zoom);
}
static int vehicle_audio_screen_pan(float col, float width)
{
    if (width <= 0) return 0;
    return (int)((2.0f * ambience_clamp(col / width) - 1.0f) * 1000);
}
#endif
