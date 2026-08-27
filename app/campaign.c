/* ====================================================================================
 * campaign.c -- the 1995 campaign-flow screens. See campaign.h for provenance.
 * ==================================================================================== */

#include "campaign.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_SILENCE_DEPRECATION 1
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "../sidebar/dosbar.h"
#include "../audio/cncaudio.h"
#include "../audio/mixer.h"
#include "../audio/mixfile.h"
#include "../audio/audioboot.h"
/* CMD+F / ALT+ENTER on the briefing, score and map screens too. */
#include "../game/fullscreen.h"

/* THE SCORE SCREEN'S FACTION LOGO BOX, in 320x200 PLATE pixels.

   THE EMBLEM SLOT, which is 1995's slot and not a space we chose. score.cpp:775-776
   draws LOGOS.SHP frame 1 at (0,0) -- 128x96 -- when house == HOUSE_BAD, so Nod's
   score screen has always carried its emblem in the top left and GDI's never did.
   The two background plates still show it: SCORE_G has the gold medal painted into
   the art, SCORE_N leaves the slot bare because the sprite was meant to land there.
   Measured on our own GDI plate, that medal occupies x 8..119, y 8..95, which is the
   same slot to within a few pixels.

   The box below is that slot. It is deliberately NOT the "blank space" this first
   shipped in: the top right band between TOTAL SCORE and CASUALTIES was clear, but
   clear is not where the emblem belongs, and it was settled with a shot of the
   1995 Nod screen -- "not the correct size or placed in the correct location", the
   reference showing the hexagon filling the top left corner. */
#define LOGO_PX   0
#define LOGO_PY   3
#define LOGO_PW 118
#define LOGO_PH  92

#define LOGO_DEG_PER_SEC 24.0f
/* A SWING, not a revolution. These plates are thin, so a full turn passes dead edge-on
   twice a cycle and the emblem all but vanishes. Under 90 by a real margin, because the
   face goes edge-on AT 90 and a swing that just grazed it would flicker. */
#define LOGO_SWING 62.0f

int camp_autopilot = 0;
/* Which side the side-select autopilot picks: 0 = GDI, 1 = Nod. The autopilot used to
   be hardwired to the GDI rectangle, so no hands-off run could ever walk the Nod
   campaign -- and until recently there were no Nod mission packs to walk anyway. */
int camp_autopilot_side = 0;

/* ------------------------------------------------------------------- the clock
 *
 * Every one of these screens is paced by the 1995 Call_Back_Delay(N) (score.cpp:1925),
 * which counts down a CountDownTimerClass. That timer's default base is BT_SYSTEM
 * (timer.h:93) and BT_SYSTEM is the 60 Hz WindowsTimer (timer.cpp:54
 * `WinTimerClass WindowsTimer(60)`; defines.h:2216 "a timer that ticks every 60th of
 * a second"). So one tick is 16.67 ms.
 *
 * The first cut of this file used the GAME LOGIC tick instead (defines.h:2219
 * TICKS_PER_SECOND 15, 66.67 ms), which made all three screens run 3 to 4 times too
 * slowly: CHOOSE.WSA's 30 frames took 6.0 s instead of 1.5 s and read as 5 fps.
 * That is the reported "video is very choppy". */
#define CAMP_TICK_MS(n) (((n) * 1000 + 30) / 60)

/* Advance a WSA frame clock by a fixed step without drifting.
 *
 * `*last += step` rather than `*last = now`: our loop period is the swap interval
 * plus the sleep, so a deadline is always noticed LATE, and resetting the phase to
 * `now` makes every frame cost that overshoot again. Measured on this screen: a 50 ms
 * step reset to `now` ran at 56.2 ms mean and a 1.72 s cycle where 1995's is 1.50 s.
 * Accumulating the phase instead makes the AVERAGE exactly the 1995 step; a frame is
 * simply held for one loop iteration longer when it has to be, which is what the DOS
 * build's own Frame_Limiter does against the vertical blank.
 *
 * If the screen has stalled for more than half a second (a movie decode, a hitch),
 * resynchronise rather than burst through the backlog. */
static int camp_wsa_due(Uint32 now, Uint32 *last, int step)
{
    if (now - *last < (Uint32)step)
        return 0;
    if (now - *last > 500)
        *last = now;
    else
        *last += (Uint32)step;
    return 1;
}

/* Autopilot runs the score and map screens on a SYNTHETIC 60 Hz clock: one tick per
 * loop pass. The full 1995 choreography (a couple of thousand ticks) then runs in a
 * few wall seconds AND identically every run, which is what the flow gates want.
 * Real play uses the real clock. The fake clock advances inside camp_pump, which
 * every screen loop calls exactly once per pass. */
static Uint32 camp_now(const Camp *c)
{
    return camp_autopilot ? c->fakenow : SDL_GetTicks();
}

/* the app links the game's PNG grabber; under autopilot each screen photographs
   itself once so the flow harness leaves pixel evidence */
extern int game_grab_png(const char *path, int w, int h);
static void camp_draw(Camp *c);
/* The faction logo's spin angle, off the screen's own clock. One function, so the
   frame that is DRAWN and the diagnostic that reports it can never disagree. */
static float camp_logo_angle(const Camp *c)
{
    const Uint32 dt = (camp_autopilot ? c->fakenow : SDL_GetTicks()) - c->logo_t0;
    return fmodf((float)dt * (LOGO_DEG_PER_SEC / 1000.0f), 360.0f);
}

static void camp_shot(Camp *c, const char *path, int *once)
{
    int fbw = 0, fbh = 0;
    if (!camp_autopilot || !once || *once)
        return;
    *once = 1;
    camp_draw(c);                 /* the plate as it is RIGHT NOW, un-swapped */
    SDL_GL_GetDrawableSize(c->win, &fbw, &fbh);
    game_grab_png(path, fbw, fbh);
    /* THE LOGO CONTRACT. The logos are thin plates, so a shot taken near 90 or 270
       degrees catches one edge-on and it all but vanishes -- that is the spin
       working, not the logo missing, and in a PNG the two look identical. */
    printf("LOGO|%s|side=%d|ok=%d|angle=%.1f\n",
           path, c->logo_side, c->logo_ok, camp_logo_angle(c));
    fflush(stdout);
}

/* ------------------------------------------------------------------ pack loading */

static int camp_read(FILE *f, void *p, size_t n)
{
    return fread(p, 1, n, f) == n;
}

/* --------------------------------------------------------- LCW and XOR delta
 *
 * The pack ships every WSA as the disc's own frame stream: per frame one LCW
 * ("format80") chunk which decompresses to an XOR delta ("format40") over the
 * previous frame. Both decoders are transcriptions of the GPL originals
 * (common/lcw.cpp LCW_Uncompress, common/xordelta.cpp Apply_XOR_Delta), matching
 * the proven Python ports in menu/tools/mixshp.py opcode for opcode; the bake
 * decodes every frame with those ports and writes a contact sheet, so a divergence
 * here would show as C frames differing from the proven sheet. */

static long camp_lcw(const unsigned char *src, long slen, unsigned char *dst, long dlen)
{
    long sp = 0, dp = 0, cp;
    int op, count, back, val;

    while (dp < dlen && sp < slen) {
        op = src[sp++];
        if (!(op & 0x80)) {
            /* 0xxxyyyy yyyyyyyy : short copy from dest, back y, run x+3 */
            if (sp >= slen)
                break;
            count = (op >> 4) + 3;
            back = src[sp++] + ((op & 0x0F) << 8);
            cp = dp - back;
            if (count > dlen - dp)
                count = (int)(dlen - dp);
            while (count--) {
                dst[dp++] = (cp >= 0) ? dst[cp] : 0;
                cp++;
            }
        } else if (!(op & 0x40)) {
            if (op == 0x80)
                return dp; /* end of data */
            /* 10xxxxxx : copy x bytes straight from source */
            count = op & 0x3F;
            if (count > dlen - dp)
                count = (int)(dlen - dp);
            memcpy(dst + dp, src + sp, (size_t)count);
            dp += count;
            sp += op & 0x3F;
        } else if (op == 0xFE) {
            /* long run of one byte */
            count = src[sp] | (src[sp + 1] << 8);
            val = src[sp + 2];
            sp += 3;
            if (count > dlen - dp)
                count = (int)(dlen - dp);
            memset(dst + dp, val, (size_t)count);
            dp += count;
        } else if (op == 0xFF) {
            /* long copy from dest, absolute offset (may overlap forward) */
            count = src[sp] | (src[sp + 1] << 8);
            cp = src[sp + 2] | (src[sp + 3] << 8);
            sp += 4;
            if (count > dlen - dp)
                count = (int)(dlen - dp);
            while (count--)
                dst[dp++] = dst[cp++];
        } else {
            /* 11xxxxxx : medium copy from dest, absolute offset, x+3 bytes */
            count = (op & 0x3F) + 3;
            cp = src[sp] | (src[sp + 1] << 8);
            sp += 2;
            if (count > dlen - dp)
                count = (int)(dlen - dp);
            while (count--)
                dst[dp++] = dst[cp++];
        }
    }
    return dp;
}

static void camp_xor(unsigned char *dst, long dlen, const unsigned char *src, long slen)
{
    long gp = 0, dp = 0;
    int cmd, count, xorval, value;

    for (;;) {
        if (gp >= slen)
            return;
        cmd = src[gp++];
        count = cmd;
        xorval = 0;
        value = 0;

        if (!(cmd & 0x80)) {
            if (cmd == 0) {
                /* 00000000 : run of a single xor value */
                count = src[gp++];
                value = src[gp++];
                xorval = 1;
            }
            /* else 0??????? : xor the next `count` source bytes */
        } else {
            count &= 0x7F;
            if (count != 0) {
                dp += count; /* 1??????? : skip */
                continue;
            }
            count = src[gp] | (src[gp + 1] << 8);
            gp += 2;
            if (count == 0)
                return; /* end of delta */
            if (!(count & 0x8000)) {
                dp += count; /* long skip */
                continue;
            }
            if (count & 0x4000) {
                count &= 0x3FFF;
                value = src[gp++];
                xorval = 1;
            } else {
                count &= 0x3FFF;
            }
        }

        if (xorval) {
            while (count--) {
                if (dp < dlen)
                    dst[dp] ^= (unsigned char)value;
                dp++;
            }
        } else {
            while (count--) {
                if (dp < dlen)
                    dst[dp] ^= src[gp];
                dp++;
                gp++;
            }
        }
    }
}

/* ------------------------------------------------------- text off the original disc
 *
 * These screens print strings from the 1995 English string table, CONQUER.ENG inside
 * LOCAL.MIX. It is read here rather than typed into the source, because typed text is
 * invented content: the first cut of this screen printed "Select a side. The one you
 * pick is the one you fight for.", which is not in the game, and at 57 characters was
 * 47 px too wide for the 320 px plate at each end (Reported: "text also cuts off").
 *
 * Format (menu/bake_dosmenu.py:288, which reads the same file at bake time): a table
 * of u16 file offsets, one per string, and offsets[0]/2 is the count. Strings are
 * NUL terminated. The indices are the engine's own TXT_ numbers (conquer.h). The
 * whole blob is retained in Camp: the score and map screens read ~20 strings by
 * index at show time. If the file cannot be read the screens print NOTHING and say
 * so: a substitute would be exactly the invention this replaces. */
#define CAMP_TXT_GDI_NAME 660  /* conquer.h:677  "GLOBAL DEFENSE INITIATIVE" */
#define CAMP_TXT_NOD_NAME 661  /* conquer.h:678  "BROTHERHOOD OF NOD"        */
#define CAMP_TXT_SEL_TRANS 662 /* conquer.h:679  "SELECT TRANSMISSION"       */

/* the score screen's strings, conquer.h:597-611 */
#define TXT_SCORE_TIME 580
#define TXT_SCORE_LEAD 581
#define TXT_SCORE_EFFI 582
#define TXT_SCORE_TOTA 583
#define TXT_SCORE_CASU 584
#define TXT_SCORE_NEUT 585
#define TXT_SCORE_GDI 586
#define TXT_SCORE_BUIL 587
#define TXT_SCORE_TOP 590         /* conquer.h:607 "TOP SCORES" */
#define TXT_SCORE_ENDCRED 591
#define TXT_SCORE_TIMEFORMAT1 592 /* "%dh %dm" */
#define TXT_SCORE_TIMEFORMAT2 593 /* "%dm"     */
#define TXT_SCORE_NOD 594
/* the map screen's, conquer.h:517-522, and the shared click line :593 */
#define TXT_MAP_GDI 500
#define TXT_MAP_NOD 501
#define TXT_MAP_LOCATE 502
#define TXT_MAP_NEXT_MISSION 503
#define TXT_MAP_SELECT 504
#define TXT_MAP_TO_ATTACK 505
#define TXT_MAP_CLICK2 576

static int camp_eng_string(const unsigned char *d, long len, int index, char *out, int outlen)
{
    long count, off, end;
    if (!d || len < 2)
        return 0;
    count = (long)(d[0] | (d[1] << 8)) / 2;
    if (index < 0 || index >= count || (long)index * 2 + 1 >= len)
        return 0;
    off = (long)(d[index * 2] | (d[index * 2 + 1] << 8));
    if (off < 0 || off >= len)
        return 0;
    for (end = off; end < len && d[end]; end++)
        ;
    if (end - off >= outlen)
        return 0;
    memcpy(out, d + off, (size_t)(end - off));
    out[end - off] = 0;
    return 1;
}

static int camp_string(const Camp *c, int index, char *out, int outlen)
{
    out[0] = 0;
    if (!camp_eng_string(c->eng, c->englen, index, out, outlen)) {
        fprintf(stderr, "campaign: CONQUER.ENG has no string %d\n", index);
        return 0;
    }
    return 1;
}

static void camp_load_strings(Camp *c)
{
    const char *dd = c->au ? cnc_audio_dosdata(c->au) : "";
    static const int want[CAMP_TXT_COUNT] = {CAMP_TXT_GDI_NAME, CAMP_TXT_NOD_NAME,
                                             CAMP_TXT_SEL_TRANS};
    char path[600], err[256];
    MixFile *mx;
    long off = 0, size = 0;
    unsigned char *blob;
    int i, ok = 1;

    if (!dd || !dd[0]) {
        fprintf(stderr, "campaign: no dosdata folder -- CONQUER.ENG text unavailable\n");
        return;
    }
    snprintf(path, sizeof path, "%s/LOCAL.MIX", dd);
    mx = mixfile_open(path, err, (int)sizeof err);
    if (!mx) {
        fprintf(stderr, "campaign: %s: %s -- CONQUER.ENG text unavailable\n", path, err);
        return;
    }
    if (!mixfile_find(mx, "CONQUER.ENG", &off, &size) || size <= 0 || size > (1 << 20)) {
        fprintf(stderr, "campaign: %s has no usable CONQUER.ENG -- text unavailable\n", path);
        mixfile_close(mx);
        return;
    }
    blob = (unsigned char *)malloc((size_t)size);
    if (!blob || mixfile_read(mx, off, blob, (int)size) != (int)size) {
        fprintf(stderr, "campaign: CONQUER.ENG short read -- text unavailable\n");
        free(blob);
        mixfile_close(mx);
        return;
    }
    mixfile_close(mx);

    c->eng = blob; /* retained: score/map read their strings by TXT_ index */
    c->englen = size;

    for (i = 0; i < CAMP_TXT_COUNT; i++)
        if (!camp_eng_string(blob, size, want[i], c->txt[i], (int)sizeof c->txt[0])) {
            fprintf(stderr, "campaign: CONQUER.ENG has no string %d\n", want[i]);
            ok = 0;
        }
    c->txt_ok = ok;
    if (ok)
        printf("CAMPAIGN|strings|%s|%s|%s\n", c->txt[0], c->txt[1], c->txt[2]);
}

