/* ====================================================================================
 *  gate_rminf.c -- WHERE A REMASTERED INFANTRY STRIP THINKS THE GROUND IS.
 *
 *  BUG-20260904-52C9E6: the remastered sprites are misaligned on their ATTACKING poses
 *  and appear to jump upward -- minigunner prone attack, flamethrower standing attack,
 *  bazooka standing attack, bazooka prone attack, commando prone attack.
 *
 *  THE CAUSE, and it is arithmetic rather than art. Every Remastered frame is a tight
 *  crop of a logical canvas that is constant per type, and remaster_inf.h builds one
 *  strip per ACTION, cropped to that action's own union of crops. dosinf_draw_sprite then
 *  stands the strip's bottom row on the terrain. So the logical row that lands on the
 *  ground is a DIFFERENT row for every action -- and an attack pose's crop reaches far
 *  below the man's feet, because the muzzle flash, the flame jet and the rocket exhaust
 *  are drawn INTO the frame. Standing that row on the ground lifts the man by the
 *  overhang. The five poses in the report are the five with the largest overhang.
 *
 *  THE FIX is one anchor per type, read off its STAND action, with every strip carrying
 *  its offset from that anchor. This binary proves the derivation against the player's
 *  OWN INSTALL: no window, no GL, no game. It re-reads the .meta files the strips are
 *  built from and checks that ri_type_anchor and ri_build_strip agree with them, and that
 *  the five reported poses carry the overhangs that were measured by hand.
 *
 *  IT IS ITS OWN BINARY because the numbers live in art the tree does not ship and must
 *  not: the archives are the player's, opened read-only, and nothing here is copied out.
 *
 *  EXIT CODES: 0 all checks passed. 1 a check failed. 77 no Remastered Collection on this
 *  machine, which is not a failure and which the gate reports as a skip.
 * ==================================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "remaster.h"
#include "remaster_tex.h"
#include "remaster_inf.h"
#include "dosinf_dotable.h"

/* THE ACTION SLOTS, by position in DI_SLOT_NAME rather than by including dosinf_mod.h,
   which would drag GL in for four integers. The names are asserted against that table at
   startup, so a slot that moves is a failure here and not a silently wrong measurement. */
enum { DA_STAND = 0, DA_WALK = 1, DA_FIRE = 7, DA_FPRONE = 9 };

static int fails = 0, checks = 0;

