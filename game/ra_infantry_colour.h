#ifndef OPENCNC3D_RA_INFANTRY_COLOUR_H
#define OPENCNC3D_RA_INFANTRY_COLOUR_H
#include <string.h>

/* RED ALERT'S SIDE COLOURS for a soldier's uniform: the middle of each PALETTE.CPS ramp
   (the disc's TEMPERAT.PAL, index 6 of the row), in linear light, so a uniform of average
   brightness comes out in the colour the 1996 sprite remaps the same cloth to. Rows as the
   brain's remap= numbers them; France's blue really is that teal. */
static const float RA_SOLDIER_CLOTH[8][3] = {
    { 0.2789f, 0.2462f, 0.0723f },   /* gold        (Spain)           sRGB 144,136,76  */
    { 0.1384f, 0.1746f, 0.3515f },   /* light blue  (Greece, GoodGuy) sRGB 104,116,160 */
    { 0.4342f, 0.0000f, 0.0000f },   /* red         (USSR, BadGuy)    sRGB 176,0,0     */
    { 0.1878f, 0.3140f, 0.1274f },   /* green       (England)         sRGB 120,152,100 */
    { 0.6584f, 0.1878f, 0.0052f },   /* orange      (Ukraine)         sRGB 212,120,16  */
    { 0.2961f, 0.2016f, 0.1620f },   /* grey        (Germany)         sRGB 148,124,112 */
    { 0.0513f, 0.2307f, 0.1746f },   /* blue        (France)          sRGB 64,132,116  */
    { 0.3140f, 0.0723f, 0.0395f } }; /* brown       (Turkey)          sRGB 152,76,56   */
/* THE SAME EIGHT COLOURS AS RED ALERT'S RADAR DRAWS THEM: column 6 of each PALETTE.CPS row,
   in sRGB, in the brain's remap= order (gold, light blue, red, green, orange, grey, blue,
   brown). These are the sRGB values RA_SOLDIER_CLOTH above was made from. */
static const unsigned char RA_RADAR_BAR[8][3] = {
    { 144, 136, 76 }, { 104, 116, 160 }, { 176, 0, 0 },   { 120, 152, 100 },
    { 212, 120, 16 }, { 148, 124, 112 }, { 64, 132, 116 }, { 152, 76, 56 } };
/* Historical normalization retained for every other Red Alert uniform. Allied blue
   uses the current material's own explicit cloth-mask reference, so an updated soldier
   atlas cannot silently inherit another model's brightness. */
static const float RA_SOLDIER_CLOTH_SCALE = 0.082f / 0.161f;

/* Campaign blue infantry use the actual Allied blue ramp, including the French enemy
   soldiers in Soviet missions. This is presentation only: the owner's original remap,
   country and simulation stay intact. Skirmish/network seats keep their chosen colour. */
static int ra_infantry_uniform_remap(int ownerRemap, bool campaign)
{
    return campaign && ownerRemap == 6 ? 1 : ownerRemap;
}

static bool ra_infantry_source_cloth(int ownerRemap, const char* type, bool enhanced,
                                     bool campaign)
{
    return enhanced && ra_infantry_uniform_remap(ownerRemap, campaign) == 1
        && type && strcmp(type, "E6") != 0;
}

/* A valid owner remap is authoritative for both explicit paint masks. It must not go
   through Tiberian Dawn's campaign gold/red or its differently ordered extra colours.
   E6 retains its original uniform: its Red Alert sprite carries no remapped cloth. */
static void ra_infantry_colours(int ownerRemap, const char* type, bool enhanced,
                               bool campaign, float* team, float* cloth, float* amount)
{
    const int remap = ra_infantry_uniform_remap(ownerRemap, campaign);
    if (remap < 0 || remap >= 8) return;
    for (int i = 0; i < 3; ++i) team[i] = RA_SOLDIER_CLOTH[remap][i];
    if (!type || strcmp(type, "E6") == 0) return;
    const float scale = ra_infantry_source_cloth(ownerRemap, type, enhanced, campaign)
                        ? 1.0f : RA_SOLDIER_CLOTH_SCALE;
    for (int i = 0; i < 3; ++i) cloth[i] = RA_SOLDIER_CLOTH[remap][i] * scale;
    *amount = 1.0f;
}

#endif