int camp_open(Camp *c, SDL_Window *win, struct CncAudio *au,
              const char *campack, const char *menupack, char *err, int errlen)
{
    memset(c, 0, sizeof(*c));
    c->win = win;
    c->au = au;
    c->fade = 256;
    c->pal = c->palbuf;
    c->mapdir = 'E';
    c->mx = c->my = -1; /* 0,0 is a legal plate coordinate; -1 is the sentinel */

    FILE *f = fopen(campack, "rb");
    if (!f) {
        snprintf(err, (size_t)errlen, "cannot open %s (run bake_campaign.py)", campack);
        return 0;
    }
    char magic[8];
    unsigned ver = 0, n = 0;
    if (!camp_read(f, magic, 8) || memcmp(magic, "CNC3DCPN", 8) != 0) {
        snprintf(err, (size_t)errlen, "%s: bad magic", campack);
        fclose(f);
        return 0;
    }
    if (!camp_read(f, &ver, 4) || !camp_read(f, &n, 4) || ver != 2 || n > 24) {
        snprintf(err, (size_t)errlen, "%s: format v%u, need v2 (re-run bake_campaign.py)",
                 campack, ver);
        fclose(f);
        return 0;
    }
    for (unsigned i = 0; i < n; i++) {
        CampEntry *e = &c->e[c->n];
        unsigned q[6];
        if (!camp_read(f, e->name, 16) || !camp_read(f, q, sizeof(q))
            || !camp_read(f, e->pal, 768)) {
            snprintf(err, (size_t)errlen, "%s: truncated", campack);
            fclose(f);
            return 0;
        }
        e->kind = (int)q[0]; e->frames = (int)q[1];
        e->w = (int)q[2]; e->h = (int)q[3]; e->x = (int)q[4]; e->y = (int)q[5];
        e->cur = -1;
        if (e->kind == 0) {
            /* a delta anim: per-frame LCW chunk sizes, then the chunks */
            unsigned total = 0, j;
            e->coff = (unsigned int *)malloc(sizeof(unsigned) * (size_t)(e->frames + 1));
            e->acc = (unsigned char *)malloc((size_t)e->w * e->h);
            if (!e->coff || !e->acc)
                goto trunc;
            memset(e->acc, 0, (size_t)e->w * e->h);
            for (j = 0; j < (unsigned)e->frames; j++) {
                unsigned sz;
                if (!camp_read(f, &sz, 4))
                    goto trunc;
                e->coff[j] = total;
                total += sz;
            }
            e->coff[e->frames] = total;
            e->chunks = (unsigned char *)malloc(total ? total : 1);
            if (!e->chunks || !camp_read(f, e->chunks, total))
                goto trunc;
        } else if (e->kind == 3) {
            /* palette only */
        } else {
            size_t bytes = (size_t)e->frames * e->w * e->h;
            e->px = (unsigned char *)malloc(bytes);
            if (!e->px || !camp_read(f, e->px, bytes))
                goto trunc;
        }
        c->n++;
        continue;
    trunc:
        snprintf(err, (size_t)errlen, "%s: truncated pixels", campack);
        fclose(f);
        return 0;
    }
    fclose(f);

    /* the menu pack's fonts (SCOREFNT, 6POINT) do the printing */
    c->fonts = db_pack_load(menupack, err, errlen);
    if (!c->fonts)
        fprintf(stderr, "campaign: %s -- screens run without text\n", err);

    camp_load_strings(c);

    glGenTextures(1, &c->tex);
    glBindTexture(GL_TEXTURE_2D, c->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

    /* The score screen's 3D faction logos, from beside campaign.pack. Never
       fatal: without the pack the score screen is exactly the 1995 one. */
    {
        char lp[512], lerr[256];
        const char *slash = strrchr(campack, '/');
        size_t n = slash ? (size_t)(slash - campack) + 1 : 0;
        if (n > sizeof lp - 16) n = 0;
        memcpy(lp, campack, n);
        snprintf(lp + n, sizeof lp - n, "logos.pack");
        c->logo_ok = logo3d_open(&c->logo, lp, lerr, sizeof lerr);
        if (!c->logo_ok)
            fprintf(stderr, "campaign: %s -- score screen runs without its logo\n", lerr);
    }
    c->logo_side = -1;          /* memset made this 0, which is GDI */

    err[0] = 0;
    return 1;
}

void camp_close(Camp *c)
{
    for (int i = 0; i < c->n; i++) {
        free(c->e[i].px);
        free(c->e[i].chunks);
        free(c->e[i].coff);
        free(c->e[i].acc);
    }
    free(c->eng);
    if (c->fonts)
        db_pack_free((DB_Pack *)c->fonts);
    if (c->tex)
        glDeleteTextures(1, &c->tex);
    logo3d_close(&c->logo);
    memset(c, 0, sizeof(*c));
}

static const CampEntry *camp_entry(const Camp *c, const char *name)
{
    for (int i = 0; i < c->n; i++)
        if (!strcmp(c->e[i].name, name))
            return &c->e[i];
    fprintf(stderr, "campaign: pack has no entry %s\n", name);
    return NULL;
}

/* ------------------------------------------------------------------ presentation */

/* plate + 6-bit palette -> RGBA -> letterboxed quad. Complete GL state every frame,
   the same discipline every screen in this program follows. c->fade scales the
   palette 0..256: the transcription of Fade_Palette_To, since a from-black fade is
   a brightness ramp of the target palette. */
static void camp_draw(Camp *c)
{
    static unsigned char rgba[512 * 256 * 4]; /* POT pad for the 320x200 plate */
    static unsigned char comp[320 * 200];     /* plate + pointer; never the plate */
    const unsigned char *pal = c->pal;
    const int fade = c->fade;
    const unsigned char *src = c->plate;

    /* The DOS pointer goes into a COPY of the plate, never into the plate itself.
       The plate is CUMULATIVE -- typed text and anim blits build up in it frame
       after frame (see camp_surface's block comment) -- so stamping the arrow in
       would leave a trail of arrows behind the mouse. Compositing here rather than
       drawing a second GL quad also means camp_shot's PNG carries the pointer, and
       the arrow is magnified by the same integer scale as every other 1995 pixel.

       MOUSE.SHP frame 0 is the plain arrow, MouseControl[MOUSE_NORMAL] =
       {0,1,0,86,0,0} (mouse.cpp:304, transcribed at game/cursors.h:75): hotspot
       (0,0), so the shape draws at the pointer unshifted. It is the same shape the
       DOS menu draws (menu/dosmenu.c:427-434) out of the same pack -- camp_open
       keeps dosmenu.pack in c->fonts -- and the same one the tactical view's
       cur_init uses. db_draw_shape skips index 0 and clips (game/dosbar.c:283). */
    if (c->curon && c->mx >= 0 && c->fonts) {
        const DB_Shape *m = db_shape((DB_Pack *)c->fonts, "MOUSE");
        if (m) {
            DB_Surface s;
            memcpy(comp, c->plate, sizeof comp);
            s.w = 320; s.h = 200; s.px = comp;
            s.cx0 = 0; s.cy0 = 0; s.cx1 = 319; s.cy1 = 199;
            db_draw_shape(&s, m, 0, c->mx, c->my);
            src = comp;
        }
    }
    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 320; x++) {
            const unsigned v = src[y * 320 + x];
            unsigned char *o = &rgba[(y * 512 + x) * 4];
            o[0] = (unsigned char)(((pal[v * 3] << 2) * fade) >> 8);
            o[1] = (unsigned char)(((pal[v * 3 + 1] << 2) * fade) >> 8);
            o[2] = (unsigned char)(((pal[v * 3 + 2] << 2) * fade) >> 8);
            o[3] = 255;
        }
    }
    int fbw = 0, fbh = 0;
    SDL_GL_GetDrawableSize(c->win, &fbw, &fbh);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, fbw, fbh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, fbw, fbh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int scale = fbw / 320 < fbh / 200 ? fbw / 320 : fbh / 200;
    if (scale < 1) scale = 1;
    const int w = 320 * scale, h = 200 * scale;
    const int x0 = (fbw - w) / 2, y0 = (fbh - h) / 2;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, c->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glColor3f(1, 1, 1);
    const float u1 = 320.0f / 512.0f, v1 = 200.0f / 256.0f;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);   glVertex2i(x0, y0);
    glTexCoord2f(u1, 0);  glVertex2i(x0 + w, y0);
    glTexCoord2f(u1, v1); glVertex2i(x0 + w, y0 + h);
    glTexCoord2f(0, v1);  glVertex2i(x0, y0 + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    /* THE FACTION LOGO, over the finished plate. Its box is in plate pixels and
       is scaled by the same integer `scale` as every 1995 pixel, so it holds its
       place in the layout at any window size. The angle comes off camp_now, which
       is the synthetic clock under --flowtest, so a flow shot is reproducible. */
    if (c->logo_ok && c->logo_side >= 0) {
        /* EACH FACTION DRAWS ITS OWN BRIEFING LOGO, and never the title disc.
           Reported, over a recording of a GDI score screen turning its Nod
           face forward: "Both Logos combined into onw", then "use BRF_LOGO_GDI
           (dl_01E9A50) for the GDI score screen, and BRF_LOGO_NOD (dl_01EF058) for
           the Nod screen." Those are slots 0 and 1 of the pack.

           WHY THE TITLE DISC WAS WRONG HERE. It carries both emblems back to back on
           one model, so a full revolution shows the OTHER faction for half of every
           turn. That reads as one logo morphing into the other, which is what he saw.

           AND THE BRIEFING MEDALLIONS ARE NOT FLAT PLATES, which is why they can take
           a full turn where the title disc could not. Reported, on a viewer capture of
           BRF_LOGO_NOD: "The ones I sent you are not onesided." They are solid
           medallions -- 717 triangles for Nod, 908 for GDI, 46 and 36 units thick --
           with a real bevelled edge and a dark backing face. Turned all the way round
           they show their emblem, then their rim, then their own back, and never the
           other faction. */
        const float ang = camp_logo_angle(c);
        logo3d_draw(&c->logo, c->logo_side ? LOGO3D_NOD : LOGO3D_GDI, fbh,
                    x0 + LOGO_PX * scale, y0 + LOGO_PY * scale,
                    LOGO_PW * scale, LOGO_PH * scale, ang, fade,
                    0.0f, 0.0f, 1.0f, 1.0f, LOGO3D_TILT_DEFAULT);
    }
}

static void camp_present(Camp *c)
{
    camp_draw(c);
    SDL_GL_SwapWindow(c->win);
}

/* window coords -> 320x200 plate coords; -1,-1 when outside the plate */
static void camp_unmap(Camp *c, int wx, int wy, int *px, int *py)
{
    int fbw = 0, fbh = 0, ww = 0, wh = 0;
    SDL_GL_GetDrawableSize(c->win, &fbw, &fbh);
    SDL_GetWindowSize(c->win, &ww, &wh);
    if (ww > 0) wx = (int)((long)wx * fbw / ww);
    if (wh > 0) wy = (int)((long)wy * fbh / wh);
    int scale = fbw / 320 < fbh / 200 ? fbw / 320 : fbh / 200;
    if (scale < 1) scale = 1;
    const int x0 = (fbw - 320 * scale) / 2, y0 = (fbh - 200 * scale) / 2;
    *px = (wx - x0) / scale;
    *py = (wy - y0) / scale;
    if (*px < 0 || *px >= 320 || *py < 0 || *py >= 200) { *px = -1; *py = -1; }
}

/* Show_Mouse / Hide_Mouse. The three screens' 1995 lifetimes, read out of the GPL
 * sources rather than guessed:
 *
 *   side select  intro.cpp:117 Hide_Mouse on entry; :182-183 Show_Mouse only once
 *                every ScorePrintClass has retired; :243 Hide_Mouse on the choice.
 *   map select   mapsel.cpp:1030-1031 Show_Mouse right after "SELECT TERRITORY TO
 *                ATTACK" prints; :1133 Hide_Mouse once a territory is picked.
 *   score        score.cpp:733 Hide_Mouse ... :1004 Show_Mouse -- and that
 *                Show_Mouse sits AFTER Cycle_Wait_Click (:990), after the score
 *                objects are deleted and after Fade_Palette_To(BlackPalette) +
 *                VisiblePage.Clear(). So it is handing the pointer back to the
 *                CALLER, not lighting one up for CLICK TO CONTINUE: the whole
 *                score screen, tally, hall of fame and name entry alike, runs with
 *                no pointer. Cycle_Wait_Click waits on the keyboard buffer, which
 *                in DOS is where the mouse buttons land too, so "CLICK TO CONTINUE"
 *                works pointerless by construction.
 *
 * The HOST pointer is off for the whole app -- the DOS menu turns it off
 * (menu/dosmenu_shell.c:389) and game_shutdown turns it back on
 * (game/cnc_eyes.cpp:8054) -- so every call re-asserts DISABLE rather than trusting
 * whoever ran last. That is why a faction that was not visible could still be clicked. */
static void camp_show_mouse(Camp *c, int on)
{
    SDL_ShowCursor(SDL_DISABLE);
    if (on && c->mx < 0) {
        if (camp_autopilot) {
            /* Hands-off runs generate no SDL_MOUSEMOTION, and where the real
               pointer happens to sit over a hidden window is not reproducible --
               the flow PNGs have to stay byte-identical between runs. Park it on
               the spot the side-select autopilot itself clicks. */
            c->mx = 80; c->my = 100;
        } else {
            int wx = 0, wy = 0;
            SDL_GetMouseState(&wx, &wy);
            camp_unmap(c, wx, wy, &c->mx, &c->my);
        }
    }
    c->curon = on ? 1 : 0;
}

/* the active palette is always the MUTABLE copy: cycling and fades write palbuf */
static void camp_use_pal(Camp *c, const unsigned char *pal)
{
    memcpy(c->palbuf, pal, 768);
    c->pal = c->palbuf;
}

/* The current frame of an entry. Delta anims advance their accumulator to `frame`,
   replaying from zero on a backward jump (a handful of small chunks, microseconds);
   kinds 1/2 just index. */
static const unsigned char *camp_frame(const Camp *c, const CampEntry *ce, int frame)
{
    static unsigned char scratch[320 * 200 + 4096];
    CampEntry *e = (CampEntry *)ce; /* the accumulator is mutable cache state */
    (void)c;
    if (frame < 0) frame = 0;
    if (frame >= e->frames) frame = e->frames - 1;
    if (e->kind != 0)
        return e->px + (size_t)frame * e->w * e->h;
    if (frame < e->cur) {
        memset(e->acc, 0, (size_t)e->w * e->h);
        e->cur = -1;
    }
    while (e->cur < frame) {
        long n;
        e->cur++;
        n = camp_lcw(e->chunks + e->coff[e->cur],
                     (long)(e->coff[e->cur + 1] - e->coff[e->cur]),
                     scratch, (long)sizeof scratch);
        camp_xor(e->acc, (long)e->w * e->h, scratch, n);
    }
    return e->acc;
}

