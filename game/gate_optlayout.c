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
 *      that stops answering. THE NUMBER IS COMPUTED, not written down: it is the sum of
 *      the six page counts (with the Advanced page contributing its VIEW rows plus OK and
 *      the bar, not its element count). It was 41 before the Gameplay page went in and it
 *      is 45 after. gates.sh asserts no literal of its own.
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
/* THE ADVANCED PAGE IS THE ONE PAGE WHOSE RECTANGLE COUNT IS NOT ITS ITEM COUNT. It is
 * a scrolling list: it holds DOPT_VE_COUNT elements and shows DOPT_A_VIEW_ROWS of them,
 * so what it answers with is those rows plus OK plus the scroll bar. Summing
 * DOPT_A_COUNT here would demand a rectangle for every element at once, which is exactly
 * what scrolling means it must not give. */
/* THE RESTATE BOX IS MEASURED TWICE: with a movie on offer it answers DOPT_R_COUNT
 * rectangles (Video and Options), and without one it answers ONE, the lone OK, because
 * the second button then does not exist (scenario.cpp:797-803). Both forms are drawn
 * for real, so both are counted. */
/* THE SLOT DIALOG IS MEASURED TWICE TOO: in LOAD mode the description field does not
 * exist, so the page answers DOPT_SL_COUNT - 1 rectangles; in SAVE mode all of them. */
/* THE NOTICE BOX answers ONE rectangle, its lone OK, on the Restate geometry. */
#define OPTLAYOUT_EXPECT                                                                 \
    (DOPT_ITEM_COUNT + DOPT_C_COUNT + DOPT_V_COUNT + (DOPT_A_VIEW_ROWS + 2)              \
     + DOPT_S_COUNT + DOPT_G_COUNT + DOPT_R_COUNT + 1 + (DOPT_SL_COUNT - 1) + DOPT_SL_COUNT \
     + 1)

/* goptions.cpp:130 walks the stack in steps of OButtonHeight + 2. The 2 is the gap. */
#define OPT_MIN_SEP 2

/* A row supplier with every slot taken, newest first, for the slot dialog legs. */
static int layout_slots(void *user, DOPT_SlotRow *out, int max)
{
    int i;
    (void)user;
    for (i = 0; i < 16 && i < max; i++) {
        out[i].slot = 15 - i;
        sprintf(out[i].text, "(GDI) slot %d", 15 - i);
    }
    return i;
}

