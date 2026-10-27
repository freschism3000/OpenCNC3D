/* CPU-only regression for Enhanced typography fitting and wrapping.
 * Usage: gate_lskin ENHANCED_MENU_PACK DOSMENU_PACK
 * No SDL initialization, OpenGL context, network, or installation writes.
 */
#include "lui.h"
#include "lskin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_X 20
#define TEST_Y 50
#define TEST_WIDTH 100
#define TEST_LINE_HEIGHT 7

static int checks, failures;

static void check(int good, const char *message)
{
    checks++;
    if (!good) failures++;
    printf("LSKINGATE|%s|%s\n", good ? "ok" : "FAIL", message);
}

static void check_ink(const unsigned char *before, const unsigned char *after,
                      int logical_rows, const char *name)
{
    int x, y, changed = 0, outside = 0;
    int x0 = ls_native_x(TEST_X), x1 = ls_native_x(TEST_X + TEST_WIDTH);
    int y0 = ls_native_y(TEST_Y);
    int y1 = ls_native_y(TEST_Y + logical_rows * TEST_LINE_HEIGHT);
    int minimum_x = ls_width(), minimum_y = ls_height(), maximum_x = -1, maximum_y = -1;
    for (y = 0; y < ls_height(); y++) {
        for (x = 0; x < ls_width(); x++) {
            size_t offset = ((size_t)y * ls_width() + x) * 4;
            if (!memcmp(before + offset, after + offset, 4)) continue;
            changed++;
            if (x < minimum_x) minimum_x = x;
            if (x > maximum_x) maximum_x = x;
            if (y < minimum_y) minimum_y = y;
            if (y > maximum_y) maximum_y = y;
            if (x < x0 || x >= x1 || y < y0 || y >= y1) outside++;
        }
    }
    printf("LSKINGATE|ink|case=%s|changed=%d|outside=%d|bounds=%d,%d,%d,%d|allowed=%d,%d,%d,%d\n",
           name, changed, outside, minimum_x, minimum_y, maximum_x, maximum_y,
           x0, y0, x1, y1);
    check(changed > 0, "text changes canvas pixels");
    check(outside == 0, "text changes no pixels outside its allotted rectangle");
}