/* blit one frame of an entry onto the plate at its authored (x,y), opaque,
   and make its palette the active one */
static void camp_blit(Camp *c, const CampEntry *e, int frame)
{
    if (!e) return;
    const unsigned char *src = camp_frame(c, e, frame);
    for (int y = 0; y < e->h; y++) {
        const int dy = e->y + y;
        if (dy < 0 || dy >= 200) continue;
        int wcopy = e->w;
        if (e->x + wcopy > 320) wcopy = 320 - e->x;
        memcpy(&c->plate[dy * 320 + e->x], &src[y * e->w], (size_t)wcopy);
    }
    camp_use_pal(c, e->pal);
}

/* CC_Draw_Shape: colour 0 transparent, top-left at (x,y), clipped. maxw < w keeps
   only the left maxw columns (the kill-bar white flash draws BAR3RED shape 120
   clipped to the bar's length -- score.cpp:1280 blits 3+gdikilled columns). */
static void camp_shape_clip(Camp *c, const CampEntry *e, int frame, int x, int y, int maxw)
{
    if (!e) return;
    if (frame < 0) frame = 0;
    if (frame >= e->frames) frame = e->frames - 1;
    const unsigned char *src = e->px + (size_t)frame * e->w * e->h;
    int w = e->w;
    if (maxw >= 0 && maxw < w) w = maxw;
    for (int yy = 0; yy < e->h; yy++) {
        const int dy = y + yy;
        if (dy < 0 || dy >= 200) continue;
        for (int xx = 0; xx < w; xx++) {
            const int dx = x + xx;
            const unsigned char v = src[yy * e->w + xx];
            if (dx < 0 || dx >= 320 || !v) continue;
            c->plate[dy * 320 + dx] = v;
        }
    }
}

static void camp_shape(Camp *c, const CampEntry *e, int frame, int x, int y)
{
    camp_shape_clip(c, e, frame, x, y, -1);
}

/* Fill_Rect, inclusive corners like the 1995 library */
static void camp_fill(Camp *c, int x0, int y0, int x1, int y1, unsigned char colour)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 319) x1 = 319;
    if (y1 > 199) y1 = 199;
    for (int y = y0; y <= y1; y++)
        memset(&c->plate[y * 320 + x0], colour, (size_t)(x1 - x0 + 1));
}

/* The plate as a DB_Surface. NOT db_surface_init: that clears the storage, and the
   plate already holds the screen art the text sits ON (found the hard way: three
   prints in a row left a black screen with only the last line standing). */
static void camp_surface(Camp *c, DB_Surface *s)
{
    s->w = 320; s->h = 200; s->px = c->plate;
    s->cx0 = 0; s->cy0 = 0; s->cx1 = 319; s->cy1 = 199;
}

/* Immediate print with a verbatim 16-slot class palette, the way the score and map
 * screens print their non-typed text (Count_Up_Print, Print_Minutes).
 *
 * A Westwood .FNT stores a 4-bit CLASS per pixel and the font palette is the
 * class -> colour lookup (common/font.cpp Buffer_Print). SCOREFNT is a SHADED face:
 * its glyphs use classes 2/4/6/8/14, never class 1, which is why score.cpp's
 * gradient tables carry their ink at the even slots. Buffer_Print (font.cpp:341)
 * overrides slots 0 and 1 with bground/fground -- every score-screen print passes
 * TBLACK (0) for both, so the tables below carry 0 there and db_print's skip of
 * output colour 0 reproduces the transparency. FontXSpacing is 0 on these screens
 * (score.cpp:637). */
static void camp_print16(Camp *c, const char *font, const char *text, int x, int y,
                         const unsigned char pal[16])
{
    const DB_Font *f;
    DB_Surface s;
    if (!c->fonts) return;
    f = db_font((DB_Pack *)c->fonts, font);
    if (!f) return;
    camp_surface(c, &s);
    db_print(&s, f, text, x, y, pal, 0);
}

/* --------------------------------------------------- ScorePrintClass, transcribed
 *
 * score.cpp:279-341. The three campaign screens all print through this: side select
 * (intro.cpp:165/169/177), the score screen and 17 more call sites in mapsel.cpp.
 *
 * Two things it is NOT. It is not a plain reveal: it is a two stage teletype. At
 * stage N it stamps character N in SOLID WHITE three times, at (pos, y-1), (pos, y+1)
 * and (pos+1, y) and never at (pos, y) -- and _whitepal maps class 0 to white too, so
 * that paints the whole 6x6 cell, giving a blocky sparkle running one character ahead
 * of the text. At stage N+1 it erases that smear by redrawing the same glyph at the
 * same three offsets in _blackpal (every class opaque BLACK) and then stamps the real
 * character once at (pos-6, y) in the faction palette, whose class 0 IS transparent.
 *
 * And it is not laid out by glyph advance: score.cpp:302 is `pos = XPos + Stage * 6`,
 * a hard 6 pixel pitch. That pitch IS the layout -- TXT_SEL_TRANS is 19 characters,
 * 19 * 6 = 114, (320 - 114) / 2 = 103, which is exactly intro.cpp's x for that line.
 * Centring by string width instead would shift it, because SCOREFNT's space is 5 wide
 * where every letter is 6.
 *
 * One character per 1/60 s (score.cpp:298 Timer.Set(1) on BT_SYSTEM), all lines
 * typing in parallel, so the longest of the three finishes in about 430 ms. */
static const unsigned char camp_whitepal[16] = {
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F}; /* score.cpp:283 _whitepal */
static const unsigned char camp_blackpal[16] = {
    DB_BLACK, DB_BLACK, DB_BLACK, DB_BLACK, DB_BLACK, DB_BLACK, DB_BLACK, DB_BLACK,
    DB_BLACK, DB_BLACK, DB_BLACK, DB_BLACK, DB_BLACK, DB_BLACK, DB_BLACK,
    DB_BLACK}; /* score.cpp:310-325 _blackpal, opaque black (index 12), not index 0 */

typedef struct CampType {
    const char *text;
    int x, y;
    const unsigned char *pal;
    int stage;
    int done;
} CampType;

static void camp_glyph(Camp *c, const DB_Font *f, char ch, int x, int y,
                       const unsigned char pal[16])
{
    DB_Surface s;
    char one[2];
    one[0] = ch;
    one[1] = 0;
    camp_surface(c, &s);
    db_print(&s, f, one, x, y, pal, 0);
}

static void camp_type_step(Camp *c, const DB_Font *f, CampType *t)
{
    int pos;
    char ch;

    if (t->done || !f)
        return;
    if (t->stage && t->text[t->stage - 1] == 0) { /* score.cpp:286, the retirement */
        t->done = 1;
        return;
    }
    pos = t->x + t->stage * 6; /* score.cpp:302 */
    if (t->stage) {
        ch = t->text[t->stage - 1];
        camp_glyph(c, f, ch, pos - 6, t->y - 1, camp_blackpal);
        camp_glyph(c, f, ch, pos - 6, t->y + 1, camp_blackpal);
        camp_glyph(c, f, ch, pos - 6 + 1, t->y, camp_blackpal);
        camp_glyph(c, f, ch, pos - 6, t->y, t->pal);
    }
    if (t->text[t->stage]) {
        ch = t->text[t->stage];
        camp_glyph(c, f, ch, pos, t->y - 1, camp_whitepal);
        camp_glyph(c, f, ch, pos, t->y + 1, camp_whitepal);
        camp_glyph(c, f, ch, pos + 1, t->y, camp_whitepal);
    }
    t->stage++;
}

/* Keyboard->Check() / Get() / Clear() (score.cpp:1629-1637). A full queue drops the
   newest key, which is what the 1995 ring buffer does too. */
static void camp_key_push(Camp *c, char ch)
{
    if (c->keyqn < (int)sizeof c->keyq)
        c->keyq[c->keyqn++] = ch;
}

static int camp_key_pop(Camp *c)
{
    int ch, i;
    if (c->keyqn <= 0)
        return 0;
    ch = (unsigned char)c->keyq[0];
    for (i = 1; i < c->keyqn; i++)
        c->keyq[i - 1] = c->keyq[i];
    c->keyqn--;
    return ch;
}

static void camp_key_clear(Camp *c) { c->keyqn = 0; }

/* pump SDL + audio once; returns 0 if the window was closed */
static int camp_pump(Camp *c, int *clickx, int *clicky, int *keyed)
{
    SDL_Event e;
    if (clickx) *clickx = -1;
    if (keyed) *keyed = 0;
    while (SDL_PollEvent(&e)) {
        if (fs_handle_event(&e)) continue;   /* CMD+F / ALT+ENTER, see fullscreen.h */
        if (e.type == SDL_QUIT) return 0;
        if (e.type == SDL_KEYDOWN) {
            if (keyed) *keyed = 1;
            /* Backspace and Return are NOT text events, so they can only come from
               here. Every printable key comes from SDL_TEXTINPUT below; taking it
               from KEYDOWN too would enter every letter twice ("CCNNCC"). */
            if (e.key.keysym.sym == SDLK_BACKSPACE)
                camp_key_push(c, '\b');
            else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)
                camp_key_push(c, '\r');
        }
        if (e.type == SDL_TEXTINPUT) {
            const unsigned char *t = (const unsigned char *)e.text.text;
            int ti;
            for (ti = 0; t[ti]; ti++)
                if (t[ti] >= 0x20 && t[ti] <= 0x7E) /* score.cpp:1663's accept set */
                    camp_key_push(c, (char)t[ti]);
        }
        /* Under the harness the pointer is PARKED and stays parked: camp_show_mouse
           puts it at 80,100 and the flow PNGs assert on an arrow being there. A real
           mouse moving over the window would drag the drawn arrow and fail a gate for
           a reason that has nothing to do with the build, so autopilot ignores host
           motion entirely. (Not observed to bite -- SDL did not deliver motion to the
           unfocused harness window here -- but determinism is not a thing to leave to
           window focus.) */
        if (e.type == SDL_MOUSEMOTION && !camp_autopilot)
            camp_unmap(c, e.motion.x, e.motion.y, &c->mx, &c->my);
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && clickx) {
            camp_unmap(c, e.button.x, e.button.y, clickx, clicky);
            /* a click parks the pointer too: a click with no preceding motion
               (tap-to-click, a warped pointer) still has to move the arrow */
            if (*clickx >= 0 && !camp_autopilot) { c->mx = *clickx; c->my = *clicky; }
        }
    }
    /* audio_frame, NOT cnc_audio_update. They do the same thing when a real device
       is open, but under --audiowav the tap is what renders the mix into the file
       (audioboot.c:147-155), and calling cnc_audio_update directly bypasses it. It
       did: a ~50 s flowtest recorded a 21.6 s WAV, because the three campaign screens
       contributed NOTHING to the recording. Every audio claim about side select, the
       score screen or the map screen was unmeasurable until this line changed, which
       is exactly the "test that passes because nothing objected" shape.
       ~17 ms is one loop iteration at the 60 Hz these screens run at. */
    if (c->au) audio_frame(c->au, CAMP_TICK_MS(1));
    c->fakenow += (Uint32)CAMP_TICK_MS(1); /* the autopilot clock, see camp_now */
    return 1;
}

/* one-shot sample through the FX bus. `vol` is the 1995 0..255 volume argument
   (Play_Sample's), priority 255 like every score/map Play_Sample call. */
static void camp_sfx(Camp *c, const char *name, int vol)
{
    if (c->au)
        cnc_audio_play_named(c->au, name, MIX_BUS_FX, vol * MIX_UNITY / 255, 0, 255);
}

/* ---------------------------------------------------------------- the fame file
 *
 * score.cpp:900-920 and :976-983. This is NOT the mission save system, which is what
 * the old deferral comment in camp_score claimed it needed: it is 140 bytes, seven
 * records of {char name[12]; int32 score; int32 level;}, which 1995 wrote as
 * HALLFAME.DAT (defines.h:244) next to CONQUER.EXE. We keep the BYTE LAYOUT exactly,
 * little-endian like the disc (score.cpp:917-918 le32toh on read, :979-980 htole32 on
 * write), so a real 1995 HALLFAME.DAT drops straight in and ours drops straight back
 * out.
 *
 * We do NOT keep the location. The playable directory is read-only on a packaged
 * build and is shared by concurrent gate runs, so the default is the platform's
 * per-user data directory via SDL_GetPrefPath, which nests org/app: measured on this
 * machine as
 *     ~/Library/Application Support/Slipgate Ironworks/CNC3D/HALLFAME.DAT
 * (%APPDATA%\... on Windows, ~/.local/share/... on Linux). SDL creates the directory
 * as a side effect and can return NULL on a locked-down system, which degrades to
 * "no persistence" rather than a crash. --famefile overrides the whole thing for
 * anyone who wants the 1995 side-by-side layout. */
#define CAMP_NUMFAMENAMES 7
#define CAMP_FAMENAME_LEN 12 /* MAX_FAMENAME_LENGTH: 11 characters + NUL */
#define CAMP_FAME_REC     20 /* 12 name + 4 score + 4 level, as on disc */

typedef struct CampFame {
    char name[CAMP_FAMENAME_LEN];
    int  score, level;
} CampFame;

const char *camp_fame_file = NULL;

/* Does this run touch a fame file at all?
 *
 * A plain --flowtest run must not, and that is load-bearing rather than tidiness:
 * score.cpp:963's Call_Back_Delay(13) fires once per NON-ZERO row, so the screen's
 * LENGTH is a function of how many rows are filled. A persistent table would make
 * G14/G15 drift longer every time anyone played, and the flow PNGs would stop being
 * byte-reproducible.
 *
 * An EXPLICIT --famefile opts back in, autopilot or not, so the persistent path is
 * something a gate can drive rather than something only a human can ever reach.
 * Such a gate owns its own scratch path and gets a deterministic run of its own;
 * the default gate, which passes no --famefile, still sees the same blank one-row
 * board every time and still leaves no file behind. */
static int camp_fame_enabled(void)
{
    return !camp_autopilot || (camp_fame_file && camp_fame_file[0]);
}

static const char *camp_fame_path(void)
{
    static char buf[1024];
    char *pref;
    if (camp_fame_file && camp_fame_file[0])
        return camp_fame_file;
    if (buf[0])
        return buf;
    pref = SDL_GetPrefPath("Slipgate Ironworks", "CNC3D");
    if (!pref)
        return NULL; /* locked-down system: degrade to "no persistence", not a crash */
    snprintf(buf, sizeof buf, "%sHALLFAME.DAT", pref);
    SDL_free(pref);
    return buf;
}

