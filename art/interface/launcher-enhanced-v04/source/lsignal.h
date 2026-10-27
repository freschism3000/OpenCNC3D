#ifndef CNC_LAUNCHER_SIGNAL_H
#define CNC_LAUNCHER_SIGNAL_H

#define LS_SIGNAL_PERIOD_MS 16000u

/* Apply once to a freshly composed straight-RGBA native canvas, before the
 * pointer. Deterministic colour-only transmission treatment; alpha and spatial
 * coordinates are never changed. Rebuild the base canvas before the next call. */
void ls_signal_apply(unsigned char *rgba,int w,int h,unsigned elapsed_ms);

#endif
