/* ======================================================================================
 *  enhanced_mod.h -- Enhanced scripting: conditions Tiberian Dawn cannot express.
 *
 *  WHY THIS EXISTS
 *
 *  A Tiberian Dawn trigger is one event paired with one action. There is no AND, no OR,
 *  no NOT, no counter and no variable (trigger.h:41-108). Every compound condition in
 *  the shipped campaign is faked -- with the Cancel-trigger latch, or with a chain of
 *  timers that only works if nothing runs late. The mission designer's real intent
 *  ("once the Hand of Nod is up AND five minutes have passed AND the convoy has not
 *  already been ambushed") is not in the file anywhere; only its approximation is.
 *
 *  Rather than pretend, or offer an AND node that quietly compiles to a latch, CNC3D
 *  adds a second tier. A map may use it, and a map that does is TAGGED, because the
 *  1995 engine cannot run it and would run the rest of the mission while silently
 *  dropping these rules -- which is worse than refusing.
 *
 *  HOW IT WORKS, and why it is small
 *
 *  The engine keeps its actions. An Enhanced rule compiles to two halves:
 *
 *    - a CARRIER: an ordinary [Triggers] line whose event is None, carrying the action,
 *      the house, the team and the persistence. Nothing in the engine springs an
 *      EVENT_NONE trigger, so it is inert on its own.
 *    - a RULE in [EnhancedScript]: the rich condition, naming its carrier.
 *
 *  This host evaluates the condition each tick and, when it is satisfied, calls
 *  CNC3D_Spring_Trigger. The engine then runs its OWN action body -- Create Team with
 *  its ScenarioInit bracket, Reinforce with Do_Reinforcements' edge resolution, Allow
 *  Win with its Blockage accounting, and every quirk intact. Not one of the eighteen
 *  actions is reimplemented here, which is the whole point: an Enhanced mission's
 *  effects are bit-for-bit the effects a native mission gets.
 *
 *  WHAT A NATIVE ENGINE SEES
 *
 *  A carrier with event None. It loads, it occupies a trigger slot, and it never fires.
 *  So an Enhanced map opened in the 1995 game is a mission with holes, not a crash --
 *  and that is exactly why the tag in [Basic] matters and why this host says so at load
 *  when it cannot run them.
 *
 *  THE FILE
 *
 *      [Basic]
 *      Enhanced=1                        ; this map needs an Enhanced-capable engine
 *
 *      [EnhancedScript]
 *      ambush=fire:amb1|when:ALL|if:TIME 300|if:TYPE BadGuy HAND 1|if:!COUNTER done 1
 *             |do:ADD done 1|repeat:0
 *
 *      [EnhancedZones]
 *      1234=ambush                       ; cell -> rule, as [CellTriggers] does
 *
 *  Pipe-separated key:value pairs, keys repeatable, order irrelevant. A reader that
 *  does not know a key ignores it, so the format can grow without a version number.
 * ==================================================================================== */

#ifndef ENHANCED_MOD_H
#define ENHANCED_MOD_H

#include <stdarg.h>

/* ---- the clause vocabulary -------------------------------------------------------- */

enum EnhClauseKind {
    ENH_TIME = 0,     /* mission elapsed >= arg, in tenths of a minute        */
    ENH_CREDITS,      /* house credits >= arg                                 */
    ENH_UNITS,        /* house owns >= arg units                              */
    ENH_BUILDINGS,    /* house owns >= arg buildings                          */
    ENH_TYPE,         /* house owns >= arg of one type                        */
    ENH_ZONE,         /* >= arg of house's things stand in this rule's zone   */
    ENH_COUNTER,      /* counter >= arg                                       */
    ENH_FIRED,        /* a native trigger is gone, i.e. it fired and was volatile */
    ENH_CLAUSE_N
};

static const char* const ENH_CLAUSE[ENH_CLAUSE_N] = {
    "TIME", "CREDITS", "UNITS", "BUILDINGS", "TYPE", "ZONE", "COUNTER", "FIRED"
};

