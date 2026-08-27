/*
 * dosbar.h -- the 1995 MS-DOS Command & Conquer sidebar, as a lift-and-drop C module.
 *
 * Draws into an 8-bit palettised 320x200 surface exactly the way the DOS engine did,
 * because the sidebar is only correct in 8-bit: index 0 is transparent, the build
 * clock is a translucency LOOKUP against the destination pixel, and the fonts resolve
 * a 4bpp pixel CLASS through a 16 entry font palette at print time.
 *
 * The only thing that ever reaches the GPU is the finished surface, converted once
 * with db_surface_to_rgba(). That is a single texture upload and one textured quad:
 * OpenGL 1.1 and Glide can both do it, no shaders, no GLU, nothing per-pixel on the
 * card. All layout and compositing is CPU side.
 *
 * Nothing in here calls malloc during drawing and nothing touches the filesystem
 * except db_pack_load().
 */

#ifndef DOSBAR_H
#define DOSBAR_H

/* ------------------------------------------------------------------------ *
 * Layout. Every number is quoted from the engine, with its source.
 * Base resolution 320x200; in the engine this is factor == 0 / lo-res.
 * ------------------------------------------------------------------------ */

#define DB_SCREEN_W 320
#define DB_SCREEN_H 200

/* tab.cpp:263  Tab_Height = 8 * factor, factor = (width==320) ? 1 : 2 */
#define DB_TAB_HEIGHT 8
/* tab.cpp:257  Eva_Width = 80 * factor */
#define DB_EVA_WIDTH 80

/* radar.cpp:128-144, RadarClass::One_Time, Get_Resolution_Factor() == 0 */
#define DB_RAD_WIDTH 80    /* 80 << 0                       radar.cpp:128 */
#define DB_RAD_HEIGHT 70   /* 70 << 0                       radar.cpp:129 */
#define DB_RAD_X 240       /* SeenBuff width - RadWidth     radar.cpp:130 */
#define DB_RAD_Y 7         /* Get_Tab_Height() - (1 << 0)   radar.cpp:131 */
#define DB_RAD_OFF_X 4     /* 4 << 0                        radar.cpp:141 */
#define DB_RAD_OFF_Y 1     /* 1 << 0                        radar.cpp:142 */
#define DB_RAD_I_WIDTH 72  /* 72 << 0                       radar.cpp:143 */
#define DB_RAD_I_HEIGHT 69 /* 69 << 0                       radar.cpp:144 */
/* radar.h:135. RADAR.<house> is the activation animation; 22 is the settled frame
 * the map gets plotted over.  radar.cpp:430 draws it at (RadX, RadY + 1). */
#define DB_RADAR_ACTIVATED_FRAME 22

/* power.cpp:132-141, PowerClass::One_Time, factor == 0 */
#define DB_POW_X 240        /* SeenBuff width - RadWidth          power.cpp:132 */
#define DB_POW_Y 90         /* RadY + RadHeight + (13 << 0)       power.cpp:136 */
#define DB_POW_WIDTH 8      /* 8 << 0                             power.cpp:138 */
#define DB_POW_HEIGHT 110   /* SeenBuff height - PowY             power.cpp:139 */
#define DB_POW_LINE_SPACE 5 /* 5 << 0                             power.cpp:140 */
#define DB_POW_LINE_WIDTH 4 /* PowWidth - 4                       power.cpp:141 */

/* sidebar.h:68 SIDEBARWIDTH, sidebar.cpp:180-187 SidebarClass::One_Time */
#define DB_SIDEBARWIDTH 80
#define DB_SIDE_X 240      /* SeenBuff width - SideBarWidth     sidebar.cpp:181 */
#define DB_SIDE_Y 78       /* RadY + RadHeight + 1              sidebar.cpp:182 */
#define DB_SIDE_WIDTH 80   /* SeenBuff width - SideX            sidebar.cpp:183 */
#define DB_SIDE_HEIGHT 122 /* SeenBuff height - SideY           sidebar.cpp:184 */
#define DB_BUTTON_HEIGHT 9 /* 9 * factor                        sidebar.cpp:186 */
#define DB_TOP_HEIGHT 13   /* ButtonHeight + 4 == sidebar.h TOP_HEIGHT=13  :187 */

/* The shape clipping window, WINDOW_SIDEBAR.  sidebar.cpp:197-205.
 * The +1/-1 in the factor==1 branch exists so the raised box's top highlight line
 * at y = SIDE_Y+TOP_HEIGHT survives; it costs the top row of the first cameo. */
#define DB_WIN_X 248 /* SideX + PowWidth                            :197 */
#define DB_WIN_Y 92  /* SideY + TopHeight, then +1 when factor==1   :198,203 */
#define DB_WIN_W 80  /* SideWidth                                   :199 */
#define DB_WIN_H 95  /* MaxVisible*OBJECT_HEIGHT, then -1           :200,204 */