static void ck(int cond, const char* fmt, ...)
{
    va_list ap;
    checks++;
    if (cond) return;
    fails++;
    fputs("FAIL ", stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

/* THE HAND MEASUREMENT the fix was derived from, in logical rows below the type's own
   STAND crop bottom. Read off a real install with a throwaway reader that
   parsed nothing but the .meta files, so it is independent of every function under test.
   These are the five poses in the report plus the two that bracket them: E5 was NOT
   reported and has the smallest overhangs of the six types, and E1's standing fire was
   not reported either -- so a fix that moved everything by a constant would fail here. */
struct Known { const char* type; int slot; int drop; };
static const struct Known KNOWN[] = {
    { "E1",   DA_FPRONE,  39 },   /* reported: minigunner prone attack   */
    { "E3",   DA_FIRE,    28 },   /* reported: bazooka standing attack   */
    { "E3",   DA_FPRONE,  41 },   /* reported: bazooka prone attack      */
    { "E4",   DA_FIRE,    47 },   /* reported: flamethrower standing     */
    { "RMBO", DA_FPRONE,  49 },   /* reported: commando prone attack     */
    { "E1",   DA_FIRE,     4 },   /* NOT reported, and only 4 rows       */
    { "E5",   DA_FIRE,    13 },   /* NOT reported, and only 13 rows      */
};
#define NKNOWN ((int)(sizeof KNOWN / sizeof KNOWN[0]))

static const char* const TYPES[] = { "E1", "E2", "E3", "E4", "E5", "E6", "RMBO" };
#define NTYPES ((int)(sizeof TYPES / sizeof TYPES[0]))

int main(void)
{
    char dir[RM_PATH_MAX], data[RM_PATH_MAX], real[256], path[RM_PATH_MAX];
    RtMeg meg, cfg;
    RiBand band;
    int t, k, built = 0;

    ck(!strcmp(DI_SLOT_NAME[DA_STAND],  "STAND"),  "slot %d is %s, not STAND",  DA_STAND,  DI_SLOT_NAME[DA_STAND]);
    ck(!strcmp(DI_SLOT_NAME[DA_WALK],   "WALK"),   "slot %d is %s, not WALK",   DA_WALK,   DI_SLOT_NAME[DA_WALK]);
    ck(!strcmp(DI_SLOT_NAME[DA_FIRE],   "FIRE"),   "slot %d is %s, not FIRE",   DA_FIRE,   DI_SLOT_NAME[DA_FIRE]);
    ck(!strcmp(DI_SLOT_NAME[DA_FPRONE], "FPRONE"), "slot %d is %s, not FPRONE", DA_FPRONE, DI_SLOT_NAME[DA_FPRONE]);
    if (fails) { printf("FAILED: the action slots moved\n"); return 1; }

    if (!rm_find_install(dir, sizeof dir) || !rm_data_dir(dir, data, sizeof data)) {
        printf("no Remastered Collection on this machine\n");
        return 77;
    }
    if (!rm_find_entry(data, "CONFIG.MEG", real, sizeof real)) { printf("no CONFIG.MEG\n"); return 77; }
    rm_join(path, sizeof path, data, real);
    if (!rt_meg_open(&cfg, path)) { printf("CONFIG.MEG unreadable\n"); return 77; }
    if (!ri_read_band(&cfg, &band)) { printf("no colour bounds\n"); rt_meg_close(&cfg); return 77; }
    if (!rm_find_entry(data, "TEXTURES_TD_SRGB.MEG", real, sizeof real)) { printf("no texture MEG\n"); return 77; }
    rm_join(path, sizeof path, data, real);
    if (!rt_meg_open(&meg, path)) { printf("texture MEG unreadable\n"); rt_meg_close(&cfg); return 77; }

    for (t = 0; t < NTYPES; t++) {
        const char* ty = TYPES[t];
        const short (*rows)[3] = di_do_rows(ty);
        char zn[256], low[32];
        unsigned char* blob;
        unsigned int len = 0;
        RiZip z;
        int ax = 0, ay = 0, sbox[4], a;
        char* q;

        ck(rows != NULL, "%s: no DO rows in dosinf_dotable.h", ty);
        if (!rows) continue;
        snprintf(low, sizeof low, "%s", ty);
        for (q = low; *q; q++) *q = (char)tolower((unsigned char)*q);
        snprintf(zn, sizeof zn,
                 "DATA\\ART\\TEXTURES\\SRGB\\TIBERIAN_DAWN\\UNITS\\%s.ZIP", ty);
        blob = rt_meg_read(&meg, rt_meg_find(&meg, zn), &len);
        ck(blob != NULL, "%s: not in the archive", ty);
        if (!blob) continue;
        if (!ri_zip_open(&z, blob, len)) { ck(0, "%s: not a readable zip", ty); free(blob); continue; }

        ck(ri_type_anchor(&z, low, rows[DA_STAND], &ax, &ay) != 0,
           "%s: STAND has no box, so the type has no anchor to hang its other actions on", ty);
        ck(ri_action_box(&z, low, rows[DA_STAND], sbox) != 0, "%s: STAND box missing", ty);
        ck(ay == sbox[3], "%s: anchor row %d is not the STAND crop bottom %d", ty, ay, sbox[3]);
        ck(ax == (sbox[0] + sbox[2]) / 2,
           "%s: anchor column %d is not the STAND crop centre %d", ty, ax, (sbox[0] + sbox[2]) / 2);
        printf("  %-4s anchor %d,%d\n", ty, ax, ay);

        /* EVERY ACTION, against the anchor, straight off the .meta files. */
        for (a = 0; a < DI_SLOT_COUNT; a++) {
            int b[4];
            if (!ri_action_box(&z, low, rows[a], b)) continue;
            for (k = 0; k < NKNOWN; k++)
                if (!strcmp(KNOWN[k].type, ty) && KNOWN[k].slot == a)
                    ck(b[3] - ay == KNOWN[k].drop,
                       "%s %s: crop bottom is %d rows below the stand line, hand-measured %d",
                       ty, DI_SLOT_NAME[a], b[3] - ay, KNOWN[k].drop);
        }

        /* THE STRIP ITSELF, for the reported poses and for STAND. This is the function the
           game calls, with the same band and the same anchor, so a strip that disagrees
           with the boxes above is the bug coming back. STAND must come out at zero on both
           axes or the anchor is not the anchor. */
        {
            static const int SLOTS[] = { DA_STAND, DA_WALK, DA_FIRE, DA_FPRONE };
            int si;
            for (si = 0; si < 4; si++) {
                const int a2 = SLOTS[si];
                int b[4];
                RiSheet sh;
                unsigned char ramp[16][3];
                memset(ramp, 0, sizeof ramp);
                if (!ri_action_box(&z, low, rows[a2], b)) continue;
                if (!ri_build_strip(&z, low, rows[a2], 24, &band, ramp, 1.0f, ax, ay, &sh)) {
                    ck(0, "%s %s: has a box but built no strip", ty, DI_SLOT_NAME[a2]);
                    continue;
                }
                built++;
                ck(sh.drop == (float)(b[3] - ay),
                   "%s %s: strip carries drop %.0f, the art says %d",
                   ty, DI_SLOT_NAME[a2], sh.drop, b[3] - ay);
                ck(sh.ox == (float)((b[0] + b[2]) / 2 - ax),
                   "%s %s: strip carries ox %.0f, the art says %d",
                   ty, DI_SLOT_NAME[a2], sh.ox, (b[0] + b[2]) / 2 - ax);
                ck(sh.fh == b[3] - b[1],
                   "%s %s: cell height %d is not the box height %d -- the placement was "
                   "supposed to move the card, not resize it",
                   ty, DI_SLOT_NAME[a2], sh.fh, b[3] - b[1]);
                if (a2 == DA_STAND) {
                    ck(sh.drop == 0.0f, "%s STAND: drop %.0f, want 0 -- the anchor IS the "
                       "stand line, so the standing man must not move at all", ty, sh.drop);
                    ck(sh.ox == 0.0f, "%s STAND: ox %.0f, want 0", ty, sh.ox);
                }
                ri_sheet_free(&sh);
            }
        }
        ri_zip_close(&z);
        free(blob);
    }
    rt_meg_close(&meg);
    rt_meg_close(&cfg);

    ck(built >= 20, "only %d strips were built; the checks above cover almost nothing", built);
    printf("%s: %d checks, %d strips built, %d failed\n",
           fails ? "FAILED" : "ok", checks, built, fails);
    return fails ? 1 : 0;
}