/* Which of the three argument slots each clause uses: house, name, number. Kept as a
   table because the parser, the writer, the editor and the validator all need the same
   answer and three copies of it would drift. */
struct EnhClauseShape { unsigned char house, name, num; const char* human; };
static const EnhClauseShape ENH_SHAPE[ENH_CLAUSE_N] = {
    { 0, 0, 1, "minutes into the mission" },
    { 1, 0, 1, "has at least this many credits" },
    { 1, 0, 1, "owns at least this many units" },
    { 1, 0, 1, "owns at least this many buildings" },
    { 1, 1, 1, "owns at least this many of a type" },
    { 1, 0, 1, "has this many things inside the zone" },
    { 0, 1, 1, "counter has reached this value" },
    { 0, 1, 0, "a native rule has already fired" },
};

enum EnhEffectKind { ENH_SET = 0, ENH_ADD, ENH_EFFECT_N };
static const char* const ENH_EFFECT[ENH_EFFECT_N] = { "SET", "ADD" };

#define ENH_CLAUSE_MAX  8
#define ENH_EFFECT_MAX  4
#define ENH_LINE_MAX  240      /* Read_Line truncates at 255; stay clear of the edge */

struct EnhClause {
    unsigned char kind;
    unsigned char negate;
    char house[16];
    char name[16];
    int  num;
};

struct EnhEffect {
    unsigned char kind;
    char name[16];
    int  num;
};

struct EnhRule {
    char name[16];
    char carrier[8];        /* the [Triggers] line whose action this runs; may be empty */
    unsigned char all;      /* 1 = every clause must hold, 0 = any one of them          */
    unsigned char repeat;   /* 0 = fire once, 1 = every time it becomes true            */
    int nclause;  EnhClause clause[ENH_CLAUSE_MAX];
    int neffect;  EnhEffect effect[ENH_EFFECT_MAX];

    /* runtime, not written to the file */
    unsigned char was;      /* was the condition true last tick (edge detection)        */
    unsigned char spent;    /* a once-only rule that has fired                          */
    int firedAt;            /* tick it last fired, -1 for never; the trace reads this   */
};

static std::vector<EnhRule> g_enh;
/* Editor selection. Lives here rather than in edit_mod.h so the rule model and the thing
   pointing at it cannot drift apart. */
static int  g_enhSel = -1;
static int  g_enhClause = -1;
static bool g_enhPaint = false;
static int  g_enhBrush = 1;         /* radius in cells: 1, 2, 3 */
static bool g_enhDirty = false;        /* the editor changed them                      */
static bool g_enhTagged = false;       /* [Basic] Enhanced=1 was present on load       */

/* Cell -> rule index, the Enhanced answer to [CellTriggers]. Separate from the native
   table on purpose: a native cell may carry only ONE trigger (display.cpp:1359-1391),
   and that limit is the engine's, not ours. */
/* Sized at the renderer's C3D_MAP_MAX ceiling and indexed by the INI's own flat
   cell number (y*64+x legacy, y*128+x when [MAP] Version=1). Storage is stride-free
   on purpose: this table is filled from the INI before the pack has committed the
   world grid, so a reader here must not depend on g_gridW. Only the CONSUMERS
   stride -- enh_w_inzone (cnc_eyes.cpp) by the live grid, the editor's zone painter
   by the map's INI stride. */
static short g_enhCell[C3D_MAP_MAX * C3D_MAP_MAX];

/* Counters. Created on first use, all starting at zero. */
struct EnhCounter { char name[16]; int value; };
static std::vector<EnhCounter> g_enhCounter;

static int enh_counter_index(const char* nm, bool make)
{
    for (size_t i = 0; i < g_enhCounter.size(); i++)
        if (!strcasecmp(g_enhCounter[i].name, nm)) return (int)i;
    if (!make) return -1;
    EnhCounter c;
    memset(&c, 0, sizeof c);
    snprintf(c.name, sizeof c.name, "%s", nm);
    g_enhCounter.push_back(c);
    return (int)g_enhCounter.size() - 1;
}

