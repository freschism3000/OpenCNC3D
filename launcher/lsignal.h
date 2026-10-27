#ifndef CNC_LAUNCHER_SIGNAL_H
#define CNC_LAUNCHER_SIGNAL_H

#define LS_SIGNAL_PERIOD_MS 16000u
#define LS_SIGNAL_CLICK_MS 420u
#define LS_SIGNAL_NO_CLICK (~0u)

/* Apply once to a freshly composed straight-RGBA native canvas, before pointer
 * rendering. Deterministic colour-only EVA scan/rail/data treatment; alpha and
 * spatial coordinates never change. Rebuild the base canvas before each call.
 * Global green drive is capped at 32, with bright bitmap lettering capped at 2. */
void ls_signal_apply(unsigned char *rgba,int w,int h,unsigned elapsed_ms);

/* Native-pixel control rectangle. Call after the global pass for interactive
 * targets. Hover carries a travelling green bevel, pressed is a tactile energy
 * accent, and click_age_ms drives a 420 ms outward pulse; LS_SIGNAL_NO_CLICK
 * suppresses that pulse. No effect is applied for a wholly idle target.
 * Edge drive caps at 50; colour targets avoid additive stacking on grey chrome,
 * protect saturated logo colours, and leave black shadows/alpha untouched. */
void ls_signal_react(unsigned char *rgba,int w,int h,int x,int y,int rw,int rh,
                     unsigned elapsed_ms,int hot,int pressed,unsigned click_age_ms);

/* Lower-energy idle indication inside the existing bevel: a two-row packet and
 * a short corner acknowledgement, staggered by stable control identity. Call
 * only for visible idle controls/modules, after the global pass. Closed over
 * eight seconds, with a maximum drive of 18 and no new silhouette/alpha pixels. */
void ls_signal_idle(unsigned char *rgba,int w,int h,int x,int y,int rw,int rh,
                    unsigned elapsed_ms,unsigned identity);

#endif