/* sidebar.h SideBarGeneralEnums:211-236 */
#define DB_MAX_BUILDABLES 30
#define DB_OBJECT_WIDTH 32
#define DB_OBJECT_HEIGHT 24
#define DB_STRIP_WIDTH 35
#define DB_MAX_VISIBLE 4
#define DB_BUTTON_WIDTH 16   /* mini scroll button              sidebar.h:226 */
#define DB_SBUTTON_HEIGHT 12 /* mini scroll button              sidebar.h:227 */
#define DB_UP_X_OFFSET 2     /* sidebar.h:222 (== sidebar.cpp:1248 X+spacing+1) */
#define DB_DOWN_X_OFFSET 18  /* sidebar.h:224 */
#define DB_UP_Y_OFFSET 97    /* MAX_VISIBLE*OBJECT_HEIGHT+1     sidebar.h:223 */
#define DB_COLUMNS 2

/* sidebar.cpp:1152-1153 StripClass::One_Time, factor == 0 */
#define DB_LEFT_EDGE_OFFSET 1 /* (STRIP_WIDTH - OBJECT_WIDTH) >> 1 */

/* Strip origins.  sidebar.cpp:214-217 and the sidebar.h enum. See the note in
 * dosbar.c: the two disagree by one pixel on column two and we follow sidebar.h. */
#define DB_COLUMN_ONE_X 248 /* SIDE_X + 8                       sidebar.h / :214 */
#define DB_COLUMN_TWO_X 283 /* COLUMN_ONE_X + ((SIDE_WIDTH-16)/2) + 3   sidebar.h */
#define DB_COLUMN_Y 92      /* SIDE_Y + TOP_HEIGHT + 1                     :215 */

/* sidebar.cpp:371-384 SidebarClass::Init_IO, else branch of Get_Resolution_Factor() */
#define DB_REPAIR_X 242
#define DB_REPAIR_Y 80
#define DB_REPAIR_W 32
#define DB_SELL_X 276 /* Repair->X + Repair->Width + 2 */
#define DB_SELL_W 20
#define DB_MAP_X 298 /* Upgrade->X + Upgrade->Width + 2 */
#define DB_MAP_W 20

/* factory.h:117 */
#define DB_STEP_COUNT 108

/* dialog.cpp Simple_Text_Print, factor 0, TPF_6POINT|TPF_NOSHADOW:
 *   xspace = 0; TPF_6POINT -> -1; TPF_NOSHADOW -> -1
 *   yspace = 1; TPF_NOSHADOW -> -2 */
#define DB_FONT6_XSPACING (-2)
#define DB_FONT6_YSPACING (-1)

/* The Westwood 16 colour GUI ramp, pinned from the engine in pal/palette_roles.json */
enum
{
    DB_TBLACK = 0, /* transparent: Buffer_Print and the shape blitter skip it */
    DB_PURPLE = 1,
    DB_CYAN = 2,
    DB_GREEN = 3,
    DB_LTGREEN = 4,
    DB_YELLOW = 5,
    DB_PINK = 6,
    DB_BROWN = 7,
    DB_RED = 8,
    DB_LTCYAN = 9,
    DB_LTBLUE = 10,
    DB_BLUE = 11,
    DB_BLACK = 12, /* opaque black */
    DB_DKGREY = 13,
    DB_LTGREY = 14,
    DB_WHITE = 15
};

/* dialog.cpp Draw_Box, ButtonColorsClassic. Filler, Shadow, Highlight, Corner. */
typedef enum
{
    DB_BOX_DOWN = 0,        /* {LTGREY, WHITE,  DKGREY, LTGREY} */
    DB_BOX_RAISED = 1,      /* {LTGREY, DKGREY, WHITE,  LTGREY} */
    DB_BOX_DIS_DOWN = 3,    /* {DKGREY, WHITE,  BLACK,  DKGREY} */
    DB_BOX_DIS_RAISED = 4   /* {DKGREY, BLACK,  WHITE,  LTGREY} */
} DB_BoxStyle;

/* sidebar.h:256-260 SideBarStipShapeEnums */
#define DB_SB_BLANK 0
#define DB_SB_FRAME 1

/* defines.h:1748-1758 PipEnum */
#define DB_PIP_READY 3
#define DB_PIP_HOLDING 4

/* ------------------------------------------------------------------------ *
 * The pack (see bake_dossidebar.py for the byte layout).
 * ------------------------------------------------------------------------ */

typedef struct
{
    char name[13];
    int frames, w, h;
    const unsigned char *pixels; /* frames * w * h palette indices */
} DB_Shape;

