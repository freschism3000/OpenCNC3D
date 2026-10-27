/*
 * forward-check.c -- the Windows half of tools/launcher/forward-check.sh.
 *
 * The launcher starts the game on Windows through CreateProcess, which takes one
 * command line rather than a list, so every forwarded argument is quoted into it by
 * lc_win_append (launcher/lcmdline.c) and split again by the game's C runtime. That
 * cannot be run here, but the string can be checked: each expected line below is
 * worked from the C runtime's documented parsing rules, not from the code under test,
 * so a quoting mistake shows up as a mismatch rather than agreeing with itself.
 *
 * Built and run by forward-check.sh; prints one FWDWIN line per case and exits 1 on
 * the first wrong one.
 */

#include "lcmdline.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want)) {
        printf("FWDWIN|FAIL|%s|got=[%s]|want=[%s]\n", what, got, want);
        failures++;
    } else
        printf("FWDWIN|ok|%s|[%s]\n", what, got);
}

/* One argument alone on a line, as the game's runtime would have to read it back. */
static void one(const char *arg, const char *want)
{
    char out[256];
    size_t n = 0;
    out[0] = '\0';
    if (!lc_win_append(out, sizeof out, &n, arg))
        snprintf(out, sizeof out, "(did not fit)");
    expect(arg, out, want);
}

int main(void)
{
    char line[32767], small[16];
    size_t n = 0;
    int i, ok = 1;

    /* Left bare: nothing in it a parser could split on or unescape. Backslashes
     * are literal unless a quote follows them, so a Windows path needs nothing. */
    one("--script", "--script");
    one("C:\\Games\\OpenCNC3D\\run.script", "C:\\Games\\OpenCNC3D\\run.script");
    one("a\\\\\\b", "a\\\\\\b");
    /* Quoted: a space, a tab, a quote, or nothing at all. */
    one("C:\\Program Files\\OpenCNC3D", "\"C:\\Program Files\\OpenCNC3D\"");
    one("a\tb", "\"a\tb\"");
    one("", "\"\"");
    one("say \"hi\"", "\"say \\\"hi\\\"\"");
    /* The backslash rules: a run in front of a quote doubles and the quote gets one
     * of its own; a run at the end doubles, because the closing quote follows it; a
     * run anywhere else stays as it is. */
    one("a\\\"b", "\"a\\\\\\\"b\"");
    one("C:\\save dir\\", "\"C:\\save dir\\\\\"");
    one("a\\\\b c", "\"a\\\\b c\"");

    /* A WHOLE LINE of the length that went wrong: the game's path, then twenty
     * forwarded arguments whose last two are --script and a file in a folder with a
     * space in its name. Every one of them has to arrive, the file included. */
    {
        static const char *args[] = {
            "C:\\Program Files\\OpenCNC3D\\ra3d\\opencnc3d-game.exe",
            "--module", "ra3d",
            "--hidden", "--no-movies", "--nosound", "--raskirmish", "SCM10EA",
            "--side", "soviet", "--ai", "3", "--seed", "4242", "--credits", "10000",
            "--savedir", "C:\\Users\\A B\\saves", "--nosound", "--nosound", "--nosound",
            "--script", "C:\\Users\\A B\\run.script"
        };
        line[0] = '\0';
        for (i = 0; ok && i < (int)(sizeof args / sizeof *args); i++)
            ok = lc_win_append(line, sizeof line, &n, args[i]);
        if (!ok)
            snprintf(line, sizeof line, "(did not fit)");
        expect("twenty forwarded",
               line,
               "\"C:\\Program Files\\OpenCNC3D\\ra3d\\opencnc3d-game.exe\" --module ra3d "
               "--hidden --no-movies --nosound --raskirmish SCM10EA --side soviet --ai 3 "
               "--seed 4242 --credits 10000 --savedir \"C:\\Users\\A B\\saves\" "
               "--nosound --nosound --nosound --script \"C:\\Users\\A B\\run.script\"");
    }

    /* A LINE THAT DOES NOT FIT IS REFUSED AND LEFT AS IT WAS, never cut: a cut
     * line is the failure this replaced, a file name missing its last letters. */
    n = 0;
    small[0] = '\0';
    ok = lc_win_append(small, sizeof small, &n, "--script");
    ok = ok && !lc_win_append(small, sizeof small, &n, "C:\\run.script");
    expect(ok && n == 8 ? "overflow refused, line kept" : "overflow NOT refused",
           small, "--script");
    if (!ok || n != 8)
        failures++;

    printf("FWDWIN|end|%d failures\n", failures);
    return failures ? 1 : 0;
}