static void camp_fame_save(const CampFame f[CAMP_NUMFAMENAMES])
{
    unsigned char raw[CAMP_NUMFAMENAMES * CAMP_FAME_REC];
    const char *path;
    FILE *fp;
    int i;

    if (!camp_fame_enabled())
        return; /* a plain --flowtest run leaves no file behind */
    path = camp_fame_path();
    if (!path)
        return;
    /* 1995 wrote uninitialised stack past the NUL here (score.cpp:908 sets only
       name[0]); we zero. Deliberate, recorded as a known gap. */
    memset(raw, 0, sizeof raw);
    for (i = 0; i < CAMP_NUMFAMENAMES; i++) {
        unsigned char *p = raw + i * CAMP_FAME_REC;
        unsigned s = (unsigned)f[i].score, l = (unsigned)f[i].level;
        memcpy(p, f[i].name, CAMP_FAMENAME_LEN);
        p[12] = (unsigned char)s;         p[13] = (unsigned char)(s >> 8);
        p[14] = (unsigned char)(s >> 16); p[15] = (unsigned char)(s >> 24);
        p[16] = (unsigned char)l;         p[17] = (unsigned char)(l >> 8);
        p[18] = (unsigned char)(l >> 16); p[19] = (unsigned char)(l >> 24);
    }
    fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "campaign: cannot write %s\n", path);
        return;
    }
    fwrite(raw, 1, sizeof raw, fp);
    fclose(fp);
}

static void camp_fame_load(CampFame f[CAMP_NUMFAMENAMES])
{
    unsigned char raw[CAMP_NUMFAMENAMES * CAMP_FAME_REC];
    const char *path;
    FILE *fp;
    int i;

    memset(f, 0, sizeof(CampFame) * CAMP_NUMFAMENAMES);
    if (!camp_fame_enabled())
        return; /* blank in, nothing out -- see camp_fame_enabled */
    path = camp_fame_path();
    if (!path)
        return;
    fp = fopen(path, "rb");
    if (!fp) {                 /* score.cpp:903 !Is_Available -> write a blank one */
        camp_fame_save(f);
        return;
    }
    if (fread(raw, 1, sizeof raw, fp) == sizeof raw)
        for (i = 0; i < CAMP_NUMFAMENAMES; i++) {
            const unsigned char *p = raw + i * CAMP_FAME_REC;
            memcpy(f[i].name, p, CAMP_FAMENAME_LEN);
            f[i].name[CAMP_FAMENAME_LEN - 1] = 0;
            f[i].score = (int)((unsigned)p[12] | ((unsigned)p[13] << 8)
                             | ((unsigned)p[14] << 16) | ((unsigned)p[15] << 24));
            f[i].level = (int)((unsigned)p[16] | ((unsigned)p[17] << 8)
                             | ((unsigned)p[18] << 16) | ((unsigned)p[19] << 24));
        }
    fclose(fp);
}

/* ------------------------------------------------------------------ side select */

/* Choose_Side's own font palettes, intro.cpp:82-84 VERBATIM -- the DOS era tables,
   preserved as comments inside the REMASTER_BUILD block. The ink sits at slots 2, 4,
   6, 8 and 14 and nowhere else, which is an exact match for SCOREFNT's class set
   {2,4,6,8,14}; every other slot is 0 and db_print skips colour 0. All fifteen
   indices were checked against CHOOSE.WSA's own palette, which campaign.pack already
   carries: 0xC9/0xBA/0x93/0x61/0xEE is a gold ramp, 0xA8/0xD9/0xDA/0xE1/0xD4 a red
   one, 0x17/0x10/0x12/0x14/0x1C a grey one.

   (The C&C95 forms a few lines further down in intro.cpp are the SAME tables
   re-compacted into slots 1..5 for the hi-res gradient font, and are the wrong shape
   for SCOREFNT. _redpal there still carries 0xE1 at slot 8 and 0xD4 at slot 14, dead
   fossils of this DOS layout, which is how you tell the two apart.) */
static const unsigned char camp_yellowpal[16] = {
    0x00, 0x00, 0xC9, 0x00, 0xBA, 0x00, 0x93, 0x00,
    0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEE, 0x00};
static const unsigned char camp_redpal[16] = {
    0x00, 0x00, 0xA8, 0x00, 0xD9, 0x00, 0xDA, 0x00,
    0xE1, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD4, 0x00};
static const unsigned char camp_graypal[16] = {
    0x00, 0x00, 0x17, 0x00, 0x10, 0x00, 0x12, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00};

int camp_side_select(Camp *c)
{
    c->logo_side = -1;          /* no logo on the side select */
    const CampEntry *ch = camp_entry(c, "CHOOSE");
    const DB_Font *sf;
    CampType type[CAMP_TXT_COUNT];
    int statich = -1;
    int frame = 0, endframe = -1, side = -1;
    Uint32 last, chose_ms = 0, open_ms, type0 = 0;
    int speech_handle = -1;
    int typed = 0, letters_done = 0, music_off = 0;
    int i;

    if (!ch) return 0; /* degrade: GDI, exactly what the remaster stub does */

    memset(c->plate, 0, sizeof(c->plate));
    camp_use_pal(c, ch->pal);
    c->fade = 256;
    camp_show_mouse(c, 0); /* intro.cpp:117 Hide_Mouse */
    c->mapdir = 'E'; /* a fresh campaign starts on the EAST column (ScenDir init) */
    last = open_ms = SDL_GetTicks();

    /* intro.cpp:165/169/177 -- TXT_GDI_NAME at (0,180) in gold, TXT_NOD_NAME at
       (180,180) in red, TXT_SEL_TRANS at (103,190) in grey, all through
       ScorePrintClass and all in SCOREFNT (intro.cpp:119 Set_Font(ScoreFontPtr)).
       At the fixed 6 px pitch that is 0..149, 180..287 and 103..216 on a 320 px
       plate: nothing clips, and nothing overlaps the WSA, which occupies rows
       22..177 (campaign.pack CHOOSE is 320x156 at y=22, matching intro.cpp:192). */
    sf = c->fonts ? db_font((DB_Pack *)c->fonts, "SCOREFNT") : NULL;
    memset(type, 0, sizeof type);
    type[CAMP_TXT_GDI].text = c->txt[CAMP_TXT_GDI];
    type[CAMP_TXT_GDI].x = 0;   type[CAMP_TXT_GDI].y = 180;
    type[CAMP_TXT_GDI].pal = camp_yellowpal;
    type[CAMP_TXT_NOD].text = c->txt[CAMP_TXT_NOD];
    type[CAMP_TXT_NOD].x = 180; type[CAMP_TXT_NOD].y = 180;
    type[CAMP_TXT_NOD].pal = camp_redpal;
    type[CAMP_TXT_SEL].text = c->txt[CAMP_TXT_SEL];
    type[CAMP_TXT_SEL].x = 103; type[CAMP_TXT_SEL].y = 190;
    type[CAMP_TXT_SEL].pal = camp_graypal;
    if (!sf || !c->txt_ok) {
        /* No font or no disc strings: print nothing rather than substitute anything,
           and do not hold the screen hostage waiting for letters that never come. */
        letters_done = 1;
        fprintf(stderr, "campaign: side select runs without its text\n");
    }

    /* init.cpp:1024-1025 -- `Theme.Fade_Out(); Choose_Side();`. theme.h:86 makes
       Fade_Out() a Queue_Song(THEME_NONE): a THEME_DELAY == TIMER_SECOND == 1 s fade
       with NOTHING queued behind it (theme.h:63, theme.cpp:280). Side select has no
       score of its own, only the static bed and the selection line, so the menu's
       MAP1 must go. Our camp_pump keeps calling cnc_audio_update, which keeps
       refilling the music ring, so without this the menu track plays straight
       through the screen -- the reported "menu music is playing during faction selection". */
    if (c->au)
        cnc_music_fade_out(c->au, 1000);

    /* intro.cpp:162  statichandle = Play_Sample(staticaud, 255, 64);
       common/audio.h:132  Play_Sample(sample, priority = 0xFF, volume = 0xFF, panloc)
       so 1995 runs the static bed at the HIGHEST priority and at 64/255 of full scale,
       about 12 dB down. The first cut had both parameters inverted (priority 5 at
       unity).

       1995 then RE-TRIGGERS the clip (intro.cpp:197-201). We cannot: our output is
       block based, so a re-trigger always leaves the tail of the block the voice died
       in as silence -- a measured, fixed 45.0 ms hole every 1.068 s at any poll rate,
       which is the reported "audio cuts between every loop". We loop the voice inside
       mixer_render instead. A deliberate deviation.

       A looping priority-255 voice never retires and cannot be stolen, so it MUST be
       stopped on every exit path below, the window-close one included. */
    if (c->au)
        statich = cnc_audio_play_named_loop(c->au, "STRUGGLE.AUD", MIX_BUS_FX,
                                            64 * MIX_UNITY / 255, 0, 255);

    printf("CAMPAIGN|side-select|open\n");
    fflush(stdout);
    for (;;) {
        const Uint32 now = SDL_GetTicks();
        int cx, cy, key;

        /* The 1 s ramp has run: close the stream so the ring stops being refilled,
           and put the fader back. cnc_music_fade_out only ramps the shared
           MIX_BUS_MUSIC, and a bus left at 0 would silence every later track. This
           mirrors ThemeClass, whose fade ends by stopping the sample without leaving
           the volume down for the next Play_Song.

           Back to the SLIDER, not to unity: cnc_audio_set_music_volume
           (Options.ScoreVolume, 0..255) owns this bus, so restoring unity would hand
           music back to a player who had turned it off. Same rule the movie sink
           uses in moviesnd.c ms_close. */
        if (c->au && !music_off && now - open_ms >= 1000) {
            const int v = cnc_audio_get_music_volume(c->au);
            music_off = 1;
            cnc_music_stop(c->au);
            mixer_bus_gain(cnc_audio_mixer(c->au), MIX_BUS_MUSIC, v * MIX_UNITY / 255, 0);
        }

        camp_blit(c, ch, frame);

        /* One character per 1/60 s, all three lines in parallel. The band survives
           between frames because camp_blit only rewrites rows 22..177.

           The clock starts on the FIRST RENDERED FRAME, not at function entry: the
           work above it decodes STRUGGLE.AUD out of the MIX, and if that ever costs
           more than 430 ms the whole teletype would be "already due" and would run to
           completion inside one frame, so the animation would simply never be seen.
           1995 has the same ordering by construction, because Alloc_Object happens
           after the sample is loaded and the objects are stepped from inside the
           display loop. */
        if (!letters_done) {
            int want;
            if (!type0)
                type0 = now ? now : 1;
            want = (int)((now - type0) / CAMP_TICK_MS(1));
            while (typed < want) {
                for (i = 0; i < CAMP_TXT_COUNT; i++)
                    camp_type_step(c, sf, &type[i]);
                typed++;
                if (type[0].done && type[1].done && type[2].done) {
                    letters_done = 1;
                    break;
                }
            }
        }
        /* intro.cpp:204-213 -- the pointer comes up only once every ScorePrintClass
           has retired, which is also exactly when the two logos become clickable
           below. Must precede the shot: camp_shot calls camp_draw, and the arrow in
           that PNG is the proof the screen has one. */
        if (letters_done && !c->curon)
            camp_show_mouse(c, 1);
        {
            /* photograph the finished band, not a half typed one */
            static int shot_once = 0;
            /* cwd, not /tmp: fixed /tmp names collide when two runs do flow
               tests at once, and one run's evidence silently overwrites another's
               (it did: a concurrent main-tree run swapped these PNGs mid-verify) */
            if (letters_done) camp_shot(c, "flow_side.png", &shot_once);
        }
        camp_present(c);

        if (!camp_pump(c, &cx, &cy, &key)) {
            if (c->au && statich >= 0)
                mixer_voice_stop(cnc_audio_mixer(c->au), statich);
            return -1;
        }
        if (camp_autopilot && side < 0 && letters_done && now - open_ms > 1200) {
            /* intro.cpp's own rectangles, read just below: GDI is 18..148, Nod is
               160..300. Click the middle of whichever one was asked for. */
            cx = camp_autopilot_side ? 230 : 80; cy = 100;
        }
        /* intro.cpp:204-213: the mouse stays HIDDEN until every ScorePrintClass has
           retired, so there is nothing to click with until the letters are through. */
        if (side < 0 && letters_done && cx >= 0 && cy > 48 && cy < 150) {
            /* intro.cpp's own rectangles */
            if (cx > 18 && cx < 148) {
                side = 0;
                endframe = 0;
                speech_handle = c->au ? cnc_audio_play_named(c->au, "GDI_SLCT.AUD",
                                                             MIX_BUS_SPEECH, MIX_UNITY, 0, 255) : -1;
            } else if (cx > 160 && cx < 300) {
                side = 1;
                endframe = 14;
                speech_handle = c->au ? cnc_audio_play_named(c->au, "NOD_SLCT.AUD",
                                                             MIX_BUS_SPEECH, MIX_UNITY, 0, 255) : -1;
            }
            if (side >= 0) {
                chose_ms = now;
                camp_show_mouse(c, 0); /* intro.cpp:243 Hide_Mouse on the choice */
                printf("CAMPAIGN|side-select|%s\n", side ? "NOD" : "GDI");
                fflush(stdout);
            }
        }

        /* intro.cpp:202 Call_Back_Delay(3) = 3 ticks of 1/60 s = 50 ms, so CHOOSE.WSA
           runs at 20 fps and its 30 frames make a 1.5 s cycle. */
        if (camp_wsa_due(now, &last, CAMP_TICK_MS(3))) {
            frame++;
            if (frame >= ch->frames)
                frame = 0;
        }
        if (side >= 0 && frame == endframe) {
            /* wait for the selection speech, but never longer than the line
               actually is: with no audio device the mixer never drains and a
               voice-active check alone would wait forever */
            const int talking = (speech_handle >= 0 && c->au
                                 && mixer_voice_active(cnc_audio_mixer(c->au), speech_handle));
            if (!talking || now - chose_ms > 3000)
                break;
        }
        SDL_Delay(1);
    }
    if (c->au && statich >= 0)
        mixer_voice_stop(cnc_audio_mixer(c->au), statich);
    /* intro.cpp:247 -- erase the "choose side" text:
       PseudoSeenBuff->Fill_Rect(0, 180, 319, 199, 0); */
    memset(&c->plate[180 * 320], 0, 20 * 320);
    return side;
}

/* =============================================================== the tick machine
 *
 * Call_Back_Delay(N) (score.cpp:1925) is the heartbeat of both remaining screens:
 * every 1/60 s tick it steps every live ScoreAnimClass object (the teletype lines,
 * the TIME/HISCORE/CREDS shape loops), runs the palette cycle when one is armed,
 * redraws, and polls input. CampCtx is that loop: camp_ctx_tick is one tick,
 * camp_ctx_delay(N) is Call_Back_Delay(N). Input latches (clickx/keyed) mirror the
 * 1995 Keyboard->Check()/Clear() pattern: they stay set until a transcription point
 * that called Keyboard->Clear() clears them. */

/* 14 covered the tally alone. The hall of fame allocates one ScorePrintClass per
   name (7) plus one per score and one per level on the FILLED rows (14 more in the
   worst case) plus TOP SCORES, on top of the 12 the GDI tally already uses (13 on
   Nod): 34 in the worst case. Retired objects keep their slot (camp_ctx_typing just
   skips the done ones), so this is a high-water mark, not a live count -- and
   camp_ctx_type's overflow path only prints to stderr and returns, so leaving it at
   14 would draw half a board and pass every test that only reads the log. */
#define CAMP_MAX_TYPE 40

typedef struct CampAnimObj {
    const CampEntry *shp;
    int x, y, maxstage, reset;
    int stage, timer, active, creds; /* creds: ScoreCredsClass's CLOCK1/CASHTURN */
} CampAnimObj;

