/* wsadpcm.c -- common/auduncmp.cpp Audio_Unzap, tables and control flow unchanged. */

#include "wsadpcm.h"

static const signed char ws_tab2[4] = {-2, -1, 0, 1};

static const signed char ws_tab4[16] = {-9, -8, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 8};

static int ws_clamp(int x)
{
    if (x > 255)
        return 255;
    if (x < 0)
        return 0;
    return x;
}

int ws_unzap(const unsigned char *src, int srclen, unsigned char *dst, int outbytes)
{
    const unsigned char *sp = src;
    const unsigned char *send = src + srclen;
    unsigned char *dp = dst;
    int sample = 0x80;
    int remaining = outbytes;

    while (remaining > 0 && sp < send) {
        unsigned int shifted = (unsigned int)(*sp++) << 2;
        int code = (int)((shifted & 0xFF00u) >> 8);
        signed char count = (signed char)((shifted & 0x00FFu) >> 2);
        int n;

        switch (code) {
        case 2:
            if (count & 0x20) {
                /* Six bit signed delta, sign extended by the shift pair. The original
                 * relies on `count` being a signed char for exactly this. */
                int d = (int)(signed char)(count << 3);
                sample += d >> 3;
                *dp++ = (unsigned char)ws_clamp(sample);
                remaining--;
            } else {
                for (n = (int)count + 1; n > 0 && remaining > 0 && sp < send; n--) {
                    *dp++ = *sp++;
                    remaining--;
                }
                sample = (int)*(sp - 1);
            }
            break;

        case 1: /* 4 bit deltas, two samples per byte */
            for (n = (int)count + 1; n > 0 && remaining > 0 && sp < send; n--) {
                unsigned char c = *sp++;
                sample += ws_tab4[c & 0x0F];
                *dp++ = (unsigned char)ws_clamp(sample);
                sample += ws_tab4[c >> 4];
                *dp++ = (unsigned char)ws_clamp(sample);
                remaining -= 2;
            }
            break;

        case 0: /* 2 bit deltas, four samples per byte */
            for (n = (int)count + 1; n > 0 && remaining > 0 && sp < send; n--) {
                unsigned char c = *sp++;
                sample += ws_tab2[c & 0x03];
                *dp++ = (unsigned char)ws_clamp(sample);
                sample += ws_tab2[(c >> 2) & 0x03];
                *dp++ = (unsigned char)ws_clamp(sample);
                sample += ws_tab2[(c >> 4) & 0x03];
                *dp++ = (unsigned char)ws_clamp(sample);
                sample += ws_tab2[(c >> 6) & 0x03];
                *dp++ = (unsigned char)ws_clamp(sample);
                remaining -= 4;
            }
            break;

        default: /* run of the held sample */
            n = (int)count + 1;
            if (n > remaining)
                n = remaining;
            while (n-- > 0) {
                *dp++ = (unsigned char)ws_clamp(sample);
                remaining--;
            }
            break;
        }
    }

    return (int)(dp - dst);
}
