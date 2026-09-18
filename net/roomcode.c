/* net/roomcode.c -- see roomcode.h for what a room code is and why it exists. */

/* BEFORE EVERY INCLUDE, and that is the whole reason it is the first line of the file.
   rand_s is only declared when _CRT_RAND_S is defined ahead of <stdlib.h>, and stdlib is
   pulled in by more than one header here -- so defining it further down does nothing at
   all: the include guard swallows the second include and rand_s falls back to an implicit
   declaration, which compiles, links by luck on one calling convention, and is a bug
   waiting for a platform to change its mind. The cross-compiler said so the first time
   this file was built for Windows.
   It costs nothing to define on a platform that has never heard of it. */
#define _CRT_RAND_S

#include "roomcode.h"

#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
  /* rand_s lives in msvcrt and needs no extra link library; the Windows link line is
     -lws2_32 and nothing else, and this must not add to it. */
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
  /* arc4random_buf: no file descriptor, no failure mode, seeded by the kernel. */
#else
  #include <stdio.h>
#endif

/* Crockford's alphabet: no I, no L, no O, no U. */
static const char RC_ALPHA[32] = {
    '0','1','2','3','4','5','6','7','8','9',
    'A','B','C','D','E','F','G','H','J','K',
    'M','N','P','Q','R','S','T','V','W','X','Y','Z'
};

/* One symbol -> its value, or -1. The folds are the whole reason a person can read a
   code down a telephone: I and L look like 1 in most faces, O looks like 0, and a
   reader who "corrects" one of those should still land on the right room. */
static int rc_value(int c)
{
    int i;
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (c == 'I' || c == 'L') return 1;
    if (c == 'O') return 0;
    for (i = 0; i < 32; i++) {
        if (RC_ALPHA[i] == (char)c) return i;
    }
    return -1;
}

int rc_encode(unsigned long id, char out[RC_TEXT_MAX])
{
    char sym[6];
    int i;
    if (!out) return 0;
    /* REFUSED RATHER THAN TRUNCATED. An id above the six-symbol ceiling would encode to
       a code that decodes to a DIFFERENT id, which is the one failure a room code must
       never have: two rooms, one code, and no way to tell from either screen. */
    if (id == 0ul || id > RC_HOST_ID_MAX) return 0;
    for (i = 5; i >= 0; i--) {
        sym[i] = RC_ALPHA[(int)(id & 31ul)];
        id >>= 5;
    }
    out[0] = '#';
    out[1] = sym[0]; out[2] = sym[1]; out[3] = sym[2];
    out[4] = '-';
    out[5] = sym[3]; out[6] = sym[4]; out[7] = sym[5];
    out[8] = '\0';
    return 1;
}

int rc_decode(const char* text, unsigned long* out_id)
{
    unsigned long v = 0ul;
    int n = 0;
    if (!text || !out_id) return 0;
    while (*text == ' ') text++;
    if (*text != '#') return 0;              /* the sigil is what makes this not a host */
    text++;
    for (; *text; text++) {
        int d;
        if (*text == '-' || *text == ' ') continue;   /* grouping is decoration */
        d = rc_value((unsigned char)*text);
        if (d < 0) return 0;
        if (n >= 6) return 0;                /* too long: refuse, never take a prefix */
        v = (v << 5) | (unsigned long)d;
        n++;
    }
    if (n != 6) return 0;                    /* too short is a typo, not a shorter room */
    if (v == 0ul) return 0;
    *out_id = v;
    return 1;
}

/* ---- entropy ----------------------------------------------------------------------
   There is no random number generator anywhere else in net/, so this is the whole of it.
   Note that Tier 1 never reaches here: net/net_udp.c makes WIN98 a hard #error, so
   multiplayer does not exist on that platform and cannot ask for entropy. That is what
   keeps this a two-platform question. */
static int rc_random_bytes(unsigned char* out, int n)
{
#if defined(_WIN32)
    int i;
    for (i = 0; i < n; i += 4) {
        unsigned int r = 0;
        int k, have;
        if (rand_s(&r) != 0) return 0;
        have = (n - i) < 4 ? (n - i) : 4;
        for (k = 0; k < have; k++) out[i + k] = (unsigned char)((r >> (k * 8)) & 0xFF);
    }
    return 1;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    arc4random_buf(out, (size_t)n);
    return 1;
#else
    FILE* f = fopen("/dev/urandom", "rb");
    size_t got;
    if (!f) return 0;
    got = fread(out, 1, (size_t)n, f);
    fclose(f);
    return got == (size_t)n;
#endif
}

static unsigned long rc_draw32(void)
{
    unsigned char b[4];
    if (!rc_random_bytes(b, 4)) return 0ul;
    return ((unsigned long)b[0] << 24) | ((unsigned long)b[1] << 16)
         | ((unsigned long)b[2] << 8)  | (unsigned long)b[3];
}

unsigned long rc_draw_host_id(void)
{
    /* MASK, THEN REJECT ZERO. Masking to thirty bits is exact -- every value in
       0..0x3FFFFFFF is equally likely -- so the only value to throw away is 0. A modulo
       fold would have made the low ids commoner, and the id is the only thing between a
       room and a stranger sweeping the space. A bounded loop because an entropy source
       that keeps answering zero is broken, and spinning for ever on it would hang the
       host at the moment it opened a room. */
    int tries;
    for (tries = 0; tries < 64; tries++) {
        const unsigned long v = rc_draw32() & RC_HOST_ID_MAX;
        if (v != 0ul) return v;
    }
    return 0ul;      /* the caller refuses to open a relayed room on this */
}

unsigned long rc_draw_peer_id(void)
{
    int tries;
    for (tries = 0; tries < 64; tries++) {
        const unsigned long v = rc_draw32();
        if (v != 0ul && v != 0xFFFFFFFFul) return v;
    }
    return 0ul;
}