typedef struct CampCtx {
    Camp *c;
    const DB_Font *sf;
    CampType type[CAMP_MAX_TYPE];
    char typebuf[CAMP_MAX_TYPE][64];
    int ntype;
    CampAnimObj anim[4];
    Uint32 last;
    int clickx, clicky, keyed, aborted;
    int cyc_lo, cyc_hi, cyc_mask, cyc_on, cyc_count;
    int fade_step;
} CampCtx;

static void camp_ctx_init(CampCtx *k, Camp *c)
{
    memset(k, 0, sizeof *k);
    k->c = c;
    k->sf = c->fonts ? db_font((DB_Pack *)c->fonts, "SCOREFNT") : NULL;
    k->last = camp_now(c);
    k->clickx = -1;
}

/* ScorePrintClass allocation for a string we already hold. The hall of fame types
   LIVE strings -- names off the fame table, numbers formatted here -- not
   CONQUER.ENG indices, which is why this half is factored out. */
static void camp_ctx_type_str(CampCtx *k, const char *text, int x, int y,
                              const unsigned char pal[16])
{
    CampType *t;
    if (k->ntype >= CAMP_MAX_TYPE) {
        fprintf(stderr, "campaign: type object overflow\n");
        return;
    }
    if (!k->sf)
        return; /* no font: print nothing, never substitute */
    snprintf(k->typebuf[k->ntype], sizeof k->typebuf[0], "%s", text);
    t = &k->type[k->ntype++];
    memset(t, 0, sizeof *t);
    t->text = k->typebuf[k->ntype - 1];
    t->x = x;
    t->y = y;
    t->pal = pal;
}

/* ScorePrintClass allocation: fetch the disc string, park it in the ctx, type it */
static void camp_ctx_type(CampCtx *k, int txtindex, int x, int y,
                          const unsigned char pal[16])
{
    char buf[64];
    if (!k->sf || !camp_string(k->c, txtindex, buf, (int)sizeof buf))
        return; /* no font or no disc string: print nothing, never substitute */
    camp_ctx_type_str(k, buf, x, y, pal);
}

static int camp_ctx_typing(const CampCtx *k) /* 1995's StillUpdating */
{
    int i;
    for (i = 0; i < k->ntype; i++)
        if (!k->type[i].done)
            return 1;
    return 0;
}

static CampAnimObj *camp_ctx_anim(CampCtx *k, const CampEntry *shp, int x, int y,
                                  int maxstage, int reset, int creds)
{
    int i;
    for (i = 0; i < 4; i++)
        if (!k->anim[i].active) {
            CampAnimObj *a = &k->anim[i];
            memset(a, 0, sizeof *a);
            a->shp = shp;
            a->x = x; a->y = y;
            a->maxstage = maxstage;
            a->reset = reset;
            a->timer = 0; /* ScoreAnimClass ctor: Timer.Set(0) -- fires first tick */
            a->active = (shp != NULL);
            a->creds = creds;
            return a;
        }
    return NULL;
}

/* one 1/60 s tick; 0 means the window was closed */
static int camp_ctx_tick(CampCtx *k)
{
    Camp *c = k->c;
    int cx, cy, key, i;

    for (;;) {
        if (!camp_pump(c, &cx, &cy, &key)) {
            k->aborted = 1;
            return 0;
        }
        if (cx >= 0) { k->clickx = cx; k->clicky = cy; }
        if (key) k->keyed = 1;
        if (camp_wsa_due(camp_now(c), &k->last, CAMP_TICK_MS(1)))
            break;
        SDL_Delay(1);
    }

    if (k->fade_step) {
        c->fade += k->fade_step;
        if (c->fade <= 0)   { c->fade = 0;   k->fade_step = 0; }
        if (c->fade >= 256) { c->fade = 256; k->fade_step = 0; }
    }

    /* the palette rotation both wait loops use: mapsel.cpp:1467-1494 rotates
       249..254 every 4th tick; score.cpp Cycle_Wait_Click rotates 233..237 every
       8th. One step moves every slot down and the first to the end. */
    if (k->cyc_on && !(++k->cyc_count & k->cyc_mask)) {
        unsigned char *p = c->palbuf;
        unsigned char r = p[k->cyc_lo * 3], g = p[k->cyc_lo * 3 + 1],
                      b = p[k->cyc_lo * 3 + 2];
        for (i = k->cyc_lo; i < k->cyc_hi; i++) {
            p[i * 3] = p[(i + 1) * 3];
            p[i * 3 + 1] = p[(i + 1) * 3 + 1];
            p[i * 3 + 2] = p[(i + 1) * 3 + 2];
        }
        p[k->cyc_hi * 3] = r;
        p[k->cyc_hi * 3 + 1] = g;
        p[k->cyc_hi * 3 + 2] = b;
        c->pal = c->palbuf;
    }

    for (i = 0; i < k->ntype; i++)
        camp_type_step(c, k->sf, &k->type[i]);

    for (i = 0; i < 4; i++) {
        CampAnimObj *a = &k->anim[i];
        if (!a->active)
            continue;
        if (--a->timer > 0)
            continue;
        a->timer = a->reset;
        a->stage++;
        if (a->stage >= a->maxstage)
            a->stage = 0;
        if (a->creds && c->au) {
            /* ScoreCredsClass::Update, score.cpp:242-261 */
            if (a->stage < 22)
                camp_sfx(c, "CLOCK1.AUD", 70);
            else if (a->stage == 24)
                camp_sfx(c, "CASHTURN.AUD", 70);
        }
        camp_shape(c, a->shp, a->stage, a->x, a->y);
    }

    camp_present(c);
    return 1;
}

static int camp_ctx_delay(CampCtx *k, int t) /* Call_Back_Delay(t); t=0 still ticks once */
{
    do {
        if (!camp_ctx_tick(k))
            return 0;
    } while (--t > 0);
    return 1;
}

/* ------------------------------------------------------------------ score screen
 *
 * ScoreClass::Presentation transcribed (score.cpp:583-1024). The gradient font
 * palettes are score.cpp:586-594 verbatim, with slots 0 and 1 zeroed because every
 * print on this screen passes TBLACK for both (see camp_print16's block comment). */
static const unsigned char camp_sc_redpal[16] = {
    0x00, 0x00, 0x24, 0x26, 0x28, 0x28, 0x28, 0x28,
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x21, 0x2F};
static const unsigned char camp_sc_greenpal[16] = {
    0x00, 0x00, 0x14, 0x16, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x10, 0x1F};
static const unsigned char camp_sc_yellowpal[16] = {
    0x00, 0x00, 0xEC, 0x00, 0xEB, 0x00, 0xEA, 0x00,
    0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0xED, 0x00};
/* score.cpp:586-594 _bluepal, the hall of fame's ramp (0x60..0x6F) */
static const unsigned char camp_sc_bluepal[16] = {
    0x00, 0x00, 0x64, 0x66, 0x68, 0x68, 0x68, 0x68,
    0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x61, 0x6F};

/* Count_Up_Print (score.cpp:1565): erase strlen*7 x 8 to opaque black, print the
   clamped number in the CURRENT gradient palette at a fixed spot. */
static void camp_countup(CampCtx *k, const char *fmt, int val, int max, int x, int y,
                         const unsigned char pal[16])
{
    char buf[24];
    snprintf(buf, sizeof buf, fmt, val <= max ? val : max);
    camp_fill(k->c, x, y, x + (int)strlen(buf) * 7, y + 7, DB_BLACK);
    camp_print16(k->c, "SCOREFNT", buf, x, y, pal);
}

/* Print_Minutes (score.cpp:1531): the disc's own "%dh %dm" / "%dm" formats. */
static void camp_minutes(CampCtx *k, int minutes, const unsigned char pal[16])
{
    char fmt[24], buf[32];
    if (minutes >= 60) {
        if (minutes / 60 > 9)
            minutes = 9 * 60 + 59;
        if (!camp_string(k->c, TXT_SCORE_TIMEFORMAT1, fmt, (int)sizeof fmt))
            return;
        snprintf(buf, sizeof buf, fmt, minutes / 60, minutes % 60);
    } else {
        if (!camp_string(k->c, TXT_SCORE_TIMEFORMAT2, fmt, (int)sizeof fmt))
            return;
        snprintf(buf, sizeof buf, fmt, minutes);
    }
    camp_print16(k->c, "SCOREFNT", buf, 275, 9, pal);
}

/* Do_GDI_Graph (score.cpp:1247): the two kill bars with their count-ups. The white
   flash on the last step is BAR3RED shape 120 clipped to the bar's length. */
static int camp_gdi_graph(CampCtx *k, const CampEntry *yellow, const CampEntry *red,
                          int gkilled, int nkilled, int ypos,
                          const unsigned char pal[16])
{
    int gdikilled = gkilled, nodkilled = nkilled, max, i;

    max = gdikilled > nodkilled ? gdikilled : nodkilled;
    if (!max) max = 1;
    gdikilled = gdikilled * 119 / max; /* SIZEGBAR */
    nodkilled = nodkilled * 119 / max;
    if (max < 20) {
        gdikilled = gkilled * 5;
        nodkilled = nkilled * 5;
    }
    max = gdikilled > nodkilled ? gdikilled : nodkilled;
    if (!max) max = 1;

    for (i = 1; i <= gdikilled; i++) {
        if (i != gdikilled)
            camp_shape(k->c, yellow, i, 172, ypos);
        else
            camp_shape_clip(k->c, red, 120, 172, ypos, 3 + gdikilled); /* white flash */
        camp_countup(k, "%d", i * gkilled / max, gkilled, 297, ypos + 2, pal);
        if (!k->keyed) {
            camp_sfx(k->c, "BEEPY6.AUD", 110);
            if (!camp_ctx_delay(k, 2)) return 0;
        }
    }
    camp_shape(k->c, yellow, gdikilled, 172, ypos);
    camp_countup(k, "%d", gkilled, gkilled, 297, ypos + 2, pal);
    if (!k->keyed && !camp_ctx_delay(k, 40)) return 0;

    for (i = 1; i <= nodkilled; i++) {
        if (i != nodkilled)
            camp_shape(k->c, red, i, 172, ypos + 12);
        else
            camp_shape_clip(k->c, red, 120, 172, ypos + 12, 3 + nodkilled);
        camp_countup(k, "%d", i * nkilled / max, nkilled, 297, ypos + 14, pal);
        if (!k->keyed) {
            camp_sfx(k->c, "BEEPY6.AUD", 110);
            if (!camp_ctx_delay(k, 2)) return 0;
        }
    }
    camp_shape(k->c, red, nodkilled, 172, ypos + 12);
    camp_countup(k, "%d", nkilled, nkilled, 297, ypos + 14, pal);
    if (!k->keyed && !camp_ctx_delay(k, 40)) return 0;
    return 1;
}

/* Show_Credits (score.cpp:1448): the CREDS.SHP cash counter next to the ramping
   ENDING CREDITS count-up. */
static int camp_credits(CampCtx *k, int house, int money, const unsigned char pal[16])
{
    static const int credsx[2] = {276, 276}, credsy[2] = {173, 58};
    static const int credpx[2] = {228, 236}, credpy[2] = {177, 74};
    static const int credtx[2] = {182, 182}, credty[2] = {167, 62};
    const CampEntry *credshape = camp_entry(k->c, "CREDS");
    CampAnimObj *creds;
    int i, add, min;

    camp_ctx_type(k, TXT_SCORE_ENDCRED, credtx[house], credty[house], pal);
    if (!camp_ctx_delay(k, 15)) return 0;

    creds = camp_ctx_anim(k, credshape, credsx[house], credsy[house], 32, 2, 1);
    min = money / 100;
    i = -50;
    do {
        add = 5;
        if (money - i > 100) add += 15;
        if (money - i > 1000) add += 30;
        if (add < min) add = min;
        i += add;
        if (i < 0) i = 0;
        camp_countup(k, "%d", i, money, credpx[house], credpy[house], pal);
        if (!camp_ctx_delay(k, 2)) return 0;
        if (k->keyed) {
            i = money - 5;
            k->keyed = 0;
        }
    } while (i < money);

    /* don't freeze the counter on its white stage (score.cpp:1510) */
    while (creds && creds->stage >= 20)
        if (!camp_ctx_delay(k, 1)) return 0;
    if (creds)
        creds->active = 0;
    return 1;
}

/* Cycle_Wait_Click (score.cpp:1031): palette 233..237 rotates every 8th tick under
   the yellow CLICK TO CONTINUE line; 20 ticks minimum before a click counts. */
static int camp_cycle_wait_click(CampCtx *k)
{
    int minclicks = 20;
    k->cyc_lo = 233; k->cyc_hi = 237; k->cyc_mask = 7;
    k->cyc_count = 0; k->cyc_on = 1;
    k->keyed = 0;
    k->clickx = -1;
    for (;;) {
        if (!camp_ctx_tick(k)) return 0;
        if (minclicks) {
            minclicks--;
            k->keyed = 0;
            k->clickx = -1;
            continue;
        }
        if (k->keyed || k->clickx >= 0 || camp_autopilot)
            break;
    }
    k->cyc_on = 0;
    k->keyed = 0;
    k->clickx = -1;
    return 1;
}

/* ------------------------------------------------- the zooming letter and cursor
 *
 * ScoreScaleClass (score.cpp:436-500). The letter appears 80x80 at the right of the
 * screen and walks in shrinking, one stage per 1/60 s tick:
 *
 *     stage    5     4     3     2     1     0
 *     x      228   180   134   107    80   its own slot
 *     size    80    60    40    30    20   6 (the real glyph)
 *
 * Before drawing stage N the square of stage N+1 is restored from the snapshot --
 * that is score.cpp:463's own commented DOS line (`SysMemPage.Blit(*PseudoSeenBuff,
 * _destx[Stage+1], ...)`; the remaster's TextPrintBuffer/HidPage pair stands in for
 * it). The restore is not optional: those squares sweep straight across the kill and
 * building graphs. The scaler is common/linear.cpp Linear_Scale_To_Linear with
 * trans = true -- nearest neighbour, ratio ((src << 16) / dst) + 1, colour 0 skipped.
 *
 * One faithfully reproduced 1995 artefact, so nobody "fixes" it later: the stage-1
 * square is x 80..99, which overlaps the 11th name cell at x 79..85, so while a
 * letter zooms the 11th character is restored away and only reappears when that
 * letter lands. That is score.cpp's own geometry (_destx[1] == 80 vs
 * HALLFAME_X + 10*6 == 79), not a porting error. */
static const int camp_scale_x[6] = {0, 80, 107, 134, 180, 228};
static const int camp_scale_w[6] = {6, 20, 30, 40, 60, 80};

static void camp_restore(Camp *c, int x, int y, int wide)
{
    int yy;
    for (yy = y; yy < y + wide; yy++) {
        int x0 = x, w0 = wide;
        if (yy < 0 || yy >= 200)
            continue;
        if (x0 < 0) { w0 += x0; x0 = 0; }
        if (x0 + w0 > 320)
            w0 = 320 - x0;
        if (w0 > 0)
            memcpy(&c->plate[yy * 320 + x0], &c->back[yy * 320 + x0], (size_t)w0);
    }
}

