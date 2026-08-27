/* ==================================================================================
 * CNC3D: how the renderer turns a screen click into a unit order.
 *
 * Drop-in fragment for cnc_eyes.cpp. Not compiled on its own; it is the reference
 * for whoever wires the mouse to the brain. Everything here is verified by
 * ../input/inputtest.cpp and by the before/after PNGs next to this file.
 *
 * The engine wants pixels of ITS OWN tactical view, at 24 px per cell, with the
 * view's top-left corner at TacticalCoord (see the contract at the top of
 * inputtest.cpp). Our camera has nothing to do with that space, so the renderer
 * must NOT pass its own mouse pixels through. It picks a CELL with its existing
 * unproject/pick code, and then converts that cell into engine input pixels.
 * ================================================================================== */

typedef void (*CNC_Handle_Input_t)(InputRequestEnum, unsigned char, uint64, int, int, int, int);
typedef bool (*CNC_Select_Object_t)(uint64, int, int);
typedef bool (*CNC_Clear_Object_Selection_t)(uint64);
typedef void (*CNC_Handle_Unit_Request_t)(UnitRequestEnum, uint64);
typedef bool (*CNC3D_Cell_To_Pixel_t)(int, int, int*, int*);
typedef void (*CNC3D_Get_View_t)(int*);

static CNC_Handle_Input_t           BrainInput;
static CNC_Select_Object_t          BrainSelect;
static CNC_Clear_Object_Selection_t BrainClearSel;
static CNC_Handle_Unit_Request_t    BrainUnitReq;
static CNC3D_Cell_To_Pixel_t        BrainCellToPixel;
static CNC3D_Get_View_t             BrainGetView;

/* resolve alongside the other dlsym calls in boot_brain() */
static void bind_input(void* hnd)
{
    BrainInput       = (CNC_Handle_Input_t)dlsym(hnd, "CNC_Handle_Input");
    BrainSelect      = (CNC_Select_Object_t)dlsym(hnd, "CNC_Select_Object");
    BrainClearSel    = (CNC_Clear_Object_Selection_t)dlsym(hnd, "CNC_Clear_Object_Selection");
    BrainUnitReq     = (CNC_Handle_Unit_Request_t)dlsym(hnd, "CNC_Handle_Unit_Request");
    BrainCellToPixel = (CNC3D_Cell_To_Pixel_t)dlsym(hnd, "CNC3D_Cell_To_Pixel");
    BrainGetView     = (CNC3D_Get_View_t)dlsym(hnd, "CNC3D_Get_View");
}

/* Cell -> engine input pixels. CNC3D_Cell_To_Pixel does this inside the brain and
   round-trips its own answer through Pixel_To_Coord, so it cannot lie. The fallback
   is the same arithmetic done here, for a brain built without the instrumentation. */
static bool engine_pixels_for_cell(int cell_x, int cell_y, int* px, int* py)
{
    if (BrainCellToPixel)
        return BrainCellToPixel(cell_x, cell_y, px, py);

    int v[11];
    if (!BrainGetView) return false;
    BrainGetView(v);                                   /* TacPixelX, TacPixelY, ..., TacCoordX, TacCoordY */
    const int lx = cell_x * 256 + 128 - v[4];
    const int ly = cell_y * 256 + 128 - v[5];
    *px = ((lx * 24) + 128) / 256 + v[0];
    *py = ((ly * 24) + 128) / 256 + v[1];
    return (unsigned)lx < (unsigned)v[2] && (unsigned)ly < (unsigned)v[3];
}

/* Left click on the world: select whatever is under the picked cell. */
static void ui_select_at_cell(int cell_x, int cell_y, bool additive)
{
    int px, py;
    if (!engine_pixels_for_cell(cell_x, cell_y, &px, &py)) return;
    BrainInput(INPUT_REQUEST_SPECIAL_KEYS, additive ? 4 : 0, 0, 0, 0, 0, 0);   /* 4 = SHIFT */
    BrainInput(INPUT_REQUEST_SELECT_AT_POSITION, 0, 0, px, py, 0, 0);
    BrainInput(INPUT_REQUEST_SPECIAL_KEYS, 0, 0, 0, 0, 0, 0);
}

/* Drag rectangle: both corners as cells. */
static void ui_band_select(int x1, int y1, int x2, int y2, bool additive)
{
    int p1x, p1y, p2x, p2y;
    if (!engine_pixels_for_cell(x1, y1, &p1x, &p1y)) return;
    if (!engine_pixels_for_cell(x2, y2, &p2x, &p2y)) return;
    BrainInput(additive ? INPUT_REQUEST_MOUSE_AREA_ADDITIVE : INPUT_REQUEST_MOUSE_AREA,
               0, 0, p1x, p1y, p2x, p2y);
}

/* Right click (or left click in "command" mode) on the world: order the selection.
   The engine decides move vs attack vs guard from what is under the cell plus the
   modifier flags; ctrl = force fire, alt = force move, ctrl+alt = guard area.
   The order lands on the next CNC_Advance_Instance, not inside this call. */
static void ui_command_at_cell(int cell_x, int cell_y, bool ctrl, bool alt)
{
    int px, py;
    if (!engine_pixels_for_cell(cell_x, cell_y, &px, &py)) return;
    unsigned char flags = (unsigned char)((ctrl ? 1 : 0) | (alt ? 2 : 0));
    BrainInput(INPUT_REQUEST_SPECIAL_KEYS, flags, 0, 0, 0, 0, 0);
    BrainInput(INPUT_REQUEST_COMMAND_AT_POSITION, 0, 0, px, py, 0, 0);
    BrainInput(INPUT_REQUEST_SPECIAL_KEYS, 0, 0, 0, 0, 0, 0);
}

/* Keyboard verbs that need no coordinates. */
static void ui_stop(void)  { BrainUnitReq(INPUT_UNIT_STOP, 0); }
static void ui_guard(void) { BrainUnitReq(INPUT_UNIT_GUARD_MODE, 0); }
