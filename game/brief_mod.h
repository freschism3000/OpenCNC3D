/* ====================================================================================
 *  brief_mod.h -- the mission briefing, read the way the 1995 engine reads it, for the
 *  pause dialog's Restate button.
 *
 *  scenarioini.cpp:472-483 fills Scen.BriefingText from two places, in this order:
 *
 *      ini.Get_TextBlock("Briefing", Scen.BriefingText, sizeof(Scen.BriefingText));
 *      if (Scen.BriefingText[0] == '\0') {
 *          INIClass mini; CCFileClass missionIniFile("MISSION.INI"); mini.Load(...);
 *          mini.Get_TextBlock(root, Scen.BriefingText, sizeof(Scen.BriefingText));
 *      }
 *
 *  i.e. the mission INI's own [Briefing] block, and when that is empty the block named
 *  after the scenario ("[SCG01EA]") in MISSION.INI, which lives in the disc's
 *  GENERAL.MIX. The Covert Operations and console-side INIs carry their own block; the
 *  36 campaign missions carry none and read MISSION.INI. Get_TextBlock (ini.cpp:696)
 *  joins the block's entries with one space between them, in file order, and stops when
 *  the 512 byte buffer is full.
 *
 *  The engine has that text in Scen.BriefingText the whole mission, but nothing exports
 *  it, and the brain is patched rather than forked, so the host reads the same two
 *  places itself. Both are plain files: the INI beside the other mission files, and
 *  MISSION.INI through the MIX reader the sound engine already links. Nothing here
 *  needs GL or SDL.
 *
 *  Also read here, from the same INI's [Basic] block: the requirement= and Action= movie
 *  names, which are what goptions.cpp:375-381 plays when the player asks for the video
 *  (the requirement if its file exists, otherwise the action movie).
 * ==================================================================================== */

#ifndef BRIEF_MOD_H
#define BRIEF_MOD_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "mixfile.h"

#define BRIEF_TEXT_MAX 512   /* Scen.BriefingText, scenario.h:86 */

static char g_briefText[BRIEF_TEXT_MAX];
static char g_briefMovie[16];    /* [Basic] Brief=  ("x" or empty means none) */
static char g_actionMovie[16];   /* [Basic] Action= */
static char g_briefFrom[32];     /* "INI", "MISSION.INI" or "" for the log line */

/* Trailing blanks and the CR a DOS file carries. Leading blanks too, as INIClass's
   loader trims both ends of every value (ini.cpp strtrim). */
static void brief_trim(char* s)
{
    size_t n = strlen(s);
    char* p = s;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

/* Get_TextBlock over an INI held in memory: find [section], then append every
   "key=value" line of it, in file order, with one space between values, until the
   next section or the buffer is full. Returns the number of characters written. */
static int brief_text_block(const char* ini, size_t len, const char* section,
                            char* out, int cap)
{
    size_t i = 0;
    int in = 0, total = 0;
    char want[40];
    out[0] = 0;
    if (cap <= 1) return 0;
    snprintf(want, sizeof want, "[%s]", section);
    while (i < len) {
        char line[512];
        size_t n = 0;
        while (i < len && ini[i] != '\n' && n < sizeof line - 1) line[n++] = ini[i++];
        while (i < len && ini[i] != '\n') i++;       /* an over-long line is cut */
        if (i < len) i++;
        line[n] = 0;
        brief_trim(line);
        if (line[0] == '[') {
            if (in) break;                            /* the block has ended */
            /* section names compare case-insensitively, as INIClass does */
            {
                size_t k;
                int same = 1;
                for (k = 0; want[k] || line[k]; k++)
                    if (tolower((unsigned char)want[k]) != tolower((unsigned char)line[k])) {
                        same = 0;
                        break;
                    }
                in = same;
            }
            continue;
        }
        if (!in || !line[0] || line[0] == ';') continue;
        {
            const char* eq = strchr(line, '=');
            const char* val;
            char v[512];
            int vl;
            if (!eq) continue;
            val = eq + 1;
            snprintf(v, sizeof v, "%s", val);
            brief_trim(v);
            vl = (int)strlen(v);
            if (total > 0 && total + 1 < cap) { out[total++] = ' '; out[total] = 0; }
            if (vl > cap - 1 - total) vl = cap - 1 - total;
            if (vl <= 0) break;
            memcpy(out + total, v, (size_t)vl);
            total += vl;
            out[total] = 0;
            if (total >= cap - 1) break;
        }
    }
    return total;
}

/* One [Basic] key, or "" when it is not there. */
static void brief_basic_key(const char* ini, size_t len, const char* key, char* out, int cap)
{
    size_t i = 0;
    int in = 0;
    const size_t kl = strlen(key);
    out[0] = 0;
    while (i < len) {
        char line[512];
        size_t n = 0;
        while (i < len && ini[i] != '\n' && n < sizeof line - 1) line[n++] = ini[i++];
        while (i < len && ini[i] != '\n') i++;
        if (i < len) i++;
        line[n] = 0;
        brief_trim(line);
        if (line[0] == '[') {
            if (in) return;
            in = !strncasecmp(line, "[Basic]", 7);
            continue;
        }
        if (!in) continue;
        if (!strncasecmp(line, key, kl) && line[kl] == '=') {
            snprintf(out, (size_t)cap, "%s", line + kl + 1);
            brief_trim(out);
            return;
        }
    }
}

static char* brief_slurp(const char* path, size_t* len)
{
    FILE* f = fopen(path, "rb");
    long n;
    char* buf;
    *len = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (1 << 20)) { fclose(f); return NULL; }
    buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    buf[n] = 0;
    *len = (size_t)n;
    return buf;
}