static void camp_scale_glyph(Camp *c, const DB_Font *f, char ch, int dx, int dy,
                             int dw, const unsigned char pal[16])
{
    unsigned char cellbuf[8 * 8];
    DB_Surface s;
    char one[2];
    int i, j, ratio, xrat;

    one[0] = ch;
    one[1] = 0;
    db_surface_init(&s, 8, 8, cellbuf); /* score.cpp:466 Fill_Rect 7x7 TBLACK */
    db_print(&s, f, one, 0, 0, pal, 0);
    ratio = ((5 << 16) / dw) + 1;       /* linear.cpp:180-181; the source is 5x5 */
    for (i = 0; i < dw; i++) {
        const int sy = (i * ratio) >> 16, py = dy + i;
        if (py < 0 || py >= 200)
            continue;
        xrat = 0;
        for (j = 0; j < dw; j++) {
            const unsigned char v = cellbuf[sy * 8 + (xrat >> 16)];
            const int px = dx + j;
            xrat += ratio;
            if (px >= 0 && px < 320 && v)
                c->plate[py * 320 + px] = v;
        }
    }
}

/* ScoreClass::Input_Name (score.cpp:1607-1689) plus Animate_Cursor (:1691-1746).
   Returns 0 on Enter, -1 if the window closed. */
static int camp_input_name(CampCtx *k, char *str, int xpos, int ypos,
                           const unsigned char pal[16])
{
    /* What the harness types. It has to be a fixed script, not a timeout: the gates
       have watchdogs, not patience, and a blocking prompt with nobody at the keyboard
       would wedge G14/G15/G16. The 'X' then backspace exercises the erase path too,
       so one gate run covers all three branches; the committed name is "CNC3D".
       Injected one key per loop pass, exactly like Keyboard->Get(), and the loop is
       driven by camp_ctx_tick, which under autopilot advances the synthetic clock by
       exactly one tick per pass -- fully deterministic. */
    static const char autokeys[] = "CNCX\b3D\r";
    /* One key every CAMP_AUTOKEY_HOLD passes, not one per pass. A key on every pass
       re-enters the `index != curlast` branch every time, which resets the blink to
       OFF and means the cursor NEVER lights up -- the harness would drive the name
       entry with the whole Animate_Cursor path unexercised. 8 passes spans more than
       the 5-tick toggle, so a gate run sees the cursor both on and off, and it is
       still a fixed script on a synthetic clock, so it is still deterministic. */
#define CAMP_AUTOKEY_HOLD 8
    static const char autokeys_end = '\r';
    Camp *c = k->c;
    int index = 0, autostep = 0, autohold = 0;
    /* Animate_Cursor's three statics, made locals so two runs of the same screen
       cannot inherit each other's blink phase */
    int curlast = 0, curstate = 0, curtimer = 0;
    int yy;

    memcpy(c->back, c->plate, sizeof c->plate); /* PseudoSeenBuff->Blit(SysMemPage) */
    if (!camp_autopilot)
        SDL_StartTextInput();
    camp_key_clear(c);

    for (;;) {
        int ch, cy = ypos + 7; /* score.cpp:1696, the cursor sits under the letter */

        if (index != curlast) { /* score.cpp:1700-1710 */
            camp_fill(c, xpos + curlast * 6, cy, xpos + curlast * 6 + 5, cy, 0);
            curlast = index;
            curstate = 0;
        }
        camp_fill(c, xpos + index * 6, cy, xpos + index * 6 + 5, cy,
                  curstate ? (unsigned char)DB_LTBLUE : 0);
        if (--curtimer <= 0) { curstate ^= 1; curtimer = 5; } /* score.cpp:1742 */

        if (!camp_ctx_tick(k))
            goto closed;

        if (camp_autopilot) {
            if (++autohold < CAMP_AUTOKEY_HOLD)
                continue;
            autohold = 0;
            ch = autokeys[autostep] ? autokeys[autostep] : autokeys_end;
            autostep++;
        } else {
            ch = camp_key_pop(c); /* one key per pass, like Keyboard->Get() */
        }
        if (!ch)
            continue;
        if (index == CAMP_FAMENAME_LEN - 2)
            camp_key_clear(c); /* score.cpp:1635 flushes on the last slot */

        /* backspace on the LAST slot types a space instead (score.cpp:1645-1648) */
        if (ch == '\b' && index == CAMP_FAMENAME_LEN - 2
            && str[index] && str[index] != ' ')
            ch = ' ';

        if (ch == '\b') {
            if (index) {
                const int x6 = xpos + (--index) * 6;
                str[index] = 0;
                /* the backspace erase is 7 rows tall and the accept erase below is 6.
                   That asymmetry is score.cpp:1652 vs :1666, not a typo here. */
                camp_fill(c, x6, ypos, x6 + 6, ypos + 6, 0);
                for (yy = ypos; yy <= ypos + 6; yy++)
                    memset(&c->back[yy * 320 + x6], 0, 7);
            }
            continue;
        }
        if (ch == '\r')
            break;
        if (ch >= 'a' && ch <= 'z')
            ch -= 'a' - 'A';                                   /* score.cpp:1661 */
        if (!((ch >= '!' && ch <= '~') || ch == ' '))
            continue;                                          /* score.cpp:1663 */

        {
            const int x6 = xpos + index * 6;
            int st;
            camp_fill(c, x6, ypos, x6 + 6, ypos + 5, 0);
            for (yy = ypos; yy <= ypos + 5; yy++)
                memset(&c->back[yy * 320 + x6], 0, 7);
            str[index] = (char)ch;
            str[index + 1] = 0;
            camp_sfx(c, "KEYSTROK.AUD", 255); /* score.cpp:1677, full volume */

            /* score.cpp:1679-1680 BLOCKS on the zoom, and Call_Back_Delay runs
               Animate_Score_Objs but NOT Animate_Cursor -- so the cursor is frozen
               for these six ticks. Reproduced by not touching it in here. */
            for (st = 5; st >= 0; st--) {
                if (st != 5)
                    camp_restore(c, camp_scale_x[st + 1], ypos, camp_scale_w[st + 1]);
                if (st)
                    camp_scale_glyph(c, k->sf, (char)ch, camp_scale_x[st], ypos,
                                     camp_scale_w[st], pal);
                else
                    camp_glyph(c, k->sf, (char)ch, x6, ypos, pal);
                if (!camp_ctx_tick(k))
                    goto closed;
            }
            if (index < CAMP_FAMENAME_LEN - 2)
                index++;
        }
    }
    /* leave no cursor behind */
    camp_fill(c, xpos + index * 6, ypos + 7, xpos + index * 6 + 5, ypos + 7, 0);
    if (!camp_autopilot)
        SDL_StopTextInput();
    camp_key_clear(c);
    k->keyed = 0;
    return 0;

closed:
    if (!camp_autopilot)
        SDL_StopTextInput();
    camp_key_clear(c);
    return -1;
}

int camp_score(Camp *c, const CampScore *s, int side, int scenario)
{
    /* The spinning faction logo belongs to THIS screen only; camp_draw serves all
       three, so the other two turn it off again on the way in. */
    c->logo_side = side;
    c->logo_t0 = camp_now(c);

    /* score.cpp's own per-house layout tables */
    static const int casuax[2] = {144, 146}, casuay[2] = {78, 90};
    static const int gditxx[2] = {150, 224}, gditxy[2] = {90, 90};
    static const int nodtxx[2] = {150, 224}, nodtxy[2] = {102, 102};
    static const int bldggy[2] = {138, 128}, bldgny[2] = {150, 140};

    /* BOTH FACTIONS WEAR NOD'S SCORE SCREEN. Reported: "Apply the Nod
       Scorescreen to GDI mission (with the GDI music and emblem), and we'll test
       that out."

       GDI's own plate, SCORE_G (S-GDIIN2.WSA), paints a gold medal into its top-left
       over frames 10..24 of its assembly animation. Nod's SCRSCN1.WSA never does, so
       its corner is clean and the spinning model has it to itself. Rather than carve
       the medal back out of GDI's plate -- which is what the ellipse and then the box
       did, and what put a notch in the grid -- GDI simply takes the plate that never
       had one.

       WHAT THIS COSTS, stated because it is not free: the two plates are not the same
       picture. They differ in 15,808 of 64,000 pixels, 7,602 of them outside the
       medal. GDI's carries four casualty bars, a gun and a ship that Nod's has not,
       runs its grid differently and puts the eye icon elsewhere. score.cpp lays every
       label out per house to match its own plate, so the layout has to move with the
       art or the numbers land on the wrong things -- hence `house` below is pinned to
       Nod's, not derived from `side`.

       WHAT STAYS THE PLAYER'S OWN: the music (WIN1 against NOD_WIN1) and the emblem
       that turns in the corner (BRF_LOGO_GDI against BRF_LOGO_NOD). */
    const CampEntry *bg = camp_entry(c, "SCORE_N");
    const CampEntry *timeshp = camp_entry(c, "TIME");
    const CampEntry *hi1 = camp_entry(c, "HISCORE1");
    const CampEntry *hi2 = camp_entry(c, "HISCORE2");
    const CampEntry *yellow = camp_entry(c, "BAR3YLW");
    const CampEntry *red = camp_entry(c, "BAR3RED");
    /* Nod's, for both, because the plate above is Nod's. This picks the label
       coordinates AND which elements the screen draws at all, so it must follow the
       art rather than the player. `side` still decides music and emblem. */
    const int house = 1;
    const int total = s->score;
    const int minutes = s->minutes > 0 ? s->minutes : 1; /* score.cpp:659's floor */
    CampCtx k;
    int i, frame, max, scorecounter;

    if (!bg) return 0;

    printf("CAMPAIGN|score|scen=%d|score=%d|leadership=%d|efficiency=%d|minutes=%d\n",
           scenario, s->score, s->leadership, s->efficiency, minutes);
    fflush(stdout);

    camp_ctx_init(&k, c);
    memset(c->plate, 0, sizeof(c->plate));
    camp_use_pal(c, bg->pal);
    c->fade = 0;
    /* score.cpp:733 Hide_Mouse, and it is never undone inside the screen -- see
       camp_show_mouse's block comment. The score screen is pointerless throughout,
       tally, hall of fame and name entry alike. */
    camp_show_mouse(c, 0);

    /* Theme.Queue_Song(THEME_WIN1) -- score.cpp:639. WIN1 is a Repeat=1 theme the
       1995 table only ever plays by name.

       NOD GETS ITS OWN, which 1995 never did. score.cpp:639 queues THEME_WIN1 for both
       houses even though the same function branches on house for the palette and the
       background art, so NOD_WIN1.AUD -- 372,405 bytes of it, sitting in the shipped
       SCORES.MIX -- was written, pressed and never played. It is in no theme table and
       referenced by no code in the 1995 source. It was asked to be restored.
       A DELIBERATE DEVIATION from 1995, and registered as such. */
    if (c->au) {
        cnc_music_stop(c->au);
        cnc_music_play_theme(c->au, side ? "NOD_WIN1" : "WIN1", 1);
    }

    /* frame 1 up, fade from black (FADE_PALETTE_FAST = 60/8 ticks), COUNTRY4 */
    camp_blit(c, bg, 1);
    k.fade_step = 256 / 7 + 1;
    if (!camp_ctx_delay(&k, 7)) goto closed;
    camp_sfx(c, "COUNTRY4.AUD", 90);

    /* the background assembles at Call_Back_Delay(2) (score.cpp:747-751) */
    for (frame = 1; frame < bg->frames; frame++) {
        camp_blit(c, bg, frame);
        if (!camp_ctx_delay(&k, 2)) goto closed;
    }

    /* the three looping shapes (score.cpp:759-765) */
    camp_ctx_anim(&k, timeshp, 233, 2, 30, 4, 0);
    camp_ctx_anim(&k, hi1, 4, 97, 10, 4, 0);
    camp_ctx_anim(&k, hi2, 8, 172, 10, 4, 0);

    /* labels type in green; SFX4; then the count-up (score.cpp:786-822) */
    camp_ctx_type(&k, TXT_SCORE_TIME, 206, 3, camp_sc_greenpal);
    camp_ctx_type(&k, TXT_SCORE_LEAD, 182, 26, camp_sc_greenpal);
    camp_ctx_type(&k, TXT_SCORE_EFFI, 182, 38, camp_sc_greenpal);
    camp_ctx_type(&k, TXT_SCORE_TOTA, 182, 50, camp_sc_greenpal);
    camp_sfx(c, "SFX4.AUD", 120);
    if (!camp_ctx_delay(&k, 13)) goto closed;

    max = s->leadership > s->efficiency ? s->leadership : s->efficiency;
    scorecounter = 0;
    k.keyed = 0; /* Keyboard->Clear() */
    for (i = 0; i <= 160; i++) {
        camp_countup(&k, "%3d%%", i, s->leadership, 264, 26, camp_sc_greenpal);
        if (i >= 30)
            camp_countup(&k, "%3d%%", i - 30, s->efficiency, 264, 38, camp_sc_greenpal);
        if (i >= 60) {
            camp_countup(&k, "%3d", scorecounter, total, 264, 50, camp_sc_greenpal);
            scorecounter += total / 100;
        }
        if (i == 0)
            camp_minutes(&k, minutes, camp_sc_greenpal);
        if (!camp_ctx_delay(&k, 1)) goto closed;
        camp_sfx(c, "BEEPY6.AUD", 60);
        if (k.keyed && i < max - 5) {
            i = 158; /* score.cpp:815's skip */
            k.keyed = 0;
        }
    }
    camp_countup(&k, "%3d", total, total, 264, 50, camp_sc_greenpal);

    if (!camp_ctx_delay(&k, 60)) goto closed;
    if (house == 1 && !camp_credits(&k, house, s->credits, camp_sc_greenpal))
        goto closed;
    if (!camp_ctx_delay(&k, 60)) goto closed;

    /* casualties (score.cpp:832-850) */
    camp_sfx(c, "SFX4.AUD", 90);
    camp_ctx_type(&k, TXT_SCORE_CASU, casuax[house], casuay[house], camp_sc_redpal);
    if (!camp_ctx_delay(&k, 9)) goto closed;
    if (house == 1) {
        camp_ctx_type(&k, TXT_SCORE_NEUT, 200, 114, camp_sc_redpal);
        if (!camp_ctx_delay(&k, 4)) goto closed;
    }
    camp_ctx_type(&k, TXT_SCORE_GDI, gditxx[house], gditxy[house], camp_sc_redpal);
    camp_ctx_type(&k, TXT_SCORE_NOD, nodtxx[house], nodtxy[house], camp_sc_redpal);
    if (!camp_ctx_delay(&k, 6)) goto closed;

    if (house == 0) {
        if (!camp_gdi_graph(&k, yellow, red, s->gdi_killed + s->civ_killed,
                            s->nod_killed, 88, camp_sc_redpal))
            goto closed;
    } else {
        /* the Nod casualty tableau (E1/C1 infantrymen posing for the bars) is a
           registered gap. The numbers still land. */
        fprintf(stderr, "campaign: Nod casualties graph is a registered gap\n");
        camp_countup(&k, "%d", s->gdi_killed, s->gdi_killed, 248, 90, camp_sc_redpal);
        camp_countup(&k, "%d", s->nod_killed, s->nod_killed, 248, 102, camp_sc_redpal);
        camp_countup(&k, "%d", s->civ_killed, s->civ_killed, 248, 114, camp_sc_redpal);
    }

    /* buildings destroyed (score.cpp:857-878) */
    camp_sfx(c, "SFX4.AUD", 90);
    if (house == 0) {
        camp_ctx_type(&k, TXT_SCORE_BUIL, 144, 126, camp_sc_greenpal);
        if (!camp_ctx_delay(&k, 9)) goto closed;
    } else {
        camp_ctx_type(&k, TXT_SCORE_BUIL, 146, 128, camp_sc_greenpal);
        if (!camp_ctx_delay(&k, 9)) goto closed;
    }
    camp_ctx_type(&k, TXT_SCORE_GDI, gditxx[house], bldggy[house], camp_sc_greenpal);
    camp_ctx_type(&k, TXT_SCORE_NOD, gditxx[house], bldgny[house], camp_sc_greenpal);
    if (!camp_ctx_delay(&k, 7)) goto closed;

    if (house == 0) {
        if (!camp_gdi_graph(&k, yellow, red, s->gdi_bldg + s->civ_bldg,
                            s->nod_bldg, 136, camp_sc_greenpal))
            goto closed;
    } else {
        fprintf(stderr, "campaign: Nod buildings graph is a registered gap\n");
        camp_countup(&k, "%d", s->gdi_bldg, s->gdi_bldg, 264, 128, camp_sc_greenpal);
        camp_countup(&k, "%d", s->nod_bldg, s->nod_bldg, 264, 140, camp_sc_greenpal);
        camp_countup(&k, "%d", s->civ_bldg, s->civ_bldg, 264, 152, camp_sc_greenpal);
    }

    while (camp_ctx_typing(&k)) /* score.cpp:881 StillUpdating */
        if (!camp_ctx_delay(&k, 1)) goto closed;
    k.keyed = 0;

    if (house == 0 && !camp_credits(&k, house, s->credits, camp_sc_greenpal))
        goto closed;

    /* ------------------------------------------------------------- hall of fame
     * score.cpp:888-995, transcribed. This never needed a save system: the fame
     * table is a 140-byte standalone file (camp_fame_load), and it needed no bake
     * either -- SCOREFNT, SCORE_G's own blue ramp and KEYSTROK.AUD all shipped
     * already. */
    {
        CampFame fame[CAMP_NUMFAMENAMES];
        char row[32];
        int idx, j;

        camp_sfx(c, "SFX4.AUD", 90);                        /* score.cpp:894 */
        camp_ctx_type(&k, TXT_SCORE_TOP, 28, 110, camp_sc_bluepal);
        if (!camp_ctx_delay(&k, 9)) goto closed;

        camp_fame_load(fame);

        /* score.cpp:928-942. Read the first line twice: if the WORST row is at least
           as good as this run its score is ZEROED, so `total > score` always finds a
           slot. In Tiberian Dawn every winning run gets to type a name; the CLICK TO
           CONTINUE branch below is only reachable with a score of 0, which
           TD_Calculate_Score (dllinterface.cpp:2280-2288, floor 46) cannot produce. */
        idx = CAMP_NUMFAMENAMES;
        if (fame[CAMP_NUMFAMENAMES - 1].score >= total)
            fame[CAMP_NUMFAMENAMES - 1].score = 0;
        for (j = 0; j < CAMP_NUMFAMENAMES; j++)
            if (total > fame[j].score) {
                int m;
                for (m = CAMP_NUMFAMENAMES - 1; m > j; m--)
                    fame[m] = fame[m - 1];
                fame[j].score = total;
                fame[j].level = scenario;                   /* Scen.Scenario */
                memset(fame[j].name, ' ', CAMP_FAMENAME_LEN - 1);
                fame[j].name[CAMP_FAMENAME_LEN - 1] = 0;
                idx = j;
                break;
            }

        /* score.cpp:946-963: name at HALLFAME_X (19), level at +6*12 (91), score at
           +6*15 (109), rows 8 apart from y = 120. The 13-tick beat is INSIDE the
           score-is-nonzero test, so empty rows go up with no pause at all. */
        for (j = 0; j < CAMP_NUMFAMENAMES; j++) {
            camp_ctx_type_str(&k, fame[j].name, 19, 120 + j * 8, camp_sc_bluepal);
            if (fame[j].score) {
                snprintf(row, sizeof row, "%d", fame[j].score);
                camp_ctx_type_str(&k, row, 19 + 6 * 15, 120 + j * 8, camp_sc_bluepal);
                if (fame[j].level < 20) snprintf(row, sizeof row, "%d", fame[j].level);
                else                    snprintf(row, sizeof row, "**");
                camp_ctx_type_str(&k, row, 19 + 6 * 12, 120 + j * 8, camp_sc_bluepal);
                if (!camp_ctx_delay(&k, 13)) goto closed;
            }
        }
        while (camp_ctx_typing(&k))                         /* score.cpp:966-969 */
            if (!camp_ctx_delay(&k, 1)) goto closed;
        k.keyed = 0;                                        /* Keyboard->Clear() */

        /* No font means no name entry: print nothing rather than substitute, the
           same policy the rest of this file follows. */
        if (idx < CAMP_NUMFAMENAMES && k.sf) {
            if (camp_input_name(&k, fame[idx].name, 19, 120 + idx * 8,
                                camp_sc_bluepal) < 0)
                goto closed;
            camp_fame_save(fame);
            printf("CAMPAIGN|fame|rank=%d|name=%s|score=%d|level=%d\n",
                   idx + 1, fame[idx].name, fame[idx].score, fame[idx].level);
            fflush(stdout);
            /* score.cpp:975-983 -- Enter both commits the name AND leaves the
               screen. There is no CLICK TO CONTINUE on this path and no
               Cycle_Wait_Click, which is 1995 and is a visible change. */
        } else {
            /* A slot was FOUND but there is no font to type a name into it. 1995
               (score.cpp:973-985) saves whenever a slot was found, so saving the
               shifted table with its default name is the faithful half of what we
               can still do; silently dropping the entry would lose a real score to
               a presentation problem. The name entry itself is skipped rather than
               substituted, the same policy the rest of this file follows. */
            if (idx < CAMP_NUMFAMENAMES) {
                camp_fame_save(fame);
                printf("CAMPAIGN|fame|rank=%d|name=%s|score=%d|level=%d|nofont\n",
                       idx + 1, fame[idx].name, fame[idx].score, fame[idx].level);
            } else {
                printf("CAMPAIGN|fame|none\n");
            }
            fflush(stdout);
            camp_ctx_type(&k, TXT_MAP_CLICK2, 149, 190, camp_sc_yellowpal);
            while (camp_ctx_typing(&k))
                if (!camp_ctx_delay(&k, 1)) goto closed;
            if (!camp_cycle_wait_click(&k)) goto closed;
        }
        k.keyed = 0;                                        /* score.cpp:997 */

        {
            /* photograph the FINISHED board with the name on it -- that is the whole
               point of the feature, and a shot taken before the entry would be the
               same picture the old build produced */
            static int shot_once = 0;
            camp_shot(c, "flow_score.png", &shot_once);
        }
    }

    /* Theme.Queue_Song(THEME_NONE); fade to black FAST (score.cpp:1001-1009) */
    k.fade_step = -(256 / 7 + 1);
    if (!camp_ctx_delay(&k, 7)) goto closed;
    if (c->au)
        cnc_music_stop(c->au);
    c->fade = 256;
    return 0;

closed:
    if (c->au)
        cnc_music_stop(c->au);
    c->fade = 256;
    return -1;
}

