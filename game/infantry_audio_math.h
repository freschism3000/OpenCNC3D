#ifndef OPENCNC3D_INFANTRY_AUDIO_MATH_H
#define OPENCNC3D_INFANTRY_AUDIO_MATH_H
#include "ambience_math.h"
#include <string.h>
enum { INFANTRY_AUDIO_VARIANTS = 10 };
/* THE CROWD LIMIT. Each soldier's step is a 0.45 s clip, two to a gait cycle: right for
   one man, a stampede for a platoon. With nothing to stop it every step played, into a
   64-voice pool: seven men marching on GDI's first mission already kept six steps ringing
   at full level, and every man added more. Three rules, all counted per engine tick so a
   headless run hears the same steps every time:
   - at most INFANTRY_AUDIO_MAX_VOICES steps ring at once;
   - at most INFANTRY_AUDIO_STARTS_PER_TICK new step starts in one tick, the loudest
     first: two feet landing in the same tick are heard as one thud, not a doubled one;
   - past INFANTRY_AUDIO_CROWD_FULL walkers in earshot each step plays quieter, by
     sqrt(full / walkers) down to INFANTRY_AUDIO_CROWD_FLOOR, so a crowd's steps add up to
     about a squad's loudness instead of growing with every man.
   A lone soldier is touched by none of them. */
enum { INFANTRY_AUDIO_MAX_VOICES = 4, INFANTRY_AUDIO_STARTS_PER_TICK = 1,
       INFANTRY_AUDIO_CROWD_FULL = 3 };
static const float INFANTRY_AUDIO_CROWD_FLOOR = 0.5f;
static int infantry_audio_gain(float zoom, float distance, int volume);
static float infantry_audio_zoom_gain(float zoom)
{
    return 0.25f + 0.75f * ambience_clamp((zoom - 0.5f) * 2.0f);
}
/* Keep the step's random volume factor across camera/attenuation retargets. */
static int infantry_audio_gain(float zoom, float distance, int volume)
{
    return (int)(volume * infantry_audio_zoom_gain(zoom) * distance + 0.5f);
}
/* The level each step of a crowd of `walkers` soldiers in earshot plays at, 0..1. */
static float infantry_audio_crowd_gain(int walkers)
{
    if (walkers <= INFANTRY_AUDIO_CROWD_FULL) return 1.0f;
    const float g = sqrtf((float)INFANTRY_AUDIO_CROWD_FULL / (float)walkers);
    return g < INFANTRY_AUDIO_CROWD_FLOOR ? INFANTRY_AUDIO_CROWD_FLOOR : g;
}
/* How many new steps may start this tick while `ringing` steps still sound. */
static int infantry_audio_budget(int ringing)
{
    const int room = INFANTRY_AUDIO_MAX_VOICES - ringing;
    if (room <= 0) return 0;
    return room < INFANTRY_AUDIO_STARTS_PER_TICK ? room : (int)INFANTRY_AUDIO_STARTS_PER_TICK;
}
/* The loudest `budget` of `n` candidate steps, a tie to the lower id: writes their
   indices to `out`, loudest first, and returns how many it wrote. */
static int infantry_audio_pick(const int* gain, const int* id, int n, int budget, int* out)
{
    int k = 0;
    while (k < budget) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            bool taken = false;
            for (int j = 0; j < k; ++j) if (out[j] == i) { taken = true; break; }
            if (taken) continue;
            if (best < 0 || gain[i] > gain[best] || (gain[i] == gain[best] && id[i] < id[best]))
                best = i;
        }
        if (best < 0) break;
        out[k++] = best;
    }
    return k;
}
static bool infantry_audio_is_soldier(const char* type)
{
    return (type[0]=='E' && type[1]>='1' && type[1]<='9' && type[2]==0)
        || !strcmp(type,"E1TR") || !strcmp(type,"RMBO")
        || !strcmp(type,"MEDI") || !strcmp(type,"MECH")
        || !strcmp(type,"SHOK") || !strcmp(type,"SNIPER");
}
/* RED ALERT'S OTHER WALKERS (idata.cpp): the spy, the thief, the general, the dog, the
   ten civilians and the three scientists. There is one set of footstep recordings, the
   boots above, and they are the nearest sound for every one of these; the dog's are
   played quieter and at the mixer's highest pitch (infantry_audio_is_paw), the nearest
   a boot comes to a paw.
   Red Alert only: Tiberian Dawn's civilians share C1..C10's names and walk silently. */
static bool infantry_audio_is_ra_walker(const char* type)
{
    if (type[0]=='C' && type[1]>='1' && type[1]<='9'
        && (type[2]==0 || (type[1]=='1' && type[2]=='0' && type[3]==0))) return true;
    return !strcmp(type,"SPY") || !strcmp(type,"THF") || !strcmp(type,"GNRL")
        || !strcmp(type,"DOG") || !strcmp(type,"EINSTEIN") || !strcmp(type,"DELPHI")
        || !strcmp(type,"CHAN");
}
static bool infantry_audio_is_paw(const char* type) { return !strcmp(type,"DOG"); }
/* Whether this type's steps are heard: the soldiers in both games, the walkers above in
   Red Alert. */
static bool infantry_audio_walks(const char* type, bool redAlert)
{
    return infantry_audio_is_soldier(type) || (redAlert && infantry_audio_is_ra_walker(type));
}
#endif