static int enh_rule_by_name(const char* nm)
{
    if (!nm || !*nm) return -1;
    for (size_t i = 0; i < g_enh.size(); i++)
        if (!strcasecmp(g_enh[i].name, nm)) return (int)i;
    return -1;
}

static void enh_clear(void)
{
    g_enh.clear();
    g_enhCounter.clear();
    g_enhDirty = false;
    g_enhTagged = false;
    g_enhSel = -1;
    g_enhClause = -1;
    g_enhPaint = false;
    for (int i = 0; i < C3D_MAP_MAX * C3D_MAP_MAX; i++) g_enhCell[i] = -1;
}

/* ---- reading --------------------------------------------------------------------- */

static int enh_name_index(const char* s, const char* const* tab, int n)
{
    for (int i = 0; i < n; i++) if (!strcasecmp(tab[i], s)) return i;
    return -1;
}

/* "TYPE BadGuy HAND 1" -> a clause. Returns false on anything it does not understand,
   because a clause silently read as something else is a rule that silently means
   something else. */
static bool enh_parse_clause(const char* text, EnhClause* out)
{
    memset(out, 0, sizeof *out);
    while (*text == ' ') text++;
    if (*text == '!') { out->negate = 1; text++; while (*text == ' ') text++; }

    char buf[160];
    snprintf(buf, sizeof buf, "%s", text);
    char* save = 0;
    char* tk = strtok_r(buf, " \t", &save);
    if (!tk) return false;
    const int k = enh_name_index(tk, ENH_CLAUSE, ENH_CLAUSE_N);
    if (k < 0) return false;
    out->kind = (unsigned char)k;
    const EnhClauseShape& sh = ENH_SHAPE[k];
    if (sh.house) {
        tk = strtok_r(0, " \t", &save);
        if (!tk) return false;
        snprintf(out->house, sizeof out->house, "%s", tk);
    }
    if (sh.name) {
        tk = strtok_r(0, " \t", &save);
        if (!tk) return false;
        snprintf(out->name, sizeof out->name, "%s", tk);
    }
    if (sh.num) {
        tk = strtok_r(0, " \t", &save);
        if (!tk) return false;
        out->num = atoi(tk);
    }
    return true;
}

static bool enh_parse_effect(const char* text, EnhEffect* out)
{
    memset(out, 0, sizeof *out);
    char buf[160];
    snprintf(buf, sizeof buf, "%s", text);
    char* save = 0;
    char* tk = strtok_r(buf, " \t", &save);
    if (!tk) return false;
    const int k = enh_name_index(tk, ENH_EFFECT, ENH_EFFECT_N);
    if (k < 0) return false;
    out->kind = (unsigned char)k;
    tk = strtok_r(0, " \t", &save);
    if (!tk) return false;
    snprintf(out->name, sizeof out->name, "%s", tk);
    tk = strtok_r(0, " \t", &save);
    out->num = tk ? atoi(tk) : 0;
    return true;
}

