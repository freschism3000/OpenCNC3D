/*
 * ljson.h -- just enough JSON to read cnc3dgame.com's two API routes.
 *
 * WHY A PARSER AND NOT strstr(). The launcher decides which 500 MB file to
 * download and then overwrites a player's game with it, from a document it did
 * not write. Scanning for `"tag":"` works until an asset is called
 * `something-"tag":"-weird.zip`, or until the site pretty-prints its output, or
 * until a field moves inside a nested object with the same key name. None of
 * those are likely; all of them are silent. This is two hundred lines that
 * either parse a document or refuse it.
 *
 * The subset is the whole of JSON except numbers keep their text (the launcher
 * wants a long long or a string, never a double) and there is no UTF-16 surrogate
 * pairing: a \uXXXX escape below 0x80 becomes that byte and anything above it
 * becomes '?'. The two routes this reads are ASCII, and a changelog with a
 * curly quote in it should degrade to a question mark rather than to a parser
 * that has to be trusted with encoding.
 */

#ifndef LJSON_H
#define LJSON_H

typedef enum
{
    LJ_NULL = 0,
    LJ_BOOL,
    LJ_NUMBER,
    LJ_STRING,
    LJ_ARRAY,
    LJ_OBJECT
} LJ_Type;

typedef struct LJ_Value LJ_Value;

struct LJ_Value
{
    LJ_Type type;
    char *text;      /* strings: the decoded text. numbers: the digits as written */
    int boolean;
    LJ_Value *first; /* arrays and objects: the first child      */
    LJ_Value *next;  /* the next sibling                          */
    char *key;       /* object members only                       */
};

/* Parse a whole document. Returns NULL and fills `err` on anything malformed;
 * a partial document is never returned, because a half-read manifest is how a
 * launcher ends up downloading the wrong file. */
LJ_Value *lj_parse(const char *text, char *err, int errlen);
void lj_free(LJ_Value *v);

/* Member of an object, or NULL. Never returns a value of the wrong type: ask for
 * what you want and check. */
const LJ_Value *lj_get(const LJ_Value *obj, const char *key);
const char *lj_str(const LJ_Value *obj, const char *key);     /* NULL if absent */
long long lj_num(const LJ_Value *obj, const char *key, long long fallback);
int lj_bool(const LJ_Value *obj, const char *key, int fallback);

/* Walk an array: lj_first then lj_next, both NULL-safe. */
const LJ_Value *lj_first(const LJ_Value *arr);
const LJ_Value *lj_next(const LJ_Value *item);

#endif /* LJSON_H */
