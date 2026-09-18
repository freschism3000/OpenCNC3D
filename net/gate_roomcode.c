/*
 * gate_roomcode.c -- the room code, exhaustively where it can be and heavily where it
 * cannot.
 *
 * A room code is a relayed host's whole address. Two properties matter and neither is
 * checkable by looking at a screenshot:
 *
 *   IT ROUND TRIPS. A code that decodes to a different id than it was made from means
 *   two rooms wearing one code, with nothing on either screen to say so. Every boundary
 *   value is checked, and a hundred thousand random ids besides.
 *
 *   IT REFUSES RATHER THAN GUESSES. A code arrives by being read out loud or typed from
 *   a message, so the interesting inputs are the WRONG ones: five symbols, seven, a
 *   missing sigil, a letter outside the alphabet. Every one of those must be turned away
 *   rather than half-read, because a prefix that silently decodes sends a player to a
 *   room that does not exist and tells them nobody answered.
 *
 * The reading folds are here too. I and L read as 1 and O as 0, so the three ways a
 * person mis-copies a code all land on the right room; U is not in the alphabet at all.
 *
 * The draws are checked for range and for not being obviously constant. This is not a
 * statistical test of the platform's entropy -- it cannot be, in a gate -- it is a test
 * that we asked for entropy and did something correct with the answer.
 */
#include "roomcode.h"

#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;