typedef struct
{
    char name[9];
    int nchars, maxw, maxh;
    const unsigned char *width; /* Char_Pixel_Width = width[c] + FontXSpacing */
    const unsigned char *ytop;
    const unsigned char *rows;
    const unsigned char *cells; /* nchars * maxw * maxh, values 0..3 */
} DB_Font;

typedef struct
{
    unsigned char *blob;
    long blobsize;
    const unsigned char *pal6;     /* 768, 6-bit DAC values */
    const unsigned char *pal8;     /* 768, widened v<<2 as the engine widens */
    const unsigned char *clocktab; /* 512: ghost_lookup[256] then fade[256] */
    DB_Shape *shapes;
    int nshapes;
    DB_Font *fonts;
    int nfonts;
} DB_Pack;

DB_Pack *db_pack_load(const char *path, char *err, int errlen);
void db_pack_free(DB_Pack *p);
const DB_Shape *db_shape(const DB_Pack *p, const char *name);
const DB_Font *db_font(const DB_Pack *p, const char *name);

/* ------------------------------------------------------------------------ *
 * The 8-bit surface and the engine primitives it supports.
 * ------------------------------------------------------------------------ */

typedef struct
{
    int w, h;
    unsigned char *px;
    int cx0, cy0, cx1, cy1; /* inclusive clip rectangle */
} DB_Surface;

void db_surface_init(DB_Surface *s, int w, int h, unsigned char *storage);
void db_clip_reset(DB_Surface *s);
void db_clip(DB_Surface *s, int x0, int y0, int x1, int y1);
void db_fill_rect(DB_Surface *s, int x0, int y0, int x1, int y1, unsigned char c);
void db_put_pixel(DB_Surface *s, int x, int y, unsigned char c);
void db_line_h(DB_Surface *s, int x0, int x1, int y, unsigned char c);
void db_line_v(DB_Surface *s, int x, int y0, int y1, unsigned char c);
void db_draw_box(DB_Surface *s, int x, int y, int w, int h, DB_BoxStyle style, int filled);
void db_draw_shape(DB_Surface *s, const DB_Shape *sh, int frame, int x, int y);
void db_draw_shape_centered(DB_Surface *s, const DB_Shape *sh, int frame, int cx, int cy);
void db_draw_shape_ghost(DB_Surface *s, const DB_Shape *sh, int frame, int x, int y,
                         const unsigned char *clocktab);

int db_string_width(const DB_Font *f, const char *text, int xspacing);
void db_print(DB_Surface *s, const DB_Font *f, const char *text, int x, int y,
              const unsigned char *fontpal, int xspacing);
void db_font_palette(unsigned char fp[16], unsigned char fore, unsigned char back);
void db_font_palette_grad(unsigned char fp[16], unsigned char fore, unsigned char back);

/* ------------------------------------------------------------------------ *
 * The state the sidebar renders. This mirrors what SidebarClass reads out of
 * StripClass::Buildables[] and FactoryClass, and nothing else.
 * ------------------------------------------------------------------------ */

typedef struct
{
    char cameo[13]; /* icon stem, e.g. "NUKE", "E1"; "" for an empty slot   */
    int producing;  /* Buildables[].Factory != -1                            */
    int completion; /* FactoryClass::Completion(), 0..107                    */
    int ready;      /* FactoryClass::Has_Completed()                         */
    int onhold;     /* factory && !factory->Is_Building()                    */
    int darken;     /* a factory of this type is already busy                */
} DB_Slot;

typedef struct
{
    DB_Slot items[DB_COLUMNS][DB_MAX_BUILDABLES];
    int count[DB_COLUMNS];    /* BuildableCount */
    int top[DB_COLUMNS];      /* TopIndex       */
    int repair_pressed;       /* Repair->IsPressed  */
    int repair_on;            /* Repair->IsOn -> RED caption */
    int sell_pressed;
    int sell_on;
    int map_disabled;         /* Zoom->Disable() when !IsRadarActive */
    int radar_active;
    int power_total;          /* drawn as the vertical power bar */
    int power_drain;
} DB_State;

void db_state_clear(DB_State *st);
int db_state_add(DB_State *st, int column, const char *cameo);

/* Draws the whole 240..319 sidebar column into `s`. `s` must be 320x200. */
void db_draw_sidebar(DB_Surface *s, const DB_Pack *p, const DB_State *st);

/* One conversion at the end. `rgba` must hold w*h*4 bytes. Index 0 -> opaque black
 * (the DOS screen has no alpha); pass want_alpha to get index 0 as transparent. */
void db_surface_to_rgba(const DB_Surface *s, const unsigned char *pal8,
                        unsigned char *rgba, int want_alpha);

#endif /* DOSBAR_H */
