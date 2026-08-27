/* w98_gfx.c -- see w98_gfx.h. Windows 98 window + DIB framebuffer + clock + keys. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w98_gfx.h"

static HWND       g_wnd;
static HDC        g_memdc;
static HBITMAP    g_dib;
static HBITMAP    g_olddib;
static W98_Frame  g_fb;
static int        g_quit;
static unsigned char g_down[256];
static unsigned char g_hit[256];
static int  g_mx, g_my;
static unsigned char g_mdown[2], g_mhit[2];
static LARGE_INTEGER g_qpf, g_qpc0;
static DWORD      g_tick0;      /* fallback clock if QPC is unavailable */
static int        g_have_qpc;

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_CLOSE:
        g_quit = 1;
        return 0;

    case WM_DESTROY:
        g_quit = 1;
        PostQuitMessage(0);
        return 0;

    /* SYSKEY as well as KEY so that Alt combinations do not silently vanish, and so
     * ESC still quits when the window has lost and regained focus. */
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp < 256)
        {
            if (!g_down[wp]) g_hit[wp] = 1;
            g_down[wp] = 1;
        }
        if (wp == VK_ESCAPE) g_quit = 1;
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wp < 256) g_down[wp] = 0;
        return 0;

    /* The framebuffer is authoritative, so a repaint is just another present. Without
     * this the window is empty whenever Windows asks for a redraw between frames. */
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g_memdc) BitBlt(dc, 0, 0, g_fb.w, g_fb.h, g_memdc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }

    /* Stops Windows painting the background white between frames, which on a slow
     * software renderer is a very visible flash. */
    case WM_ERASEBKGND:
        return 1;

    /* Client pixels, which is exactly the framebuffer's space, so nothing has to be
     * scaled or offset between what the player points at and what was drawn. */
    case WM_MOUSEMOVE:
        g_mx = (short)LOWORD(lp);
        g_my = (short)HIWORD(lp);
        return 0;

    case WM_LBUTTONDOWN:
        if (!g_mdown[0]) g_mhit[0] = 1;
        g_mdown[0] = 1;
        SetCapture(h);
        return 0;
    case WM_LBUTTONUP:
        g_mdown[0] = 0;
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        if (!g_mdown[1]) g_mhit[1] = 1;
        g_mdown[1] = 1;
        return 0;
    case WM_RBUTTONUP:
        g_mdown[1] = 0;
        return 0;

    default:
        break;
    }
    return DefWindowProcA(h, m, wp, lp);
}

int w98_open(const char *title, int w, int h, char *err, int errlen)
{
    WNDCLASSA wc;
    BITMAPINFO bi;
    RECT r;
    HDC screen;
    void *bits = NULL;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "CNC3D_W98";
    if (!RegisterClassA(&wc))
    {
        _snprintf(err, errlen, "RegisterClassA failed (%lu)", (unsigned long)GetLastError());
        return 0;
    }

    /* AdjustWindowRect so that the CLIENT area is exactly w by h. Without it the
     * caption and border eat into the framebuffer and every pixel is off by a few. */
    r.left = 0; r.top = 0; r.right = w; r.bottom = h;
    AdjustWindowRect(&r, style, FALSE);

    g_wnd = CreateWindowA("CNC3D_W98", title, style,
                          CW_USEDEFAULT, CW_USEDEFAULT,
                          r.right - r.left, r.bottom - r.top,
                          NULL, NULL, wc.hInstance, NULL);
    if (!g_wnd)
    {
        _snprintf(err, errlen, "CreateWindowA failed (%lu)", (unsigned long)GetLastError());
        return 0;
    }

    screen = GetDC(g_wnd);
    g_memdc = CreateCompatibleDC(screen);

    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          /* negative: top-down, so px[y*w+x] */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    g_dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(g_wnd, screen);
    if (!g_dib || !bits)
    {
        _snprintf(err, errlen, "CreateDIBSection %dx%d failed (%lu)",
                  w, h, (unsigned long)GetLastError());
        return 0;
    }
    g_olddib = (HBITMAP)SelectObject(g_memdc, g_dib);

    g_fb.w  = w;
    g_fb.h  = h;
    g_fb.px = (unsigned int *)bits;
    memset(g_fb.px, 0, (size_t)w * (size_t)h * 4);

    g_have_qpc = QueryPerformanceFrequency(&g_qpf) && g_qpf.QuadPart > 0;
    if (g_have_qpc) QueryPerformanceCounter(&g_qpc0);
    g_tick0 = GetTickCount();

    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);
    SetForegroundWindow(g_wnd);
    return 1;
}

