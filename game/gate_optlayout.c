/*
 * gate_optlayout.c -- THE PAUSE DIALOG'S GEOMETRY, MEASURED.
 *
 * Reported against v0.5.7: the options menu window needs to be taller so every button
 * fits -- EXIT GAME was almost cutting through RESUME MISSION and RESTATE -- and the
 * Sound Controls and Options Menu buttons under GAME CONTROLS belong side by side,
 * centred and larger, so
 * theres space for all of the text for SOUND CONTROL."
 *
 * Both of those are RECTANGLES AND PRINTED TEXT EXTENTS, and until this file existed
 * the suite could not see either one. What it had instead was a false claim: gates.sh
 * carried the line "The seventh pause-menu button and the grown dialog are covered by
 * G13 already passing" above G42, and G13 (gates.sh:285-317) asserts only that the
 * dialog opened, that the brain's Frame counter is the same at open and at abort, and
 * that at least one tick of play happened first. It measures no pixel and no rectangle.
 * G12 asserts click routing and a slider value. G4 compares two runs of the SAME binary,
 * so it cannot see a change of look at all. gate_options.txt, gate_visuals.txt and
 * gate_jukebox.txt click by LABEL through dopt_item_rect, so they follow the geometry
 * wherever it goes and stay green while two labels print on top of each other.
 *
 * WHY THIS IS A SEPARATE BINARY AND NOT A --script GATE. dosopt.c is plain C89 with no
 * SDL, no GL and no window in it (game/build.sh:42-44 says so and compiles it that way),
 * so its layout can be interrogated directly. This links dosopt.o and dosbar.o, opens
 * the same dossidebar.pack the game opens, and answers in milliseconds without a display.
 * A rendering gate would have to boot a mission to ask a question about arithmetic.
 *
 * WHAT IT ASSERTS, five legs, none of them a taste value:
 *   (a) every item's rectangle lies inside its own dialog box
 *   (b) every button's PRINTED LABEL lies inside its own button and inside the dialog
 *   (c) no two items overlap
 *   (d) two items sharing a column are at least 2 rows apart
 *   (e) two items sharing a row are at least 2 columns apart
 *
 * The 2 in (d) and (e) is not chosen: it is goptions.cpp:129-130's own stack step,
 * OButtonHeight + 2, i.e. the breathing space the 1995 dialog puts between every pair of
 * stacked buttons. Anything tighter than that is a pair of buttons the engine would
 * never have drawn.
 *
 * LEG (b) MEASURES THE PRINTED TEXT, NOT THE RECTANGLE, and that is the whole point of
 * it. the second complaint is invisible to a rectangle-only test: Sound Controls sat
 * in a 64 wide box at x=50..113 and Options Menu at x=121..198, which do not overlap,
 * while the label "Sound Controls" is 82 wide and printed at x=40..121 -- five columns
 * outside the dialog border, on the battlefield, with its last column on the OK button's
 * first. db_print (dosbar.c:393-413) clips to the SURFACE and never to the button, so a
 * label too wide for its box is NOT truncated: it is painted over whatever is beside it.
 * tx below is dopt_draw_button's own expression (dosopt.c:903), copied verbatim so this
 * measures what is drawn rather than what ought to be.
 *
 * TWO ANTI-VACUITY RULES, because this project has been burned by gates that pass
 * because nothing objected (project rule 7):
 *
 *   1. A MISSING PACK OR FONT IS A HARD FAILURE, exit 2. dopt_layout (dosopt.c:158-163)
 *      SILENTLY falls back to DOPT_MIN_BTN_W for every button when db_font returns null.
 *      Every width this gate reads would then be a default rather than a measurement,
 *      every leg would pass, and the gate would report a number it never took. That is
 *      the same shape of hole as the gfx_lowsun.cfg one written up at
 *      game/make-build.sh:86-91: a missing input that is not an error.
 *
 *   2. THE NUMBER OF RECTANGLES CHECKED IS ITSELF ASSERTED, here and again in gates.sh.
 *      A page whose dopt_page_count went to zero returns no rectangles, so no leg can
 *      fail and the gate goes green on a dialog that has vanished. OPTLAYOUT_EXPECT is
 *      the sum of the five page enums at compile time, so this binary catches a page
 *      that stops answering; gates.sh asserts the literal 38 as the outer belt, so
 *      ADDING a button is a deliberate edit to the gate and not a silent drift.
 *
 * TWO EXEMPTIONS, both named rather than papered over by loosening leg (b):
 *   - DOPT_S_STOP and DOPT_S_PLAY print a GLYPH and not their label (dosopt.h:247-250:
 *     BTN-ST.SHP / BTN-PL.SHP are in no archive we bake, so they are drawn as a filled
 *     square and a triangle). They still carry a name, because optclick looks buttons up
 *     by label, but that name is never printed and must not be measured.
 *   - The On/Off toggles are 25 wide, which is sounddlg.cpp:83's own OnOff_Width. "Off"
 *     is 18 wide so it fits its box; it fails textbtn.cpp's text+8 autosize rule, and
 *     that is 1995's arithmetic rather than ours. Leg (b) asks whether the label FITS,
 *     not whether the box obeys the autosize rule, so no exemption is needed here --
 *     written down so nobody adds one.
 *
 * Usage:  gate_optlayout <dossidebar.pack>
 * Prints: OPTLAYOUT|page=NAME|box=..|items=N and OPTLAYOUT|page=NAME|checked=N per page,
 *         then OPTLAYOUT|checked=N|failures=M
 * Exit:   0 clean, 1 one or more failures, 2 the inputs were not there.
 */