/* ------------------------------------------------------------------ map selection */

/* mapsel.cpp's CountryArray, transcribed IN FULL: indexed [scenario][ScenDir], the
   direction chosen the LAST time around (column 0 = EAST, 1 = WEST -- defines.h:728
   SCEN_DIR_EAST is 0). Rows 1..14 are the GDI campaign, 15..26 Nod (row = 14 +
   scenario; the Nod screens themselves are a registered gap until CD-2's reels are
   staged, but the table is complete so they cost one bake when that lands).
   dir/var are stored as the letters the scenario namer wants; 0 = no choice. */
typedef struct MapRow {
    unsigned char choices[2];
    short start[2], cont[2];
    unsigned char colour[2][3];
    unsigned char shape[2][3];
    char dir[2][3], var[2][3];
} MapRow;

static const MapRow camp_country[27] = {
    /*  0 */ {{0, 0}, {0, 0}, {0, 0}, {{0}}, {{0}}, {{0}}, {{0}}},
    /* -------- GDI -------- */
    /*  1 */ {{1, 1}, {0, 0}, {3, 3},
              {{0x95, 0, 0}, {0x95, 0, 0}}, {{17, 0, 0}, {17, 0, 0}},
              {{'E', 0, 0}, {0, 0, 0}}, {{'A', 0, 0}, {0, 0, 0}}},
    /*  2 */ {{1, 1}, {16, 16}, {19, 19},
              {{0x80, 0, 0}, {0x80, 0, 0}}, {{0, 0, 0}, {0, 0, 0}},
              {{'E', 0, 0}, {0, 0, 0}}, {{'A', 0, 0}, {0, 0, 0}}},
    /*  3 */ {{3, 3}, {32, 32}, {35, 35},
              {{0x81, 0x82, 0x83}, {0x81, 0x82, 0x83}}, {{3, 3, 1}, {3, 3, 1}},
              {{'W', 'W', 'E'}, {0, 0, 0}}, {{'A', 'B', 'A'}, {0, 0, 0}}},
    /*  4 */ /* DELIBERATE DEVIATION FROM THE 1995 TABLE, and the only one in here.
                 mapsel.cpp's own row 4 reads {{SVA, SVA, SVN}, {SVA, SVB, SVN}}: the two
                 EAST choices both name variant A, so both click areas lead to SCG05EA and
                 SCG05EB -- which ships, and whose INI and BIN are installed beside the
                 rest -- can never be reached. The WEST column of the same row is A then
                 B, which is what makes the East column read as a typo rather than a
                 design. Our transcription WAS faithful; this changes it on purpose so the
                 second East territory leads somewhere new. Recorded as a known gap;
                 revert the 'B' below to 'A' to go back to the original's behaviour. */
             {{2, 2}, {48, 64}, {51, 67},
              {{0x84, 0x85, 0}, {0x86, 0x87, 0}}, {{4, 4, 0}, {2, 2, 0}},
              {{'E', 'E', 0}, {'W', 'W', 0}}, {{'A', 'B', 0}, {'A', 'B', 0}}},
    /*  5 */ {{2, 2}, {99, 99}, {102, 102},
              {{0x88, 0x89, 0}, {0x88, 0x89, 0}}, {{7, 7, 0}, {7, 7, 0}},
              {{'E', 'E', 0}, {'E', 'E', 0}}, {{'A', 'A', 0}, {'A', 'A', 0}}},
    /*  6 */ {{2, 2}, {80, 83}, {86, 86},
              {{0x88, 0x89, 0}, {0x88, 0x89, 0}}, {{7, 7, 0}, {7, 7, 0}},
              {{'E', 'E', 0}, {'E', 'E', 0}}, {{'A', 'A', 0}, {'A', 'A', 0}}},
    /*  7 */ {{2, 2}, {115, 0}, {118, 0},
              {{0x8B, 0x8A, 0}, {0x8B, 0x8A, 0}}, {{6, 8, 0}, {6, 8, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /*  8 */ {{1, 1}, {131, 0}, {134, 0},
              {{0x8C, 0, 0}, {0x8C, 0, 0}}, {{9, 0, 0}, {9, 0, 0}},
              {{'E', 0, 0}, {0, 0, 0}}, {{'A', 0, 0}, {0, 0, 0}}},
    /*  9 */ {{2, 1}, {147, 0}, {150, 0},
              {{0x8D, 0x8E, 0}, {0, 0, 0}}, {{10, 13, 0}, {0, 0, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /* 10 */ {{1, 1}, {163, 0}, {166, 0},
              {{0x8F, 0, 0}, {0, 0, 0}}, {{16, 0, 0}, {0, 0, 0}},
              {{'E', 0, 0}, {0, 0, 0}}, {{'A', 0, 0}, {0, 0, 0}}},
    /* 11 */ {{2, 1}, {179, 0}, {182, 0},
              {{0x90, 0x91, 0}, {0, 0, 0}}, {{14, 15, 0}, {0, 0, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /* 12 */ {{2, 1}, {195, 0}, {198, 0},
              {{0x92, 0x93, 0}, {0, 0, 0}}, {{12, 12, 0}, {0, 0, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /* 13 */ {{1, 1}, {211, 0}, {214, 0},
              {{0x93, 0, 0}, {0, 0, 0}}, {{12, 0, 0}, {0, 0, 0}},
              {{'E', 0, 0}, {0, 0, 0}}, {{'A', 0, 0}, {0, 0, 0}}},
    /* 14 */ {{3, 1}, {0, 0}, {3, 0},
              {{0x81, 0x82, 0x83}, {0, 0, 0}}, {{0, 0, 0}, {0, 0, 0}},
              {{'E', 'E', 'E'}, {0, 0, 0}}, {{'A', 'B', 'C'}, {0, 0, 0}}},
    /* -------- Nod (rows 14 + scenario) -------- */
    /*  1 */ {{2, 1}, {0, 0}, {3, 0},
              {{0x80, 0x81, 0}, {0, 0, 0}}, {{4, 4, 0}, {0, 0, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /*  2 */ {{2, 1}, {16, 0}, {19, 0},
              {{0x82, 0x83, 0}, {0, 0, 0}}, {{6, 6, 0}, {0, 0, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /*  3 */ {{2, 1}, {32, 0}, {35, 0},
              {{0x84, 0x85, 0}, {0, 0, 0}}, {{5, 5, 0}, {0, 0, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /*  4 */ {{1, 1}, {48, 0}, {51, 0},
              {{0x86, 0, 0}, {0, 0, 0}}, {{0, 0, 0}, {0, 0, 0}},
              {{'E', 0, 0}, {0, 0, 0}}, {{'A', 0, 0}, {0, 0, 0}}},
    /*  5 */ {{3, 1}, {64, 0}, {67, 0},
              {{0x87, 0x88, 0x89}, {0, 0, 0}}, {{1, 2, 3}, {0, 0, 0}},
              {{'E', 'E', 'E'}, {0, 0, 0}}, {{'A', 'B', 'C'}, {0, 0, 0}}},
    /*  6 */ {{3, 1}, {80, 0}, {83, 0},
              {{0x8A, 0x8B, 0x8C}, {0, 0, 0}}, {{9, 7, 8}, {0, 0, 0}},
              {{'E', 'E', 'E'}, {0, 0, 0}}, {{'A', 'B', 'C'}, {0, 0, 0}}},
    /*  7 */ {{2, 1}, {96, 0}, {99, 0},
              {{0x8D, 0x8E, 0}, {0, 0, 0}}, {{10, 10, 0}, {0, 0, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /*  8 */ {{1, 1}, {112, 0}, {115, 0},
              {{0xA0, 0, 0}, {0, 0, 0}}, {{4, 4, 0}, {0, 0, 0}},
              {{'E', 0, 0}, {0, 0, 0}}, {{'A', 0, 0}, {0, 0, 0}}},
    /*  9 */ {{2, 1}, {128, 0}, {131, 0},
              {{0x8F, 0x90, 0}, {0, 0, 0}}, {{11, 15, 0}, {0, 0, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /* 10 */ {{2, 1}, {144, 0}, {147, 0},
              {{0x91, 0x92, 0}, {0, 0, 0}}, {{12, 16, 0}, {0, 0, 0}},
              {{'E', 'E', 0}, {0, 0, 0}}, {{'A', 'B', 0}, {0, 0, 0}}},
    /* 11 */ {{1, 1}, {160, 0}, {163, 0},
              {{0x93, 0, 0}, {0, 0, 0}}, {{13, 0, 0}, {0, 0, 0}},
              {{'E', 0, 0}, {0, 0, 0}}, {{'A', 0, 0}, {0, 0, 0}}},
    /* 12 */ {{3, 1}, {0, 0}, {3, 0},
              {{0x81, 0x82, 0x83}, {0, 0, 0}}, {{14, 0, 0}, {0, 0, 0}},
              {{'E', 'E', 'E'}, {0, 0, 0}}, {{'A', 'B', 'C'}, {0, 0, 0}}},
};

/* the mapsel screen's green, mapsel.cpp:499 (slots 0/1 zeroed, see camp_print16) */
static const unsigned char camp_map_greenpal[16] = {
    0x00, 0x00, 0x42, 0x43, 0x44, 0x44, 0x44, 0x44,
    0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44};

/* Bit_It_In_Scale (mapsel.cpp:1534): the shuffled-pixel dissolve that brings each
   territory advance in. Row by row with a shifting column phase, one delay tick
   every other row, plus the two "dagger" diagonals from (160,0). The shuffle RNG is
   a fixed-seed LCG: deterministic, no wallclock, same dissolve every run. */
static int camp_dissolve(CampCtx *k, const unsigned char *src, int delay, int dagger)
{
    short xindex[320], yindex[200];
    unsigned rng = 0x1995C0DEu;
    int i, j, n, kk;

    for (i = 0; i < 320; i++) xindex[i] = (short)i;
    for (i = 0; i < 200; i++) yindex[i] = (short)i;
    for (i = 0; i < 320; i++) {
        rng = rng * 1103515245u + 12345u;
        kk = (int)((rng >> 16) % 320);
        n = xindex[kk]; xindex[kk] = xindex[i]; xindex[i] = (short)n;
    }
    for (i = 0; i < 200; i++) {
        rng = rng * 1103515245u + 12345u;
        kk = (int)((rng >> 16) % 200);
        n = yindex[kk]; yindex[kk] = yindex[i]; yindex[i] = (short)n;
    }

    for (j = 0; j < 200; j++) {
        int j1 = j;
        if (j & 1) {
            i = delay;
            do {
                if (!camp_ctx_delay(k, i ? 1 : 0)) return 0;
            } while (i--);
        }
        for (i = 0; i < 320; i++) {
            const int x = xindex[i], y = yindex[j1];
            j1++;
            if (j1 >= 200) j1 = 0;
            k->c->plate[y * 320 + x] = src[y * 320 + x];
        }
        if (dagger)
            for (kk = j; kk >= 0; kk--) {
                const int d = j - kk;
                if (160 - d >= 0)
                    k->c->plate[kk * 320 + 160 - d] = src[kk * 320 + 160 - d];
                if (160 + d < 320)
                    k->c->plate[kk * 320 + 160 + d] = src[kk * 320 + 160 + d];
            }
    }
    return 1;
}

int camp_mapsel(Camp *c, int side, int scenario, char *dir, char *var)
{
    c->logo_side = -1;          /* no logo on the globe */
    const CampEntry *grey = camp_entry(c, "GREYERTH");
    const CampEntry *ebw = camp_entry(c, "E-BWTOCL");
    const CampEntry *earth = camp_entry(c, side ? "EARTH_A" : "EARTH_E");
    const CampEntry *europe = camp_entry(c, side ? "AFRICA" : "EUROPE");
    const CampEntry *click = camp_entry(c, side ? "CLICK_A" : "CLICK_E");
    CampCtx k;
    int d, f, q, startframe, contframe, sel = -1, appick = 0;
    /* mapsel.cpp:516-520 ends GDI at 14 and Nod at 12; :514 offsets Nod's row
       by 14, and all 27 rows are already transcribed in camp_country. */
    const int lastscen = side ? 12 : 14;
    const int rowbase = side ? 14 : 0;

    *dir = 'E';
    *var = 'A';
    /* NOD HAS ITS OWN REELS AS OF 26 Aug 2026. This was `if (side != 0) return 0`
       with a note calling them a registered gap needing CD-2. They were never
       missing: EARTH_A.WSA, AFRICA.WSA and CLICK_A.CPS sit in the GENERAL.MIX we
       already ship, beside the GDI three, and nobody had looked. Reported:
       "There are no NOD map screens between any NOD campaign missions currently."
       Only the three that name a continent differ: GREYERTH and E-BWTOCL are
       shared, being the same grey globe and the same fade to colour. */

    if (!grey || !ebw || !earth || !europe || !click)
        return 0;

    const MapRow *row = (scenario >= 1 && scenario <= lastscen)
                        ? &camp_country[rowbase + scenario] : NULL;
    d = (c->mapdir == 'W') ? 1 : 0; /* ScenDir: the direction chosen LAST time */
    if (!row || row->choices[d] == 0)
        return 0;
    if (scenario >= lastscen) {
        /* lastscenario: 1995 swaps EUROPE for the BOSNIA reel and CLICK_EB; that
           reel is not baked (registered gap) and cnc3d.cpp ends the campaign after
           15 anyway. Hand back choice 0 so the flow stays whole. */
        fprintf(stderr, "campaign: last-scenario BOSNIA reel is a registered gap\n");
        *dir = row->dir[d][0] ? row->dir[d][0] : 'E';
        *var = row->var[d][0] ? row->var[d][0] : 'A';
        c->mapdir = *dir;
        return 0;
    }

    printf("CAMPAIGN|mapsel|scen-won=%d|choices=%d\n", scenario, row->choices[d]);
    fflush(stdout);

    camp_ctx_init(&k, c);
    camp_show_mouse(c, 0); /* the globe spin-in is a hidden-mouse phase in 1995 */

    /* Theme.Queue_Song(THEME_MAP1) -- mapsel.cpp:529, and the same Nod restoration as
       the score screen above.

       GDI's MAP1 is fine and I briefly thought it was not: it is not in any MIX under
       playable/dosdata, but it does not need to be -- playable/dosdata/music/ holds
       fifteen loose .AUD files and MAP1.AUD and WIN1.AUD are two of them. Checking the
       archives and not the directory beside them is how that mistake was made. */
    if (c->au) {
        cnc_music_stop(c->au);
        cnc_music_play_theme(c->au, side ? "NOD_MAP1" : "MAP1", 1);
    }

    /* --- the grey globe fades in (mapsel.cpp:595-613): APPEAR1, then frames at
       Call_Back_Delay(4). (1995 "dissolves" frame 0 in while the palette is still
       black -- invisible by construction -- then fades the palette in MEDIUM.) */
    memset(c->plate, 0, sizeof(c->plate));
    camp_blit(c, grey, 0);
    c->fade = 0;
    camp_sfx(c, "APPEAR1.AUD", 110);
    k.fade_step = 256 / 15 + 1; /* FADE_PALETTE_MEDIUM = 60/4 ticks */
    if (!camp_ctx_delay(&k, 15)) goto closed;
    for (f = 1; f < grey->frames; f++) {
        if (!camp_ctx_delay(&k, 4)) goto closed;
        camp_blit(c, grey, f);
    }

    /* --- grey to colour (mapsel.cpp:616-631) */
    if (!camp_ctx_delay(&k, 4)) goto closed;
    camp_blit(c, ebw, 0);
    if (!camp_ctx_delay(&k, 4)) goto closed;
    for (f = 1; f < ebw->frames; f++) {
        camp_blit(c, ebw, f);
        if (!camp_ctx_delay(&k, 4)) goto closed;
    }

    /* --- EARTH_E: the globe spins to Europe and the grid appears, with the DOS
       sound score keyed to frame numbers (mapsel.cpp:656-746). The READING IMAGE
       DATA overlays in that block are factor>1, i.e. C&C95 hi-res only. */
    camp_blit(c, earth, 1);
    camp_sfx(c, "SFX4.AUD", 130);
    camp_sfx(c, "TEXT2.AUD", 90);
    for (f = 1; f < earth->frames; f++) {
        if (f == 16 || f == 33 || f == 44 || f == 70 || f == 73)
            camp_sfx(c, "TEXT2.AUD", 90);
        if (f == 21 || f == 27)
            camp_sfx(c, "TARGET1.AUD", 90);
        if (f == 45 || f == 47 || f == 49)
            camp_sfx(c, "BEEPY6.AUD", 90);
        if (f == 51)
            camp_sfx(c, "WORLD2.AUD", 90);
        if (f == 70 || f == 72)
            camp_sfx(c, "BEEPY2.AUD", 90);
        if (f == 74)
            camp_sfx(c, "TARGET2.AUD", 110);
        camp_blit(c, earth, f);
        if (!camp_ctx_delay(&k, 3)) goto closed;
    }
    {
        static int shot_once = 0;
        camp_shot(c, "flow_globe.png", &shot_once);
    }

    /* --- freeze on Europe; jump to the territory state as of LAST scenario
       (mapsel.cpp:770-790) */
    camp_blit(c, europe, 0);
    startframe = row->start[d];
    if (startframe)
        camp_blit(c, europe, startframe);
    if (!camp_ctx_delay(&k, 45)) goto closed;

    /* --- first advance of territories: GDI PROGRESSION types, COUNTRY1, dissolve
       (mapsel.cpp:796-822) */
    camp_sfx(c, "TEXT2.AUD", 90);
    camp_ctx_type(&k, TXT_MAP_GDI, 0, 2, camp_map_greenpal);
    if (!camp_ctx_delay(&k, 60)) goto closed;
    camp_sfx(c, "COUNTRY1.AUD", 90);
    if (!camp_dissolve(&k, camp_frame(c, europe, startframe + 1), 1, 1)) goto closed;
    if (!camp_ctx_delay(&k, 85)) goto closed;
    camp_fill(c, 0, 0, 96, 8, DB_BLACK);

    /* --- second advance: NOD PROGRESSION (mapsel.cpp:823-847) */
    camp_sfx(c, "TEXT2.AUD", 90);
    camp_ctx_type(&k, TXT_MAP_NOD, 0, 12, camp_map_greenpal);
    if (!camp_ctx_delay(&k, 65)) goto closed;
    camp_sfx(c, "COUNTRY1.AUD", 90);
    if (!camp_dissolve(&k, camp_frame(c, europe, startframe + 2), 1, 1)) goto closed;
    if (!camp_ctx_delay(&k, 85)) goto closed;
    camp_fill(c, 0, 12, 96, 20, DB_BLACK);

    /* --- the crosshair reel at ContAnim (mapsel.cpp:850-990): LOCATING
       COORDINATES / OF NEXT MISSION type while 13 frames run at Call_Back_Delay(6),
       BEEPY3 at 0 and 2, NEWTARG1 at 6, frames 3..4 replayed four extra times. */
    contframe = row->cont[d];
    camp_sfx(c, "TEXT2.AUD", 90);
    camp_ctx_type(&k, TXT_MAP_LOCATE, 0, 160, camp_map_greenpal);
    if (!camp_ctx_delay(&k, 20)) goto closed;
    camp_ctx_type(&k, TXT_MAP_NEXT_MISSION, 0, 170, camp_map_greenpal);
    if (!camp_ctx_delay(&k, 50)) goto closed;

    q = 0;
    for (f = 0; f < 13; f++) {
        if (f == 0 || f == 2)
            camp_sfx(c, "BEEPY3.AUD", 90);
        if (f == 6)
            camp_sfx(c, "NEWTARG1.AUD", 90);
        camp_blit(c, europe, contframe + f);
        if (!camp_ctx_delay(&k, 6)) goto closed;
        if (f == 4 && q < 4) { /* cycle the country flash a little while */
            f = 2;
            q++;
        }
    }

    /* --- SELECT TERRITORY / TO ATTACK over the blink frame. The blink is the
       palette: the ContAnim+12 frame paints the choice territories in slots
       249..254 and Cycle_Call_Back_Delay rotates those every 4th tick. */
    camp_sfx(c, "BEEPY6.AUD", 90);
    camp_fill(c, 0, 160, 120, 176, DB_BLACK);
    k.cyc_lo = 249; k.cyc_hi = 254; k.cyc_mask = 3;
    k.cyc_count = 0; k.cyc_on = 1;
    camp_ctx_type(&k, TXT_MAP_SELECT, 0, 160, camp_map_greenpal);
    if (!camp_ctx_delay(&k, 16)) goto closed;
    camp_ctx_type(&k, TXT_MAP_TO_ATTACK, 0, 170, camp_map_greenpal);
    if (!camp_ctx_delay(&k, 24)) goto closed;

    /* mapsel.cpp:1030-1031, right after "SELECT TERRITORY TO ATTACK" prints. Must
       precede the selection loop so the flow_map.png shot taken inside it carries
       the arrow. */
    camp_show_mouse(c, 1);
    k.clickx = -1;
    k.keyed = 0;
    while (sel < 0) {
        if (!camp_ctx_tick(&k)) goto closed;
        if (camp_autopilot && !camp_ctx_typing(&k) && ++appick > 40 && k.clickx < 0) {
            /* hands-off: click the first pixel of choice 0's territory */
            const unsigned char *cm = camp_frame(c, click, 0);
            int i;
            static int shot_once = 0;
            camp_shot(c, "flow_map.png", &shot_once);
            for (i = 0; i < 320 * 200; i++)
                if (cm[i] == row->colour[d][0]) {
                    k.clickx = i % 320;
                    k.clicky = i / 320;
                    break;
                }
        }
        if (k.clickx >= 0) {
            const unsigned char *cm = camp_frame(c, click, 0);
            int v = cm[k.clicky * 320 + k.clickx];
            int s2;
            for (s2 = 0; s2 < row->choices[d]; s2++) {
                int want = row->colour[d][s2];
                /* mapsel.cpp:1046 -- Egypt the second time through */
                if (want == 0xA0 && (v == 0x80 || v == 0x81))
                    v = 0xA0;
                if (want == v) {
                    camp_sfx(c, "WORLD2.AUD", 90);
                    sel = s2;
                    camp_show_mouse(c, 0); /* mapsel.cpp:1133 Hide_Mouse on the pick */
                    break;
                }
            }
            if (sel < 0)
                camp_sfx(c, "SCOLD1.AUD", 90); /* 1995 scolds per missed compare;
                                                  once per click is the same sound */
            k.clickx = -1;
        }
    }
    k.cyc_on = 0;

    *dir = row->dir[d][sel] ? row->dir[d][sel] : 'E';
    *var = row->var[d][sel] ? row->var[d][sel] : 'A';
    c->mapdir = *dir;
    printf("CAMPAIGN|mapsel|picked=%d|dir=%c|var=%c\n", sel, *dir, *var);
    fflush(stdout);

    /* The post-click country highlight (COUNTRYE.SHP + DARK_E.PAL fade) and
       Print_Statistics country page are a registered gap this wave -- the art and
       palette are already in the pack (COUNTRYE, DARK_E) for when they land.
       Fade to black MEDIUM and hand the flow on (mapsel.cpp:1158-1166). */
    k.fade_step = -(256 / 15 + 1);
    if (!camp_ctx_delay(&k, 15)) goto closed;
    if (c->au)
        cnc_music_stop(c->au);
    c->fade = 256;
    return 0;

closed:
    if (c->au)
        cnc_music_stop(c->au);
    c->fade = 256;
    return -1;
}
