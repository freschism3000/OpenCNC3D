/*
 * w98_gfx.h -- the Windows 98 platform layer: a window, a framebuffer, a clock
 *              and a keyboard. This is what stands in for SDL2 on Tier 1.
 *
 * SDL2 requires Windows XP or later and will not load on Windows 98 at all, so the
 * Tier 1 build cannot use it. Everything SDL was doing for us that Win98 still needs
 * is here, and it is deliberately small: RegisterClassA, CreateWindowA, one
 * CreateDIBSection, one BitBlt, WM_KEYDOWN, and QueryPerformanceCounter.
 *
 * There is NO OpenGL context and no Glide context in this file. The renderer above it
 * rasterises into W98_Frame.px with the CPU and this layer only puts that memory on
 * the screen. That is the whole point of the software tier: the display path has no
 * 3D driver in it, so it cannot fail for 3D reasons.
 *
 * Pure C89 and Win32 only. No CRT beyond stdio/string, so it links against the real
 * msvcrt.dll on Win98 (see tools/win98/build.sh for why that sentence is load bearing).
 */

#ifndef W98_GFX_H
#define W98_GFX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int w, h;
    /* 32 bits per pixel, 0x00RRGGBB, TOP DOWN: px[y * w + x] is row y from the top.
     * Top-down is a negative biHeight on the DIB section, which Windows 95 and later
     * support for a DIB SECTION. (The Win9x top-down trap is LoadImage on a BMP FILE,
     * which is a different thing and which w98_save_bmp avoids by writing bottom-up.) */
    unsigned int *px;
} W98_Frame;

/* Opens the window and allocates the framebuffer. Returns 0 on failure and fills
 * `err` with something a human can act on. */
int w98_open(const char *title, int w, int h, char *err, int errlen);
void w98_close(void);

/* Drains the message queue. Returns 0 once the user has asked to quit (window closed
 * or ESC pressed), which is the loop's exit condition. */
int w98_pump(void);

W98_Frame *w98_framebuffer(void);

/* Puts the framebuffer on the screen. One BitBlt, no stretching. */
void w98_present(void);

/* Keyboard. w98_key is the live state; w98_key_hit consumes an edge, so it fires once
 * per physical press however long the frame took. Argument is a Win32 VK_ code. */
int w98_key(int vk);
int w98_key_hit(int vk);

/* Mouse, in CLIENT pixels, which is the same space as the framebuffer. btn is 0 left,
 * 1 right. w98_mouse_hit consumes a press edge so a click fires once however long the
 * frame took, which matters at ten frames a second. */
void w98_mouse(int *x, int *y);
int  w98_mouse_down(int btn);
int  w98_mouse_hit(int btn);

/* Monotonic seconds since w98_open.
 *
 * This is QueryPerformanceCounter, NOT GetTickCount. On Windows 98 GetTickCount only
 * advances about 18 times a second, which is coarser than a frame and makes any
 * motion clocked off it visibly strobe even at high frame rates. QPC on Win9x is the
 * 1.193182 MHz PIT, which is plenty. Measured on the box: freq = 1193180. */
double w98_seconds(void);

/* CPU timestamp counter, for measuring the rasteriser in cycles rather than seconds.
 * Returns 0 if the CPU has no RDTSC (anything pre-Pentium). */
unsigned __int64 w98_rdtsc(void);

/* Writes the framebuffer as a 24 bit bottom-up BMP. This is how a frame gets off the
 * box and onto the Mac to be looked at, which project rule 7 requires before anyone
 * claims the picture is right. */
int w98_save_bmp(const char *path);

void w98_set_title(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* W98_GFX_H */
