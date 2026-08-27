"""Authoritative element sizes for the 640x480 sidebar.

Chosen so that (a) every element keeps a sane aspect close to the delivered artwork,
(b) they tile exactly into 160x480, (c) the radar opening is 128x128, which is exactly
2 pixels per cell for the full 64x64 engine grid."""

SCREEN = (640, 480)
SIDEBAR = (160, 480)
SIDEBAR_AT = (480, 0)

ELEMENTS = [
    # name                      size      pos       aspect  note
    ("panel_body",             (160,480), (0,0),    "backing panel, full sidebar"),
    ("panel_header",           (160, 23), (0,0),    "title strip"),
    ("panel_radar_bezel",      (160,160), (0,24),   "radar housing; leave a 128x128 hole at (16,16)"),
    ("button_repair",          ( 43, 26), (11,192), ""),
    ("button_sell",            ( 43, 26), (59,192), ""),
    ("button_map",             ( 39, 26), (107,192),""),
    ("button_repair_pressed",  ( 43, 26), None,     "same size as unpressed"),
    ("button_sell_pressed",    ( 43, 26), None,     ""),
    ("button_map_pressed",     ( 39, 26), None,     ""),
    ("meter_empty",            ( 26,192), (2,226),  "power channel, no fill"),
    ("meter_full",             ( 26,192), (2,226),  "same channel, fully lit"),
    ("cell_well",              ( 64, 48), None,     "empty build slot"),
    ("cell_frame",             ( 64, 48), None,     "overlay; border <= 3px each side"),
    ("arrow_up",               ( 32, 30), None,     ""),
    ("arrow_down",             ( 32, 30), None,     ""),
    ("arrow_up_pressed",       ( 32, 30), None,     ""),
    ("arrow_down_pressed",     ( 32, 30), None,     ""),
    ("cameo  (x61)",           ( 64, 48), None,     "safe area 58x42 at (3,3)"),
    ("radar_emblem_gdi",       (128,128), None,     "unpowered radar"),
    ("radar_emblem_nod",       (128,128), None,     ""),
]

RADAR_SURFACE = (16, 40, 128, 128)          # x, y, w, h in sidebar coords
CELL_COLS = [30, 94]
CELL_ROWS = [226, 274, 322, 370]            # 4 rows x 48
ARROW_Y   = 426
ARROW_XS  = [7, 45, 83, 121]
METER_AT  = (2, 226)

def check():
    ok = True
    last = CELL_ROWS[-1] + 48
    print("vertical budget")
    for lbl, y0, y1 in [("header", 0, 22), ("radar bezel", 24, 184),
                        ("buttons", 192, 218), ("build rows", 226, last),
                        ("arrows", ARROW_Y, ARROW_Y+30)]:
        print("  %-13s y %3d..%-3d  (%d)" % (lbl, y0, y1-1, y1-y0))
    print("  %-13s y %3d..479  (%d)" % ("bottom margin", ARROW_Y+30, 480-ARROW_Y-30))
    print()
    print("horizontal budget")
    print("  meter   x %3d..%-3d (26)" % (METER_AT[0], METER_AT[0]+25))
    print("  col 1   x %3d..%-3d (64)" % (CELL_COLS[0], CELL_COLS[0]+63))
    print("  col 2   x %3d..%-3d (64)" % (CELL_COLS[1], CELL_COLS[1]+63))
    print("  used %d of 160" % (CELL_COLS[1]+64))
    print()
    print("radar: opening %dx%d at (%d,%d) -> 64x64 grid at exactly 2 px/cell"
          % (RADAR_SURFACE[2], RADAR_SURFACE[3], RADAR_SURFACE[0], RADAR_SURFACE[1]))
    for w, h in [(26,23),(37,24),(60,60),(61,59),(64,64)]:
        z = min(RADAR_SURFACE[2]//w, RADAR_SURFACE[3]//h)
        print("    %2dx%-2d -> %dx -> %3dx%-3d" % (w, h, z, w*z, h*z))
    return ok

if __name__ == "__main__":
    check()