#include <stdio.h>
#include <string.h>
#include "dosopt.h"

/* Anti-vacuity rule 2. Compile time, out of the page enums themselves. */
#define OPTLAYOUT_EXPECT                                                                 \
    (DOPT_ITEM_COUNT + DOPT_C_COUNT + DOPT_V_COUNT + DOPT_A_COUNT + DOPT_S_COUNT)

/* goptions.cpp:130 walks the stack in steps of OButtonHeight + 2. The 2 is the gap. */
#define OPT_MIN_SEP 2

static int fails = 0;
static int checked = 0;

static void page(DOPT_State *st, const DB_Pack *p, int pg, const char *name, int bx,
                 int by, int bw, int bh)
{
    const DB_Font *fnt = db_font(p, "GRAD6FNT");
    int n, i, j, x, y, w, h, x2, y2, w2, h2, sw, tx, gap, here = 0;

    st->page = pg;
    n = dopt_page_count(st);
    printf("OPTLAYOUT|page=%s|box=%d..%d,%d..%d|items=%d\n", name, bx, bx + bw - 1, by,
           by + bh - 1, n);

    /* (f) THE DIALOG ITSELF MUST FIT THE 320x200 PLATE. Every other leg measures items
       against the box, so a box that has walked off the plate takes all of them with it and
       the gate reports nothing. Measured: DOPT_H 210 puts DOPT_Y at -5, clips the top and
       bottom borders off the plate, and still scored checked=38 failures=0. DOPT_H is the
       exact constant this gate was written to protect, so this is the leg it could least
       afford to be missing. */
    if (bx < 0 || by < 0 || bx + bw > DOPT_SCREEN_W || by + bh > DOPT_SCREEN_H) {
        printf("  FAIL the dialog does not fit the plate: %s box x=%d..%d y=%d..%d "
               "screen 0..%d,0..%d\n",
               name, bx, bx + bw - 1, by, by + bh - 1, DOPT_SCREEN_W - 1, DOPT_SCREEN_H - 1);
        fails++;
    }

    for (i = 0; i < n; i++) {
        if (!dopt_item_rect(st, i, &x, &y, &w, &h)) {
            /* Anti-vacuity: an item that answers with no rectangle is not a pass. */
            printf("  FAIL no rectangle: item %d (%s)\n", i, dopt_item_label(st, i));
            fails++;
            continue;
        }
        checked++;
        here++;

        /* (a) inside its own dialog box. */
        if (x < bx || x + w > bx + bw || y < by || y + h > by + bh) {
            printf("  FAIL rect outside the dialog: %-16s x=%d..%d y=%d..%d\n",
                   dopt_item_label(st, i), x, x + w - 1, y, y + h - 1);
            fails++;
        }

        /* (b) the printed label. THE DEFAULT IS TO CHECK, and that inversion is the whole
           point. This leg used to read "if (h == DOPT_BTN_H)", so anything whose height
           was not exactly 9 had its label silently unchecked -- and a button's height is
           a thing someone changes. Measured on the old form: put the Sound Controls
           button back at its overflowing width AND move its height 9 -> 10, and the gate
           scored checked=38 failures=0 with the original defect on the screen.
           So the exceptions are NAMED and everything else is checked. The named ones are
           the items that carry no centred label: sliders (h 5 or 6), the jukebox listbox
           (h 73), the Advanced checkbox rows (h 7, whose labels print to the RIGHT of the
           box rather than centred in it, dosopt.h:490), and the Sound page's STOP and
           PLAY, which are shape buttons with no text at all.
           A NEW non-button item will now produce a false FAIL rather than a silent skip.
           That is deliberate: a loud wrong answer gets the item named here in a minute,
           and a silent one hid a real defect for a release. */
        if (h != 5 && h != 6 && h != 7 && h != 73
            && !(pg == DOPT_PAGE_SOUND && (i == DOPT_S_STOP || i == DOPT_S_PLAY))) {
            sw = db_string_width(fnt, dopt_item_label(st, i), DB_FONT6_XSPACING);
            tx = x + (w >> 1) - 1 - (sw >> 1); /* dopt_draw_button, dosopt.c:903 */
            if (tx < x || tx + sw - 1 > x + w - 1) {
                printf("  FAIL label wider than its own button: %-16s text x=%d..%d "
                       "button %d..%d\n",
                       dopt_item_label(st, i), tx, tx + sw - 1, x, x + w - 1);
                fails++;
            }
            if (tx < bx + 1 || tx + sw - 1 > bx + bw - 2) {
                printf("  FAIL label leaves the dialog: %-16s text x=%d..%d box inner "
                       "%d..%d\n",
                       dopt_item_label(st, i), tx, tx + sw - 1, bx + 1, bx + bw - 2);
                fails++;
            }
        }

        for (j = 0; j < i; j++) {
            if (!dopt_item_rect(st, j, &x2, &y2, &w2, &h2))
                continue;
            if (x < x2 + w2 && x2 < x + w && y < y2 + h2 && y2 < y + h) {
                /* (c) */
                printf("  FAIL overlap: %s x=%d..%d y=%d..%d / %s x=%d..%d y=%d..%d\n",
                       dopt_item_label(st, i), x, x + w - 1, y, y + h - 1,
                       dopt_item_label(st, j), x2, x2 + w2 - 1, y2, y2 + h2 - 1);
                fails++;
            } else if (x < x2 + w2 && x2 < x + w) {
                /* (d) they share a column, so they must not be welded vertically. */
                gap = (y > y2) ? y - (y2 + h2) : y2 - (y + h);
                if (gap < OPT_MIN_SEP) {
                    printf("  FAIL gap %d rows < %d: %-16s y=%d..%d vs %-16s y=%d..%d\n",
                           gap, OPT_MIN_SEP, dopt_item_label(st, i), y, y + h - 1,
                           dopt_item_label(st, j), y2, y2 + h2 - 1);
                    fails++;
                }
            } else if (y < y2 + h2 && y2 < y + h) {
                /* (e) they share a row, so they must not be welded horizontally. */
                gap = (x > x2) ? x - (x2 + w2) : x2 - (x + w);
                if (gap < OPT_MIN_SEP) {
                    printf("  FAIL gap %d cols < %d: %-16s x=%d..%d vs %-16s x=%d..%d\n",
                           gap, OPT_MIN_SEP, dopt_item_label(st, i), x, x + w - 1,
                           dopt_item_label(st, j), x2, x2 + w2 - 1);
                    fails++;
                }
            }
        }
    }
    printf("OPTLAYOUT|page=%s|checked=%d\n", name, here);
}