static void enh_read(const char* path)
{
    enh_clear();
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[1024];
    int sec = 0;                    /* 0 none, 1 Basic, 2 EnhancedScript, 3 zones */
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            sec = !strncasecmp(p, "[Basic]", 7)          ? 1
                : !strncasecmp(p, "[EnhancedScript]", 16) ? 2
                : !strncasecmp(p, "[EnhancedZones]", 15)  ? 3 : 0;
            continue;
        }
        if (!sec || *p == ';' || *p == '\r' || *p == '\n' || !*p) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char* val = eq + 1;
        for (char* e = val + strlen(val); e > val && (e[-1] == '\r' || e[-1] == '\n'); )
            *--e = 0;

        if (sec == 1) {
            if (!strcasecmp(p, "Enhanced")) g_enhTagged = (atoi(val) != 0);
            continue;
        }
        /* Zones are resolved in a second pass: a cell may name a rule defined further
           down the file, and the two sections may be in either order. */
        if (sec == 3) continue;

        EnhRule r;
        memset(&r, 0, sizeof r);
        snprintf(r.name, sizeof r.name, "%s", p);
        r.all = 1;
        r.firedAt = -1;
        char buf[1024];
        snprintf(buf, sizeof buf, "%s", val);
        char* save = 0;
        for (char* tk = strtok_r(buf, "|", &save); tk; tk = strtok_r(0, "|", &save)) {
            while (*tk == ' ') tk++;
            char* colon = strchr(tk, ':');
            if (!colon) continue;
            *colon = 0;
            const char* key = tk;
            const char* v = colon + 1;
            if (!strcasecmp(key, "fire")) {
                snprintf(r.carrier, sizeof r.carrier, "%.4s", v);
            } else if (!strcasecmp(key, "when")) {
                r.all = (unsigned char)(strcasecmp(v, "ANY") != 0);
            } else if (!strcasecmp(key, "repeat")) {
                r.repeat = (unsigned char)(atoi(v) != 0);
            } else if (!strcasecmp(key, "if")) {
                if (r.nclause < ENH_CLAUSE_MAX &&
                    enh_parse_clause(v, &r.clause[r.nclause])) r.nclause++;
            } else if (!strcasecmp(key, "do")) {
                if (r.neffect < ENH_EFFECT_MAX &&
                    enh_parse_effect(v, &r.effect[r.neffect])) r.neffect++;
            }
        }
        g_enh.push_back(r);
    }

    /* Second pass for the zones: a cell may name a rule defined later in the file, and
       the sections may be in either order. Cheap enough to just re-read. */
    rewind(f);
    sec = 0;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            sec = !strncasecmp(p, "[EnhancedZones]", 15) ? 3 : 0;
            continue;
        }
        if (sec != 3 || *p == ';' || !*p) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char* val = eq + 1;
        for (char* e = val + strlen(val); e > val && (e[-1] == '\r' || e[-1] == '\n'); )
            *--e = 0;
        const int cell = atoi(p);
        const int r = enh_rule_by_name(val);
        if (cell >= 0 && cell < C3D_MAP_MAX * C3D_MAP_MAX) g_enhCell[cell] = (short)r;
    }
    fclose(f);
    if (!g_enh.empty() || g_enhTagged)
        fprintf(stderr, "enhanced: %d rules, tagged=%d\n", (int)g_enh.size(),
                g_enhTagged ? 1 : 0);
}

/* ---- writing --------------------------------------------------------------------- */

static int enh_clause_text(const EnhClause& c, char* out, int cap)
{
    const EnhClauseShape& sh = ENH_SHAPE[c.kind];
    int n = snprintf(out, cap, "%s%s", c.negate ? "!" : "", ENH_CLAUSE[c.kind]);
    if (sh.house && n < cap) n += snprintf(out + n, cap - n, " %s", c.house);
    if (sh.name  && n < cap) n += snprintf(out + n, cap - n, " %s", c.name);
    if (sh.num   && n < cap) n += snprintf(out + n, cap - n, " %d", c.num);
    return n;
}

/* Append, and NEVER walk off the end.
 *
 * snprintf returns what it WOULD have written, so accumulating its return value walks
 * `n` past `cap` the moment anything truncates -- and then `out + n` is past the buffer
 * and `cap - n` is negative, which snprintf takes as a size_t and reads as about four
 * billion. Both line writers were built that way and the last call in each was not even
 * guarded. The return value still reports the length the line WANTS, because both
 * callers use it to refuse a line the engine's own 255-byte buffer cannot take. */
static int enh_append(char* out, int cap, int n, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char piece[256];
    const int want = vsnprintf(piece, sizeof piece, fmt, ap);
    va_end(ap);
    if (n < cap - 1) {
        const int room = cap - 1 - n;
        int k = 0;
        while (k < room && piece[k]) { out[n + k] = piece[k]; k++; }
        out[n + k] = 0;
    } else if (cap > 0) {
        out[cap - 1] = 0;
    }
    return n + (want < 0 ? 0 : want);
}