static int fails = 0;
static int checked = 0;
static int dropseen = 0;   /* leg (g): which drop rows were measured */

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
            /* Anti-vacuity: an item that answers with no rectangle is not a pass --
               EXCEPT an Advanced page element that is outside the well, which genuinely
               has none. That is the same answer dopt_hit_test reads, so a row with no
               rectangle is a row no click can reach, which is what scrolling has to mean.
               The exemption is narrow on purpose: it applies only to element rows, only
               on that page, and only when the row is actually outside the window advTop
               opens. OK and the scroll bar are never exempt, and the per-page count below
               is what stops this swallowing a page that has stopped answering. */
            if (pg == DOPT_PAGE_ADVANCED && i < DOPT_VE_COUNT
                && (i < st->advTop || i >= st->advTop + DOPT_A_VIEW_ROWS))
                continue;
            /* And the Restate box's second button, which is not there without a movie:
               the lone OK form has one rectangle by design, and the total leg counts it
               as one. */
            if ((pg == DOPT_PAGE_RESTATE || pg == DOPT_PAGE_NOTICE) && i == DOPT_R_RIGHT
                && !st->rvideo)
                continue;
            /* And the slot dialog's field, which exists only in SAVE mode. */
            if (pg == DOPT_PAGE_SLOTS && i == DOPT_SL_EDIT && st->sl.mode != DOPT_SL_SAVE)
                continue;
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
        /* THE SCROLL BAR IS THE FOURTH NAMED EXEMPTION. It is DOPT_A_BAR_H tall, which no
           height rule below would ever cover, and it prints nothing: it carries a name
           only so the scripted click verb can address it. Named here rather than absorbed
           by loosening the height test, which is what this leg's own header asks for. */
        if (h != 5 && h != 6 && h != 7 && h != 73
            && !(pg == DOPT_PAGE_ADVANCED && i == DOPT_A_BAR)
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

        /* (g) A DROP ROW'S VALUE BOX: clear of the row's own printed label by the 2 that
           legs (d)/(e) demand between any two controls, wide enough for its widest entry
           plus the arrow, and inside its row. Legs (a)-(e) never saw this: the box is
           not an item, and the label is printed to the RIGHT of an h=7 row, which leg
           (b) names as exempt. Measured: RESOLUTION, UI SCALING and
           PERSPECTIVE ran 6, 4 and 12 columns under one right-aligned box while every
           leg above was green. */
        if (pg == DOPT_PAGE_ADVANCED) {
            int dx, dy, dw, dh, need, labend;
            if (dopt_texset_box_rect_pub(st, i, &dx, &dy, &dw, &dh)) {
                dropseen |= 1 << i;
                labend = DOPT_A_LABEL_X
                       + db_string_width(fnt, dopt_item_label(st, i), DB_FONT6_XSPACING);
                need = dopt_drop_need_pub(st, p, i);
                if (labend + OPT_MIN_SEP > dx) {
                    printf("  FAIL label under the drop box: %-16s label ends %d, box starts "
                           "%d (want >= %d)\n", dopt_item_label(st, i), labend - 1, dx,
                           labend + OPT_MIN_SEP);
                    fails++;
                }
                if (dw < need) {
                    printf("  FAIL drop box too narrow: %-16s box %d wide, widest entry + "
                           "arrow needs %d\n", dopt_item_label(st, i), dw, need);
                    fails++;
                }
                if (dx < x || dx + dw > x + w) {
                    printf("  FAIL drop box outside its row: %-16s box x=%d..%d row %d..%d\n",
                           dopt_item_label(st, i), dx, dx + dw - 1, x, x + w - 1);
                    fails++;
                }
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
    /* THE ADVANCED PAGE'S OWN NUMBER, because it is the one page that does not answer
       with its item count and therefore cannot lean on the total at the bottom. The well
       must give back exactly the rows it shows, plus OK, plus the bar: fewer means a row
       fell out of the window that should have been in it, more means the scroll offset is
       not being applied at all. */
    if (pg == DOPT_PAGE_ADVANCED && here != DOPT_A_VIEW_ROWS + 2) {
        printf("  FAIL the Advanced page returned %d rectangles, want %d (%d rows in the "
               "well, OK and the scroll bar)\n",
               here, (int)(DOPT_A_VIEW_ROWS + 2), (int)DOPT_A_VIEW_ROWS);
        fails++;
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
    page(&st, p, DOPT_PAGE_GAMEPLAY, "GAMEPLAY", DOPT_V_X, DOPT_V_Y, DOPT_V_W,
         DOPT_V_H);

    /* THE RESTATE BOX, sized from a briefing of the longest shape the discs carry (the
     * widest one is 280 characters; this is longer, so the box measured here is at
     * least as big as any real one) and measured in both of its forms. The box's own
     * rectangle comes out of the layout the way the confirmation's does, so the leg
     * that checks it fits the plate is the one that matters: a briefing that wrapped
     * to too many lines would walk the box off the bottom. */
    {
        static const char* const LONG =
            "Nod is experimenting on civilians using Tiberium. Use the Commando to take "
            "out the SAM sites surrounding the dropoff area. With the SAMs gone you will "
            "then be given an airstrike. Take out the Obelisk and an MCV will be delivered "
            "to help you to locate and destroy the BioResearch Facility. Nod forces are "
            "scattered throughout the area so it is only a matter of time before you are "
            "detected, and the longest shipped briefing is two hundred and eighty long.";
        dopt_set_briefing(&st, LONG, 1);
        dopt_layout(&st, p);
        if (st.brlines < 6 || st.brlines > DOPT_R_MAX_LINES) {
            printf("  FAIL the Restate wrap produced %d lines for a %d character briefing "
                   "at %d px; a wrap that stopped working would draw one line off the "
                   "plate\n", st.brlines, (int)strlen(LONG), DOPT_R_WRAP_W);
            fails++;
        }
        {
            int li, x0 = st.rx + DOPT_CF_TEXT_X;
            for (li = 0; li < st.brlines; li++) {
                const int lw = db_string_width(p ? db_font(p, "GRAD6FNT") : NULL,
                                               st.brwrap + st.brline[li], DB_FONT6_XSPACING);
                if (lw >= DOPT_R_WRAP_W) {
                    printf("  FAIL Restate line %d is %d px wide, past the %d px wrap: [%s]\n",
                           li, lw, DOPT_R_WRAP_W, st.brwrap + st.brline[li]);
                    fails++;
                }
                if (x0 + lw > st.rx + st.rw - 2) {
                    printf("  FAIL Restate line %d runs out of its box: text to %d, box "
                           "inner edge %d\n", li, x0 + lw - 1, st.rx + st.rw - 2);
                    fails++;
                }
            }
        }
        page(&st, p, DOPT_PAGE_RESTATE, "RESTATE-VIDEO", st.rx, st.ry, st.rw, st.rh);
        dopt_set_briefing(&st, LONG, 0);
        dopt_layout(&st, p);
        page(&st, p, DOPT_PAGE_RESTATE, "RESTATE-OK", st.rx, st.ry, st.rw, st.rh);
    }

    /* THE NOTICE BOX, the Restate geometry lent to a refused mission start. Opened the
     * way the shell opens it, with a caption and the longest sentence the boot path can
     * produce (a loader's own error text runs long: this one is the shape of a missing
     * engine library on Windows), and measured as the single-OK form. The leg that
     * matters is the same as Restate's: the wrapped text and the lone button inside the
     * box, and the box on the plate. */
    {
        static const char* const NOTICE =
            "The engine library C:\\Users\\Player\\Desktop\\CNC3D-windows-v0.7.1\\"
            "TiberianDawn.dll could not be loaded: LoadLibrary(TiberianDawn.dll) failed, "
            "GetLastError=126 (ERROR_MOD_NOT_FOUND: the file itself, or a DLL it depends "
            "on, is not where the loader looked)";
        dopt_open_notice(&st, p, "Unable to start mission", NOTICE);
        if (st.page != DOPT_PAGE_NOTICE || st.brlines < 3 || st.rw <= 0) {
            printf("  FAIL dopt_open_notice opened page %d with %d lines in a %d wide box; "
                   "want the notice page, several lines and a measured box\n",
                   st.page, st.brlines, st.rw);
            fails++;
        }
        page(&st, p, DOPT_PAGE_NOTICE, "NOTICE", st.rx, st.ry, st.rw, st.rh);
        /* OK, Enter and Escape all close it and nothing else does: the one answer. */
        if (dopt_key(&st, DOPT_KEY_ESC) != DOPT_ACT_RESUME) {
            printf("  FAIL Escape on the notice did not answer DOPT_ACT_RESUME\n");
            fails++;
        }
    }

    /* THE SLOT DIALOG, in LOAD and in SAVE mode, with the row supplier answering a full
     * sixteen so the well is fuller than it can show. The list and the field print no
     * centred label and are taller than any button, so leg (b) measures their names
     * against their own width, which they pass by a mile; what this is here for is legs
     * (a), (d) and (e): every rectangle inside the 250x156 box, and no two of them
     * touching. loaddlg.cpp's numbers are derived in dosopt.h and this is where they are
     * read back. */
    {
        int li;
        dopt_bind_slots(&st, layout_slots);
        dopt_open_slots(&st, p, DOPT_SL_LOAD);
        if (st.sl.nrows != 16) {
            printf("  FAIL the LOAD dialog lists %d rows from a supplier that gave 16\n",
                   st.sl.nrows);
            fails++;
        }
        page(&st, p, DOPT_PAGE_SLOTS, "SLOTS-LOAD", DOPT_SL_X, DOPT_SL_Y, DOPT_SL_W,
             DOPT_SL_H);
        dopt_open_slots(&st, p, DOPT_SL_SAVE);
        /* SAVE puts the empty slot first, and with sixteen taken there is none: the
           rows are the sixteen, and the field opens on the row's own description. */
        if (st.sl.nrows != 16 || strcmp(dopt_slot_row_text(&st, 0), "(GDI) slot 15") != 0) {
            printf("  FAIL the SAVE dialog's first row is [%s] of %d; with sixteen slots "
                   "taken there is no empty row and the newest comes first\n",
                   dopt_slot_row_text(&st, 0) ? dopt_slot_row_text(&st, 0) : "(none)",
                   st.sl.nrows);
            fails++;
        }
        if (strcmp(dopt_slot_descr(&st), "slot 15") != 0) {
            printf("  FAIL the SAVE field opened on [%s], want the first row's own "
                   "description with the (GDI) stripped\n", dopt_slot_descr(&st));
            fails++;
        }
        /* the rows the well shows must end above the field's label */
        for (li = 0; li < DOPT_SL_LIST_H_SAVE / DOPT_SL_ROW_H; li++) {
            const int ry = DOPT_SL_LIST_Y + 1 + li * DOPT_SL_ROW_H;
            if (ry + DOPT_SL_ROW_H > DOPT_SL_LABEL_Y) {
                printf("  FAIL SAVE row %d ends at %d, through the label at %d\n", li,
                       ry + DOPT_SL_ROW_H - 1, DOPT_SL_LABEL_Y);
                fails++;
            }
        }
        page(&st, p, DOPT_PAGE_SLOTS, "SLOTS-SAVE", DOPT_SL_X, DOPT_SL_Y, DOPT_SL_W,
             DOPT_SL_H);
    }

    /* THE ADVANCED PAGE AGAIN, AT THE BOTTOM OF TRAVEL.
     *
     * Every leg above measures the page as it opens, which is the one scroll offset
     * guaranteed to be tidy. An offset is arithmetic that can put a row through the OK
     * button just as easily as a step can, and the whole point of this round is that the
     * offset is no longer always zero. So the page is scrolled to the end and read again,
     * through dopt_scroll, which is the function both dialog loops hand the wheel to.
     *
     * `checked` is put back afterwards because the total leg counts each page once; the
     * second pass is here to fail on a rectangle, not to be counted. */
    {
        int before = checked;
        int x, y, w, h;
        const int wantTop = (DOPT_VE_COUNT > DOPT_A_VIEW_ROWS)
                                ? DOPT_VE_COUNT - DOPT_A_VIEW_ROWS : 0;
        st.page = DOPT_PAGE_ADVANCED;
        dopt_scroll(&st, DOPT_VE_COUNT); /* far past the end; it must clamp */
        if (st.advTop != wantTop) {
            printf("  FAIL the Advanced page scrolled to %d, want %d: dopt_scroll is not "
                   "clamping to the bottom of travel\n", st.advTop, wantTop);
            fails++;
        }
        if (wantTop > 0 && dopt_item_rect(&st, 0, &x, &y, &w, &h)) {
            printf("  FAIL the first element still has a rectangle at the bottom of "
                   "travel: x=%d..%d y=%d..%d. dopt_hit_test would find a row that is not "
                   "on the screen\n", x, x + w - 1, y, y + h - 1);
            fails++;
        }
        if (!dopt_item_rect(&st, DOPT_VE_COUNT - 1, &x, &y, &w, &h)) {
            printf("  FAIL the last element has no rectangle even at the bottom of "
                   "travel, so nothing on this page can ever reach it\n");
            fails++;
        }
        page(&st, p, DOPT_PAGE_ADVANCED, "ADVANCED-BOTTOM", DOPT_V_X, DOPT_V_Y, DOPT_V_W,
             DOPT_V_H);
        checked = before;
    }

    /* Leg (g) is only worth having if it measured every drop row on one of the two
       passes (the top of travel shows the display rows, the bottom the art rows). */
    {
        const int want = (1 << DOPT_VE_RESOLUTION) | (1 << DOPT_VE_UISCALE)
                       | (1 << DOPT_VE_PERSPECTIVE) | (1 << DOPT_VE_TEXSET)
                       | (1 << DOPT_VE_INFSET);
        if (dropseen != want) {
            printf("  FAIL not every drop row was measured by leg (g): mask %#x, want %#x\n",
                   dropseen, want);
            fails++;
        }
    }

    if (checked != OPTLAYOUT_EXPECT) {
        printf("  FAIL checked %d rectangles, the pages declare %d\n", checked,
               (int)OPTLAYOUT_EXPECT);
        fails++;
    }
    printf("OPTLAYOUT|checked=%d|failures=%d\n", checked, fails);
    return fails ? 1 : 0;
}