int main(int argc, char **argv)
{
    char err[256];
    DB_Pack *p;
    DOPT_State st;

    if (argc < 2) {
        printf("OPTLAYOUT|fatal=usage\n");
        printf("usage: gate_optlayout <dossidebar.pack>\n");
        return 2;
    }
    /* Anti-vacuity rule 1: neither of these is allowed to be a warning. Without the
       font, dopt_layout hands back DOPT_MIN_BTN_W for every button and every leg below
       would be measuring a default. */
    p = db_pack_load(argv[1], err, sizeof err);
    if (!p) {
        printf("OPTLAYOUT|fatal=pack|%s\n", err);
        return 2;
    }
    if (!db_font(p, "GRAD6FNT")) {
        printf("OPTLAYOUT|fatal=font|GRAD6FNT missing: every width below would be the "
               "silent DOPT_MIN_BTN_W fallback, not a measurement\n");
        return 2;
    }

    memset(&st, 0, sizeof st);
    dopt_settings_init(&st.set);
    dopt_open(&st, p);

    page(&st, p, DOPT_PAGE_OPTIONS, "OPTIONS", DOPT_X, DOPT_Y, DOPT_W, DOPT_H);
    page(&st, p, DOPT_PAGE_CONTROLS, "CONTROLS", DOPT_GC_X, DOPT_GC_Y, DOPT_GC_W,
         DOPT_GC_H);
    page(&st, p, DOPT_PAGE_VISUALS, "VISUALS", DOPT_V_X, DOPT_V_Y, DOPT_V_W, DOPT_V_H);
    page(&st, p, DOPT_PAGE_ADVANCED, "ADVANCED", DOPT_V_X, DOPT_V_Y, DOPT_V_W, DOPT_V_H);
    page(&st, p, DOPT_PAGE_SOUND, "SOUND", DOPT_SND_X, DOPT_SND_Y, DOPT_SND_W,
         DOPT_SND_H);

    if (checked != OPTLAYOUT_EXPECT) {
        printf("  FAIL checked %d rectangles, the five pages declare %d\n", checked,
               (int)OPTLAYOUT_EXPECT);
        fails++;
    }
    printf("OPTLAYOUT|checked=%d|failures=%d\n", checked, fails);
    return fails ? 1 : 0;
}