static int enh_rule_line(const EnhRule& r, char* out, int cap)
{
    int n = 0;
    if (cap > 0) out[0] = 0;
    if (r.carrier[0]) n = enh_append(out, cap, n, "fire:%s|", r.carrier);
    n = enh_append(out, cap, n, "when:%s", r.all ? "ALL" : "ANY");
    for (int i = 0; i < r.nclause; i++) {
        char c[160];
        enh_clause_text(r.clause[i], c, sizeof c);
        n = enh_append(out, cap, n, "|if:%s", c);
    }
    for (int i = 0; i < r.neffect; i++)
        n = enh_append(out, cap, n, "|do:%s %s %d", ENH_EFFECT[r.effect[i].kind],
                       r.effect[i].name, r.effect[i].num);
    n = enh_append(out, cap, n, "|repeat:%d", r.repeat ? 1 : 0);
    return n;
}

static void enh_emit(FILE* out)
{
    fputs("\r\n[EnhancedScript]\r\n", out);
    for (size_t i = 0; i < g_enh.size(); i++) {
        char line[1024];
        const int n = enh_rule_line(g_enh[i], line, sizeof line);
        if (n >= ENH_LINE_MAX) {
            fprintf(stderr, "enhanced: rule %s is %d characters and the limit is %d; "
                            "NOT written. Split it across two rules with a counter.\n",
                    g_enh[i].name, n, ENH_LINE_MAX);
            continue;
        }
        fprintf(out, "%s=%s\r\n", g_enh[i].name, line);
    }
    int zones = 0;
    for (int c = 0; c < C3D_MAP_MAX * C3D_MAP_MAX; c++) if (g_enhCell[c] >= 0) zones++;
    if (zones) {
        fputs("\r\n[EnhancedZones]\r\n", out);
        for (int c = 0; c < C3D_MAP_MAX * C3D_MAP_MAX; c++) {
            const int r = g_enhCell[c];
            if (r >= 0 && r < (int)g_enh.size())
                fprintf(out, "%d=%s\r\n", c, g_enh[r].name);
        }
    }
}

static int enh_zone_cells(int rule)
{
    int n = 0;
    for (int c = 0; c < C3D_MAP_MAX * C3D_MAP_MAX; c++) if (g_enhCell[c] == rule) n++;
    return n;
}


/* ======================================================================================
 *  The evaluator
 *
 *  Runs once per game tick, alongside the brain rather than inside it. Every clause is
 *  answered from state the host already has: the per-house line the brain prints each
 *  frame, and the object list it draws from. Nothing here reaches into the engine, and
 *  nothing here changes the world except by springing a carrier -- so a bug in this file
 *  cannot desync the simulation, it can only fail to fire a rule.
 *
 *  EDGE-TRIGGERED, always. A rule fires on the tick its condition becomes true, not on
 *  every tick it stays true. "Repeat" means it may fire again after going false and true
 *  again; without it the rule is spent. This matches how a designer reads the sentence
 *  and, more practically, stops "credits >= 1000" from creating a team fifteen times a
 *  second.
 * ==================================================================================== */

/* Filled in by the host each frame, before enh_tick. Kept as plain function pointers so
   this header does not need to know the renderer's types. */
struct EnhWorld {
    int  tick;                                  /* ticks since the mission started     */
    int  (*credits)(const char* house);
    int  (*units)(const char* house);
    int  (*buildings)(const char* house);
    int  (*typecount)(const char* house, const char* code);
    int  (*inzone)(const char* house, const short* cells, int rule);
    bool (*fired)(const char* trigname);        /* the native trigger is gone           */
    bool (*spring)(const char* trigname);
};

