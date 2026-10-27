#ifndef CNC_LAUNCHER_SKIN_H
#define CNC_LAUNCHER_SKIN_H

/* CPU-only Enhanced-menu skin. Geometry is in the launcher's legacy logical
 * coordinates; each logical pixel occupies three native pixels. Typography
 * retains the menu's original two-times glyph scale independently. */
enum { LS_TEXT=0, LS_MUTED=1, LS_ACCENT=2, LS_DISABLED=3 };
int ls_load(const char *path);
int ls_enabled(void);
void ls_free(void);
void ls_begin(void);
unsigned char *ls_pixels(void);
int ls_width(void);
int ls_height(void);
int ls_text_width(const char *text);
void ls_image(const char *id,int native_x,int native_y);
/* Supplied brand content stays RGBA and separate from the shared chrome pack. */
void ls_rgba_image(const unsigned char *rgba,int w,int h,int native_x,int native_y);
void ls_clip(int x,int y,int w,int h);
void ls_clip_reset(void);
void ls_text(const char *text,int x,int y,int role,int align);
void ls_button(int x,int y,int w,int h,int pressed,int hot,int disabled,int active);
void ls_well(int x,int y,int w,int h);
void ls_outline(int x,int y,int w,int h,int role);
void ls_gauge(int x,int y,int w,int h,int frac,int phase);
void ls_plus(int cx,int cy,int arm,int thick,int role);
void ls_check(int x,int y,int size,int checked,int hot,int disabled);
void ls_gear(int x,int y,int w,int h,int pressed,int hot,int disabled);
/* Explicit native-pixel primitive for original indexed artwork/cursor adapters. */
void ls_pixel_rect(int x,int y,int w,int h,unsigned colour);
void ls_pixel_rgba(int x,int y,unsigned r,unsigned g,unsigned b,unsigned a);
int ls_native_x(int logical_x);
int ls_native_y(int logical_y);

#endif
