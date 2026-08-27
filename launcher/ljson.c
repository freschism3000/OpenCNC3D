/*
 * ljson.c -- see ljson.h.
 *
 * A recursive descent parser with a depth cap. The cap is not decoration: the
 * input is a document fetched over the network, and `[[[[[[...` is the one input
 * that turns a recursive parser into a stack overflow.
 */

#include "ljson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LJ_MAX_DEPTH 32

typedef struct
{
    const char *p;
    char *err;
    int errlen;
    int depth;
    int failed;
} LJ_Parser;

static LJ_Value *lj_value(LJ_Parser *ps);

static void lj_fail(LJ_Parser *ps, const char *what)
{
    if (!ps->failed) {
        ps->failed = 1;
        snprintf(ps->err, (size_t)ps->errlen, "the reply is not valid JSON (%s)", what);
    }
}

static void lj_space(LJ_Parser *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')
        ps->p++;
}

static LJ_Value *lj_new(LJ_Type t)
{
    LJ_Value *v = (LJ_Value *)calloc(1, sizeof *v);
    if (v)
        v->type = t;
    return v;
}

void lj_free(LJ_Value *v)
{
    while (v) {
        LJ_Value *next = v->next;
        lj_free(v->first);
        free(v->text);
        free(v->key);
        free(v);
        v = next;
    }
}

/* A JSON string, decoded. The buffer is at most as long as the source, because
 * every escape shrinks. */
static char *lj_string(LJ_Parser *ps)
{
    const char *start;
    char *out, *w;

    if (*ps->p != '"') {
        lj_fail(ps, "expected a string");
        return NULL;
    }
    ps->p++;
    start = ps->p;
    /* Measure first, so the write pass cannot overrun. */
    while (*ps->p && *ps->p != '"') {
        if (*ps->p == '\\' && ps->p[1])
            ps->p++;
        ps->p++;
    }
    if (*ps->p != '"') {
        lj_fail(ps, "a string is not closed");
        return NULL;
    }
    out = (char *)malloc((size_t)(ps->p - start) + 1);
    if (!out) {
        lj_fail(ps, "out of memory");
        return NULL;
    }
    ps->p = start;
    w = out;
    while (*ps->p && *ps->p != '"') {
        if (*ps->p != '\\') {
            *w++ = *ps->p++;
            continue;
        }
        ps->p++;
        switch (*ps->p) {
        case '"':  *w++ = '"';  ps->p++; break;
        case '\\': *w++ = '\\'; ps->p++; break;
        case '/':  *w++ = '/';  ps->p++; break;
        case 'b':  *w++ = '\b'; ps->p++; break;
        case 'f':  *w++ = '\f'; ps->p++; break;
        case 'n':  *w++ = '\n'; ps->p++; break;
        case 'r':  *w++ = '\r'; ps->p++; break;
        case 't':  *w++ = '\t'; ps->p++; break;
        case 'u': {
            /* ASCII through, anything else to '?'. See the header: the two routes
             * this reads are ASCII, and a launcher is not the place to grow a
             * UTF-16 surrogate decoder that nothing exercises. */
            unsigned int cp = 0;
            int i;
            ps->p++;
            for (i = 0; i < 4; i++) {
                char c = ps->p[i];
                if (c >= '0' && c <= '9')      cp = cp * 16 + (unsigned int)(c - '0');
                else if (c >= 'a' && c <= 'f') cp = cp * 16 + (unsigned int)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') cp = cp * 16 + (unsigned int)(c - 'A' + 10);
                else { lj_fail(ps, "a bad \\u escape"); free(out); return NULL; }
            }
            ps->p += 4;
            *w++ = (cp && cp < 0x80) ? (char)cp : '?';
            break;
        }
        default:
            lj_fail(ps, "an unknown escape");
            free(out);
            return NULL;
        }
    }
    if (*ps->p != '"') {
        lj_fail(ps, "a string is not closed");
        free(out);
        return NULL;
    }
    ps->p++;
    *w = '\0';
    return out;
}

static LJ_Value *lj_container(LJ_Parser *ps, char open, char close, LJ_Type type)
{
    LJ_Value *v = lj_new(type);
    LJ_Value *tail = NULL;

    if (!v) {
        lj_fail(ps, "out of memory");
        return NULL;
    }
    ps->p++; /* the opening bracket */
    (void)open;
    lj_space(ps);
    if (*ps->p == close) {
        ps->p++;
        return v;
    }
    for (;;) {
        LJ_Value *item;
        char *key = NULL;

        lj_space(ps);
        if (type == LJ_OBJECT) {
            key = lj_string(ps);
            if (!key) { lj_free(v); return NULL; }
            lj_space(ps);
            if (*ps->p != ':') {
                lj_fail(ps, "expected ':' after a member name");
                free(key);
                lj_free(v);
                return NULL;
            }
            ps->p++;
            lj_space(ps);
        }
        item = lj_value(ps);
        if (!item) { free(key); lj_free(v); return NULL; }
        item->key = key;
        if (tail) tail->next = item; else v->first = item;
        tail = item;

        lj_space(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == close) { ps->p++; return v; }
        lj_fail(ps, "expected ',' or a closing bracket");
        lj_free(v);
        return NULL;
    }
}