static bool enh_clause_true(const EnhClause& c, const EnhWorld& w)
{
    bool v = false;
    switch (c.kind) {
    /* Time is in tenths of a minute, exactly as a native Time trigger is, so the two
       tiers say the same thing when they say 300. TICKS_PER_MINUTE / 10 == 90. */
    case ENH_TIME:      v = (w.tick >= c.num * 90); break;
    case ENH_CREDITS:   v = w.credits   ? (w.credits(c.house)   >= c.num) : false; break;
    case ENH_UNITS:     v = w.units     ? (w.units(c.house)     >= c.num) : false; break;
    case ENH_BUILDINGS: v = w.buildings ? (w.buildings(c.house) >= c.num) : false; break;
    case ENH_TYPE:      v = w.typecount ? (w.typecount(c.house, c.name) >= c.num) : false;
                        break;
    case ENH_ZONE:      v = false; break;    /* filled in by the caller: needs the rule */
    case ENH_COUNTER: {
        const int i = enh_counter_index(c.name, false);
        v = (i >= 0 && g_enhCounter[i].value >= c.num);
        break;
    }
    case ENH_FIRED:     v = w.fired ? w.fired(c.name) : false; break;
    default: v = false; break;
    }
    return c.negate ? !v : v;
}

static bool enh_rule_true(const EnhRule& r, const EnhWorld& w, int ruleIndex)
{
    /* A rule with no clauses is never true. The alternative -- vacuously true, which is
       what ALL over an empty set means in logic -- would make a half-authored rule fire
       on the first tick, which is the worst possible moment to learn about it. */
    if (!r.nclause) return false;
    for (int i = 0; i < r.nclause; i++) {
        bool v;
        if (r.clause[i].kind == ENH_ZONE) {
            const int n = w.inzone ? w.inzone(r.clause[i].house, g_enhCell, ruleIndex) : 0;
            v = (n >= r.clause[i].num);
            if (r.clause[i].negate) v = !v;
        } else {
            v = enh_clause_true(r.clause[i], w);
        }
        if (r.all && !v) return false;
        if (!r.all && v) return true;
    }
    return r.all ? true : false;
}

/* What happened this tick, for the toast and the trace. */
struct EnhFireLog { int rule; int tick; bool sprang; };
static std::vector<EnhFireLog> g_enhFired;

static void enh_tick(const EnhWorld& w)
{
    for (size_t i = 0; i < g_enh.size(); i++) {
        EnhRule& r = g_enh[i];
        const bool now = enh_rule_true(r, w, (int)i);
        const bool edge = now && !r.was;
        r.was = now ? 1 : 0;
        if (!edge || r.spent) continue;
        if (!r.repeat) r.spent = 1;
        r.firedAt = w.tick;

        for (int e = 0; e < r.neffect; e++) {
            const int ci = enh_counter_index(r.effect[e].name, true);
            if (r.effect[e].kind == ENH_SET) g_enhCounter[ci].value = r.effect[e].num;
            else                             g_enhCounter[ci].value += r.effect[e].num;
        }
        bool sprang = false;
        if (r.carrier[0] && w.spring) sprang = w.spring(r.carrier);
        EnhFireLog lg; lg.rule = (int)i; lg.tick = w.tick; lg.sprang = sprang;
        g_enhFired.push_back(lg);
        /* The legibility rail: when a rule fires, say so and say what it did. A rule
           that fires invisibly is indistinguishable from one that never fired. */
        fprintf(stderr, "enhanced: t%d  %s fired%s%s%s\n", w.tick, r.name,
                r.carrier[0] ? " -> " : "", r.carrier[0] ? r.carrier : "",
                r.carrier[0] && !sprang ? "  (carrier already gone)" : "");
    }
}

/* Back to tick zero, for a reload or a restart. The counters and the edge state are the
   only things this module owns; everything else it reads. */
static void enh_reset_runtime(void)
{
    for (size_t i = 0; i < g_enh.size(); i++) {
        g_enh[i].was = 0;
        g_enh[i].spent = 0;
        g_enh[i].firedAt = -1;
    }
    for (size_t i = 0; i < g_enhCounter.size(); i++) g_enhCounter[i].value = 0;
    g_enhFired.clear();
}

#endif /* ENHANCED_MOD_H */
