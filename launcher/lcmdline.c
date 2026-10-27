/*
 * lcmdline.c -- see lcmdline.h.
 */

#include "lcmdline.h"

#include <string.h>

/* One byte onto the line, keeping room for the terminating NUL. */
static int lc_put(char *out, size_t cap, size_t *i, char c)
{
    if (*i + 1 >= cap)
        return 0;
    out[(*i)++] = c;
    return 1;
}

/* THE RULES ARE THE C RUNTIME'S, which CommandLineToArgvW shares: a space or tab
 * ends an argument unless it is inside quotes, a quote is literal only when a
 * backslash escapes it, and backslashes are literal EXCEPT in a run that ends at a
 * quote, where each pair of them reads as one. So an argument that needs no quotes
 * is written as it is, backslashes and all (every Windows path), and one that does
 * doubles exactly the backslashes that stand in front of a quote, its own closing
 * quote included. */
int lc_win_append(char *out, size_t cap, size_t *len, const char *arg)
{
    size_t i = *len, k, bs;
    const char *p;
    int ok = 1;

    if (i > 0)
        ok = lc_put(out, cap, &i, ' ');
    if (*arg && !strpbrk(arg, " \t\n\v\"")) {
        for (p = arg; ok && *p; p++)
            ok = lc_put(out, cap, &i, *p);
    } else {
        ok = ok && lc_put(out, cap, &i, '"');
        for (p = arg; ok; p++) {
            for (bs = 0; *p == '\\'; p++)
                bs++;
            if (!*p) {
                for (k = 0; ok && k < 2 * bs; k++)
                    ok = lc_put(out, cap, &i, '\\');
                break;
            }
            if (*p == '"')
                bs = 2 * bs + 1;
            for (k = 0; ok && k < bs; k++)
                ok = lc_put(out, cap, &i, '\\');
            ok = ok && lc_put(out, cap, &i, *p);
        }
        ok = ok && lc_put(out, cap, &i, '"');
    }
    if (!ok) {
        if (cap > 0)
            out[*len < cap ? *len : cap - 1] = '\0';
        return 0;
    }
    out[i] = '\0';
    *len = i;
    return 1;
}