int main(int argc, char **argv)
{
    DB_Pack *pack;
    const DB_Font *font;
    DB_Surface surface;
    unsigned char indexed[DM_SCREEN_W * DM_SCREEN_H];
    unsigned char *baseline, *actual;
    LUI_Text wrapped;
    char error[512], long_i[402], fitted[406];
    size_t canvas_bytes;
    int native_limit, i, preserved = 0, rows, max_line = 0, prefix;

    if (argc != 3) {
        fprintf(stderr, "usage: %s ENHANCED_MENU_PACK DOSMENU_PACK\n", argv[0]);
        return 2;
    }
    if (!ls_load(argv[1])) {
        fprintf(stderr, "Cannot load Enhanced menu pack: %s\n", argv[1]);
        return 2;
    }
    pack = db_pack_load(argv[2], error, sizeof error);
    if (!pack) {
        fprintf(stderr, "Cannot load DOS pack: %s\n", error);
        ls_free();
        return 2;
    }
    font = db_font(pack, "6POINT");
    if (!font) {
        fprintf(stderr, "DOS pack has no 6POINT font\n");
        db_pack_free(pack);
        ls_free();
        return 2;
    }
    memset(indexed, 0, sizeof indexed);
    db_surface_init(&surface, DM_SCREEN_W, DM_SCREEN_H, indexed);
    memset(long_i, 'I', sizeof long_i - 1);
    long_i[sizeof long_i - 1] = '\0';
    native_limit = ls_native_x(TEST_X + TEST_WIDTH) - ls_native_x(TEST_X);
    check(native_limit == 300, "100 logical pixels provide 300 native pixels");
    check(ls_text_width(long_i) > native_limit, "long I token exceeds the available width");

    memset(&wrapped, 0, sizeof wrapped);
    rows = lui_text_build(&wrapped, font, long_i, TEST_WIDTH);
    check(rows > 1 && wrapped.count == rows, "long unbroken token creates multiple stored lines");
    for (i = 0; i < wrapped.count; i++) {
        int width = ls_text_width(wrapped.lines[i].text);
        preserved += (int)strlen(wrapped.lines[i].text);
        if (width > max_line) max_line = width;
        check(width > 0 && width <= native_limit, "stored line fits the actual Enhanced font");
    }
    check(preserved == (int)strlen(long_i), "stored wrapping preserves the complete token");
    printf("LSKINGATE|wrap|lines=%d|max_native_width=%d|limit=%d|characters=%d\n",
           rows, max_line, native_limit, preserved);
    lui_text_free(&wrapped);

    canvas_bytes = (size_t)ls_width() * ls_height() * 4;
    baseline = (unsigned char *)malloc(canvas_bytes);
    actual = (unsigned char *)malloc(canvas_bytes);
    if (!baseline || !actual) {
        fprintf(stderr, "Cannot allocate private canvas snapshots\n");
        free(baseline);
        free(actual);
        db_pack_free(pack);
        ls_free();
        return 2;
    }
    ls_begin();
    memcpy(baseline, ls_pixels(), canvas_bytes);
    lui_print_fit(&surface, font, long_i, TEST_X, TEST_Y, TEST_WIDTH, DB_WHITE);
    memcpy(actual, ls_pixels(), canvas_bytes);
    check_ink(baseline, actual, 1, "fit");

    /* Build the expected longest fitting prefix with the public native-font
     * oracle. No copied font widths, spacing table, or assumed glyph advance. */
    prefix = 0;
    while (prefix < (int)strlen(long_i)) {
        memcpy(fitted, long_i, (size_t)prefix + 1);
        memcpy(fitted + prefix + 1, "...", 4);
        if (ls_text_width(fitted) > native_limit) break;
        prefix++;
    }
    memcpy(fitted, long_i, (size_t)prefix);
    memcpy(fitted + prefix, "...", 4);
    check(ls_text_width(fitted) <= native_limit, "oracle's fitted prefix and ellipsis fit");
    memcpy(ls_pixels(), baseline, canvas_bytes);
    ls_text(fitted, TEST_X, TEST_Y, LS_TEXT, 0);
    check(!memcmp(actual, ls_pixels(), canvas_bytes), "print_fit matches the native-font oracle's canvas");
    printf("LSKINGATE|fit|prefix=%d|native_width=%d|limit=%d\n",
           prefix, ls_text_width(fitted), native_limit);

    /* A token whose advance lands on the limit also checks the font atlas's
     * trailing shadow pixels, which can extend beyond its advance width. */
    prefix = 0;
    while (prefix < (int)strlen(long_i)) {
        memcpy(fitted, long_i, (size_t)prefix + 1);
        fitted[prefix + 1] = '\0';
        if (ls_text_width(fitted) > native_limit) break;
        prefix++;
    }
    memcpy(fitted, long_i, (size_t)prefix);
    fitted[prefix] = '\0';
    memcpy(ls_pixels(), baseline, canvas_bytes);
    lui_print_fit(&surface, font, fitted, TEST_X, TEST_Y, TEST_WIDTH, DB_WHITE);
    check_ink(baseline, ls_pixels(), 1, "fit-boundary");

    memcpy(ls_pixels(), baseline, canvas_bytes);
    rows = lui_print_wrapped(&surface, font, long_i, TEST_X, TEST_Y,
                             TEST_WIDTH, DB_WHITE, 0, 3);
    check(rows == 3, "bounded paragraph uses its three allotted rows");
    check_ink(baseline, ls_pixels(), 3, "wrapped-left");
    memcpy(ls_pixels(), baseline, canvas_bytes);
    rows = lui_print_wrapped(&surface, font, long_i, TEST_X, TEST_Y,
                             TEST_WIDTH, DB_WHITE, 1, 3);
    check(rows == 3, "centered bounded paragraph uses three allotted rows");
    check_ink(baseline, ls_pixels(), 3, "wrapped-centered");

    free(baseline);
    free(actual);
    db_pack_free(pack);
    ls_free();
    printf("LSKINGATE|end|checks=%d|failures=%d|cpu_only=1|windows=0\n", checks, failures);
    return failures ? 1 : 0;
}