void w98_close(void)
{
    if (g_memdc)
    {
        if (g_olddib) SelectObject(g_memdc, g_olddib);
        DeleteDC(g_memdc);
        g_memdc = NULL;
    }
    if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = NULL; }
}

int w98_pump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT) g_quit = 1;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return !g_quit;
}

W98_Frame *w98_framebuffer(void) { return &g_fb; }

void w98_present(void)
{
    HDC dc;
    if (!g_wnd || !g_memdc) return;
    dc = GetDC(g_wnd);
    /* GdiFlush is what makes the DIB section writes we did with the CPU visible to
     * GDI before it reads them. Without it Windows is entitled to blit stale bits. */
    GdiFlush();
    BitBlt(dc, 0, 0, g_fb.w, g_fb.h, g_memdc, 0, 0, SRCCOPY);
    ReleaseDC(g_wnd, dc);
}

int w98_key(int vk) { return (vk >= 0 && vk < 256) ? g_down[vk] : 0; }

int w98_key_hit(int vk)
{
    int r;
    if (vk < 0 || vk >= 256) return 0;
    r = g_hit[vk];
    g_hit[vk] = 0;
    return r;
}

void w98_mouse(int *x, int *y) { *x = g_mx; *y = g_my; }
int  w98_mouse_down(int btn) { return (btn >= 0 && btn < 2) ? g_mdown[btn] : 0; }
int  w98_mouse_hit(int btn)
{
    int r;
    if (btn < 0 || btn > 1) return 0;
    r = g_mhit[btn];
    g_mhit[btn] = 0;
    return r;
}

double w98_seconds(void)
{
    LARGE_INTEGER now;
    if (g_have_qpc)
    {
        QueryPerformanceCounter(&now);
        return (double)(now.QuadPart - g_qpc0.QuadPart) / (double)g_qpf.QuadPart;
    }
    /* 55 ms granularity, which is not good enough to animate from, but the program
     * should still run rather than divide by zero. Anything that cares says so. */
    return (double)(GetTickCount() - g_tick0) / 1000.0;
}

unsigned __int64 w98_rdtsc(void)
{
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned __int64)hi << 32) | lo;
}

void w98_set_title(const char *s)
{
    if (g_wnd) SetWindowTextA(g_wnd, s);
}

int w98_save_bmp(const char *path)
{
    /* 24 bit, BOTTOM UP. Bottom-up on purpose: a top-down BMP file (negative height)
     * crashes Win9x LoadImage, and more to the point every tool on the Mac reads a
     * bottom-up file without arguing. */
    FILE *f;
    unsigned char hdr[54];
    int rowbytes = ((g_fb.w * 3) + 3) & ~3;
    long imgsize  = (long)rowbytes * g_fb.h;
    long filesize = 54 + imgsize;
    unsigned char *row;
    int x, y;

    if (!g_fb.px) return 0;
    row = (unsigned char *)malloc((size_t)rowbytes);
    if (!row) return 0;
    f = fopen(path, "wb");
    if (!f) { free(row); return 0; }

    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2]  = (unsigned char)(filesize      ); hdr[3]  = (unsigned char)(filesize >>  8);
    hdr[4]  = (unsigned char)(filesize >> 16); hdr[5]  = (unsigned char)(filesize >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (unsigned char)(g_fb.w      ); hdr[19] = (unsigned char)(g_fb.w >>  8);
    hdr[20] = (unsigned char)(g_fb.w >> 16); hdr[21] = (unsigned char)(g_fb.w >> 24);
    hdr[22] = (unsigned char)(g_fb.h      ); hdr[23] = (unsigned char)(g_fb.h >>  8);
    hdr[24] = (unsigned char)(g_fb.h >> 16); hdr[25] = (unsigned char)(g_fb.h >> 24);
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = (unsigned char)(imgsize      ); hdr[35] = (unsigned char)(imgsize >>  8);
    hdr[36] = (unsigned char)(imgsize >> 16); hdr[37] = (unsigned char)(imgsize >> 24);
    fwrite(hdr, 1, 54, f);

    for (y = g_fb.h - 1; y >= 0; --y)
    {
        const unsigned int *src = g_fb.px + (long)y * g_fb.w;
        memset(row, 0, (size_t)rowbytes);
        for (x = 0; x < g_fb.w; ++x)
        {
            unsigned int p = src[x];
            row[x * 3 + 0] = (unsigned char)( p        & 0xFF); /* B */
            row[x * 3 + 1] = (unsigned char)((p >>  8) & 0xFF); /* G */
            row[x * 3 + 2] = (unsigned char)((p >> 16) & 0xFF); /* R */
        }
        fwrite(row, 1, (size_t)rowbytes, f);
    }
    fclose(f);
    free(row);
    return 1;
}