static LJ_Value *lj_value(LJ_Parser *ps)
{
    LJ_Value *v;

    if (ps->depth >= LJ_MAX_DEPTH) {
        lj_fail(ps, "nested too deeply");
        return NULL;
    }
    lj_space(ps);
    switch (*ps->p) {
    case '{':
    case '[': {
        LJ_Value *c;
        ps->depth++;
        c = (*ps->p == '{') ? lj_container(ps, '{', '}', LJ_OBJECT)
                            : lj_container(ps, '[', ']', LJ_ARRAY);
        ps->depth--;
        return c;
    }
    case '"':
        v = lj_new(LJ_STRING);
        if (!v) { lj_fail(ps, "out of memory"); return NULL; }
        v->text = lj_string(ps);
        if (!v->text) { lj_free(v); return NULL; }
        return v;
    case 't':
        if (strncmp(ps->p, "true", 4)) break;
        ps->p += 4;
        v = lj_new(LJ_BOOL);
        if (v) v->boolean = 1;
        return v;
    case 'f':
        if (strncmp(ps->p, "false", 5)) break;
        ps->p += 5;
        return lj_new(LJ_BOOL);
    case 'n':
        if (strncmp(ps->p, "null", 4)) break;
        ps->p += 4;
        return lj_new(LJ_NULL);
    default: {
        /* A number, kept as its own text. The launcher wants byte counts and
         * asset ids, both of which are exact integers a double would round. */
        const char *start = ps->p;
        if (*ps->p == '-' || *ps->p == '+')
            ps->p++;
        while ((*ps->p >= '0' && *ps->p <= '9') || *ps->p == '.' || *ps->p == 'e'
               || *ps->p == 'E' || *ps->p == '-' || *ps->p == '+')
            ps->p++;
        if (ps->p == start)
            break;
        v = lj_new(LJ_NUMBER);
        if (!v) { lj_fail(ps, "out of memory"); return NULL; }
        v->text = (char *)malloc((size_t)(ps->p - start) + 1);
        if (!v->text) { lj_free(v); lj_fail(ps, "out of memory"); return NULL; }
        memcpy(v->text, start, (size_t)(ps->p - start));
        v->text[ps->p - start] = '\0';
        return v;
    }
    }
    lj_fail(ps, "an unexpected character");
    return NULL;
}

LJ_Value *lj_parse(const char *text, char *err, int errlen)
{
    LJ_Parser ps;
    LJ_Value *root;

    ps.p = text ? text : "";
    ps.err = err;
    ps.errlen = errlen;
    ps.depth = 0;
    ps.failed = 0;
    err[0] = '\0';

    root = lj_value(&ps);
    if (!root)
        return NULL;
    lj_space(&ps);
    if (*ps.p) {
        /* Trailing junk after a complete value. Usually an HTML error page that
         * happens to start with something parseable, which is exactly the case
         * worth refusing rather than half-believing. */
        lj_fail(&ps, "there is more after the end of the document");
        lj_free(root);
        return NULL;
    }
    return root;
}

const LJ_Value *lj_get(const LJ_Value *obj, const char *key)
{
    const LJ_Value *m;
    if (!obj || obj->type != LJ_OBJECT)
        return NULL;
    for (m = obj->first; m; m = m->next)
        if (m->key && !strcmp(m->key, key))
            return m;
    return NULL;
}

const char *lj_str(const LJ_Value *obj, const char *key)
{
    const LJ_Value *m = lj_get(obj, key);
    return (m && m->type == LJ_STRING) ? m->text : NULL;
}

long long lj_num(const LJ_Value *obj, const char *key, long long fallback)
{
    const LJ_Value *m = lj_get(obj, key);
    if (!m || m->type != LJ_NUMBER || !m->text)
        return fallback;
    return (long long)strtod(m->text, NULL);
}

int lj_bool(const LJ_Value *obj, const char *key, int fallback)
{
    const LJ_Value *m = lj_get(obj, key);
    if (!m || m->type != LJ_BOOL)
        return fallback;
    return m->boolean;
}

const LJ_Value *lj_first(const LJ_Value *arr)
{
    return arr ? arr->first : NULL;
}

const LJ_Value *lj_next(const LJ_Value *item)
{
    return item ? item->next : NULL;
}