/* MISSION.INI out of GENERAL.MIX. The archive is looked for where the sound engine
   looks for its own: the folder asked for, then the places a play folder and a
   working tree keep it. The first GENERAL.MIX found is the one read. */
static char* brief_mission_ini(const char* dosdata, size_t* len)
{
    static const char* const places[] = {
        NULL, "dosdata", "../dosdata", "data/dosdata", "../data/dosdata", "../../data/dosdata"
    };
    unsigned k;
    *len = 0;
    for (k = 0; k < sizeof places / sizeof places[0]; k++) {
        const char* d = (k == 0) ? dosdata : places[k];
        char path[600], err[128];
        MixFile* mx;
        long off = 0, size = 0;
        char* buf;
        if (!d || !d[0]) continue;
        snprintf(path, sizeof path, "%s/GENERAL.MIX", d);
        mx = mixfile_open(path, err, sizeof err);
        if (!mx) continue;
        if (!mixfile_find(mx, "MISSION.INI", &off, &size) || size <= 0 || size > (1 << 20)) {
            mixfile_close(mx);
            return NULL;
        }
        buf = (char*)malloc((size_t)size + 1);
        if (!buf) { mixfile_close(mx); return NULL; }
        if (mixfile_read(mx, off, buf, (int)size) != (int)size) {
            free(buf);
            mixfile_close(mx);
            return NULL;
        }
        mixfile_close(mx);
        buf[size] = 0;
        *len = (size_t)size;
        return buf;
    }
    return NULL;
}

/* Read everything for one mission. `dir` is the mission folder (with or without its
   trailing separator), `scen` the scenario name, `dosdata` the sound archive folder
   the command line asked for (NULL for the default search). Prints one line saying
   where the text came from, or that there is none, because a Restate button that is
   greyed for a mission that has a briefing is a fault this line is the evidence for. */
static void brief_load(const char* dir, const char* scen, const char* dosdata)
{
    char path[1024];
    size_t len = 0;
    char* ini;

    g_briefText[0] = 0;
    g_briefMovie[0] = 0;
    g_actionMovie[0] = 0;
    g_briefFrom[0] = 0;
    if (!scen || !scen[0]) return;

    snprintf(path, sizeof path, "%s%s%s.INI", dir ? dir : "",
             (dir && *dir && dir[strlen(dir) - 1] != '/') ? "/" : "", scen);
    ini = brief_slurp(path, &len);
    if (ini) {
        brief_basic_key(ini, len, "Brief", g_briefMovie, sizeof g_briefMovie);
        brief_basic_key(ini, len, "Action", g_actionMovie, sizeof g_actionMovie);
        if (brief_text_block(ini, len, "Briefing", g_briefText, BRIEF_TEXT_MAX) > 0)
            snprintf(g_briefFrom, sizeof g_briefFrom, "INI");
        free(ini);
    }
    if (!g_briefText[0]) {
        char* mini = brief_mission_ini(dosdata, &len);
        if (mini) {
            if (brief_text_block(mini, len, scen, g_briefText, BRIEF_TEXT_MAX) > 0)
                snprintf(g_briefFrom, sizeof g_briefFrom, "MISSION.INI");
            free(mini);
        }
    }
    /* "x" is how the INIs spell "no movie" */
    if (!strcmp(g_briefMovie, "x") || !strcmp(g_briefMovie, "X")) g_briefMovie[0] = 0;
    if (!strcmp(g_actionMovie, "x") || !strcmp(g_actionMovie, "X")) g_actionMovie[0] = 0;
    printf("BRIEF|%s|from=%s|chars=%d|brief=%s|action=%s\n", scen,
           g_briefFrom[0] ? g_briefFrom : "none", (int)strlen(g_briefText),
           g_briefMovie[0] ? g_briefMovie : "-", g_actionMovie[0] ? g_actionMovie : "-");
    fflush(stdout);
}

/* Which movie Restate's Video button plays: goptions.cpp:377-381, the requirement if its
   file is there, otherwise the action movie. Returns "" when neither file exists,
   which is scenario.cpp:797-803's "no video button" case. */
static const char* brief_video_name(void)
{
    static const char* const names[2] = { g_briefMovie, g_actionMovie };
    int k;
    for (k = 0; k < 2; k++) {
        char path[512];
        FILE* f;
        if (!names[k][0]) continue;
        snprintf(path, sizeof path, "dosdata/movies/%s.VQA", names[k]);
        f = fopen(path, "rb");
        if (f) { fclose(f); return names[k]; }
    }
    return "";
}

#endif /* BRIEF_MOD_H */
