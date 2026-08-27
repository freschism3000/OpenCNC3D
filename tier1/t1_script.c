#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

#include "t1_script.h"

static int vk_named(const char *n)
{
    if (!strcmp(n, "LEFT"))  return VK_LEFT;
    if (!strcmp(n, "RIGHT")) return VK_RIGHT;
    if (!strcmp(n, "UP"))    return VK_UP;
    if (!strcmp(n, "DOWN"))  return VK_DOWN;
    if (!strcmp(n, "PGUP"))  return VK_PRIOR;
    if (!strcmp(n, "PGDN"))  return VK_NEXT;
    if (!strcmp(n, "CTRL"))  return VK_CONTROL;
    if (!strcmp(n, "ESC"))   return VK_ESCAPE;
    if (!strcmp(n, "SPACE")) return VK_SPACE;
    if (!strcmp(n, "ENTER")) return VK_RETURN;
    /* The middle mouse button, so the drag-to-pan can be exercised from a script. A
     * SYNTHETIC middle button never reaches a fullscreen Glide app, which is the same
     * reason the left and right buttons are scripted rather than injected. */
    if (!strcmp(n, "MBUTTON")) return VK_MBUTTON;
    if (n[0] >= 'A' && n[0] <= 'Z' && !n[1]) return n[0];
    return 0;
}

int t1_script_load(T1_Script *s, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[160];
    memset(s, 0, sizeof *s);
    if (!f) return 0;
    while (s->n < T1S_MAX && fgets(line, sizeof line, f))
    {
        T1_ScriptCmd *c = &s->cmd[s->n];
        char word[32], arg[32];
        int fr, got;
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == ';' || *p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        got = sscanf(p, "%d %31s %31s %d", &fr, word, arg, &c->b);
        if (got < 2) continue;
        c->frame = fr;
        strncpy(c->cmd, word, sizeof c->cmd - 1);
        c->cmd[sizeof c->cmd - 1] = 0;
        c->a = 0; c->s[0] = 0;
        if (!strcmp(word, "move") || !strcmp(word, "cam"))
        {
            if (sscanf(p, "%d %31s %d %d", &fr, word, &c->a, &c->b) < 4) continue;
        }
        else if (!strcmp(word, "lb") || !strcmp(word, "rb") || !strcmp(word, "over"))
        {
            if (got < 3) continue;
            c->a = atoi(arg);
        }
        else if (!strcmp(word, "key"))
        {
            if (got < 3) continue;
            c->a = vk_named(arg);
            if (got < 4 || c->b < 1) c->b = 2;
            if (!c->a) continue;
        }
        else if (!strcmp(word, "shot"))
        {
            if (got < 3) continue;
            strncpy(c->s, arg, sizeof c->s - 1);
            c->s[sizeof c->s - 1] = 0;
        }
        else if (strcmp(word, "quit") && strcmp(word, "place")) continue;
        ++s->n;
    }
    fclose(f);
    s->mx = 320; s->my = 240;
    return s->n;
}

void t1_script_step(T1_Script *s, int frame)
{
    int i;
    s->shot[0] = 0;
    s->camset = 0;
    s->place = 0;
    for (i = 0; i < 256; ++i) if (s->hold[i] > 0) --s->hold[i];
    while (s->next < s->n && s->cmd[s->next].frame <= frame)
    {
        T1_ScriptCmd *c = &s->cmd[s->next++];
        ++s->fired;
        if      (!strcmp(c->cmd, "move")) { s->mx = c->a; s->my = c->b; }
        else if (!strcmp(c->cmd, "lb"))   s->lb = c->a ? 1 : 0;
        else if (!strcmp(c->cmd, "rb"))   s->rb = c->a ? 1 : 0;
        else if (!strcmp(c->cmd, "key"))  s->hold[c->a & 255] = c->b;
        else if (!strcmp(c->cmd, "over")) { s->over = c->a ? 1 : 0; s->overset = 1; }
        else if (!strcmp(c->cmd, "cam"))
        { s->camx = c->a; s->camz = c->b; s->camset = 1; }
        else if (!strcmp(c->cmd, "shot")) strcpy(s->shot, c->s);
        else if (!strcmp(c->cmd, "place")) s->place = 1;
        else if (!strcmp(c->cmd, "quit")) s->quit = 1;
    }
}

int t1_script_key(const T1_Script *s, int vk) { return s->hold[vk & 255] > 0; }