static void check(int cond, const char* what)
{
    checks++;
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

static void check_msg(int cond, const char* what, const char* detail)
{
    checks++;
    if (!cond) { printf("FAIL %s (%s)\n", what, detail); fails++; }
}

int main(void)
{
    char text[RC_TEXT_MAX];
    unsigned long id, back;
    int i;

    /* ---- shape ---- */
    check(rc_encode(1ul, text) == 1, "the smallest id encodes");
    check(strlen(text) == 8, "a code is eight characters");
    check(text[0] == '#', "a code starts with the sigil");
    check(text[4] == '-', "and is grouped three and three");
    check(rc_encode(RC_HOST_ID_MAX, text) == 1, "the largest host id encodes");
    check(rc_encode(0ul, text) == 0, "zero is refused: it is not a peer id");
    check(rc_encode(RC_HOST_ID_MAX + 1ul, text) == 0,
          "an id one past the ceiling is REFUSED, not truncated to a different room");
    check(rc_encode(0xFFFFFFFFul, text) == 0, "and neither is the broadcast id encodable");

    /* ---- round trip: the boundaries, then volume ---- */
    {
        static const unsigned long EDGE[] = {
            1ul, 2ul, 31ul, 32ul, 33ul, 1023ul, 1024ul,
            0x1FFFFFFFul, 0x20000000ul, 0x3FFFFFFEul, RC_HOST_ID_MAX
        };
        int bad = 0;
        for (i = 0; i < (int)(sizeof EDGE / sizeof EDGE[0]); i++) {
            if (!rc_encode(EDGE[i], text)) { bad++; continue; }
            if (!rc_decode(text, &back) || back != EDGE[i]) bad++;
        }
        check(bad == 0, "every boundary id survives encode and decode");
    }
    {
        /* A hundred thousand, walked rather than drawn, so this gate says the same thing
           on every machine and every run. The stride is odd and coprime with the space,
           so it visits the whole range rather than one residue class. */
        unsigned long v = 1ul;
        int bad = 0;
        for (i = 0; i < 100000; i++) {
            if (!rc_encode(v, text)) { bad++; }
            else if (!rc_decode(text, &back) || back != v) bad++;
            v = (v + 10739ul) & RC_HOST_ID_MAX;
            if (v == 0ul) v = 1ul;
        }
        check(bad == 0, "and so do a hundred thousand more");
    }

    /* ---- the reading folds ---- */
    check(rc_decode("#K7M-3QX", &id) == 1, "a code reads");
    check(rc_decode("#k7m-3qx", &back) == 1 && back == id, "lower case reads the same");
    check(rc_decode("#K7M3QX", &back) == 1 && back == id, "so does one with no hyphen");
    check(rc_decode("#K7M 3QX", &back) == 1 && back == id, "and one with a space");
    check(rc_decode("  #K7M-3QX", &back) == 1 && back == id, "leading spaces are ignored");
    {
        /* 1 and 0 are the two symbols a person can mis-copy in three ways each. */
        unsigned long one, oh;
        check(rc_decode("#111-111", &one) == 1, "a code of ones reads");
        check(rc_decode("#ILI-LIL", &back) == 1 && back == one,
              "I and L read as 1, which is how a code survives being read aloud");
        check(rc_decode("#000-000", &oh) == 0, "a code of zeros is not a room");
        check(rc_decode("#O0O-0O0", &back) == 0, "and O folds to 0, so nor is that one");
        check(rc_decode("#01O-OO1", &back) == 1, "O inside a code still reads as zero");
    }

    /* ---- refusals ---- */
    check(rc_decode("K7M-3QX", &back) == 0, "without the sigil it is a hostname, not a code");
    check(rc_decode("#K7M-3Q", &back) == 0, "five symbols is a typo and is refused");
    check(rc_decode("#K7M-3QXA", &back) == 0, "seven symbols likewise");
    check(rc_decode("#K7M-3QU", &back) == 0, "U is not in the alphabet");
    check(rc_decode("#K7M-3Q!", &back) == 0, "punctuation is refused");
    check(rc_decode("#", &back) == 0, "a bare sigil is refused");
    check(rc_decode("", &back) == 0, "and so is nothing at all");
    check(rc_decode("#      ", &back) == 0, "spaces are not symbols");
    check(rc_decode(NULL, &back) == 0, "a null string is refused rather than followed");

    /* ---- the draws ---- */
    {
        unsigned long seen_or = 0ul, seen_and = 0xFFFFFFFFul;
        int out_of_range = 0, zero = 0, same_as_last = 0;
        unsigned long last = 0ul;
        for (i = 0; i < 20000; i++) {
            id = rc_draw_host_id();
            if (id == 0ul) zero++;
            if (id > RC_HOST_ID_MAX) out_of_range++;
            if (i && id == last) same_as_last++;
            last = id;
            seen_or |= id;
            seen_and &= id;
            if (!rc_encode(id, text) || !rc_decode(text, &back) || back != id)
                out_of_range++;
        }
        check_msg(zero == 0, "a drawn host id is never zero", "zero is not a peer id");
        check(out_of_range == 0, "a drawn host id always fits a code and round trips");
        check(same_as_last == 0, "consecutive draws differ");
        check_msg((seen_or & RC_HOST_ID_MAX) == RC_HOST_ID_MAX,
                  "every bit of a host id is seen set", "a stuck-low bit halves the space");
        check_msg(seen_and == 0ul, "and every bit is seen clear",
                  "a stuck-high bit is the same fault the other way up");
    }
    {
        unsigned long seen_or = 0ul, seen_and = 0xFFFFFFFFul;
        int bad = 0;
        for (i = 0; i < 20000; i++) {
            id = rc_draw_peer_id();
            if (id == 0ul || id == 0xFFFFFFFFul) bad++;
            seen_or |= id;
            seen_and &= id;
        }
        check(bad == 0, "a drawn peer id is never one of the two reserved ids");
        check_msg(seen_or == 0xFFFFFFFFul, "a peer id uses the FULL 32 bits",
                  "a peer id is never written down, so it is not limited to thirty");
        check(seen_and == 0ul, "and every bit of one is seen clear");
    }

    printf("%s: %d checks, %d failure(s)\n",
           fails ? "ROOMCODE FAILED" : "ROOMCODE OK", checks, fails);
    return fails ? 1 : 0;
}
