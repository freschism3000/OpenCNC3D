#ifndef OPENCNC3D_IOS_GAME_BRIDGE_H
#define OPENCNC3D_IOS_GAME_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Call only on the SDL/game main thread. Coordinates are drawable pixels,
 * top-left origin, not UIKit points. Snapshots own all their copied data. */
#define CNC_IOS_MAX_BUILDS 64
#define CNC_IOS_MAX_PLACE_CELLS 64
#define CNC_IOS_RADAR_SIZE 128

typedef struct {
    char name[32];
    int build_type, build_id, object_type, column;
    int cost, power, build_ticks;
    float progress;
    int completed, constructing, on_hold, busy, is_structure;
} CNC_IOSBuildEntry;

typedef struct {
    int live, modal, drawable_width, drawable_height;
    float tactical_right;
    float field_x, field_y, field_width, field_height;
    int credits, power_produced, power_drained, selected_count;
    uint64_t selection_signature;
    int own_structure_count, tick, placement_requests_sent;
    int order_requests_sent, stop_requests_sent, last_order_mode;
    int first_mobile_key;
    float first_mobile_x, first_mobile_y;
    char selection_name[32];
    int group_counts[10];
    int map_x, map_y, map_width, map_height;
    float camera_x, camera_y;
    /* Normalized map coordinates of the four ground-plane view corners. */
    float view_x[4], view_y[4];
    int build_count, builds_truncated, placing;
    CNC_IOSBuildEntry builds[CNC_IOS_MAX_BUILDS];
    int radar_available;
    /* Same fog-safe plotter/palette/objects as the desktop radar. Black bars
     * preserve the map's aspect ratio; normalized_navigation uses map space. */
    unsigned char radar_rgba[CNC_IOS_RADAR_SIZE*CNC_IOS_RADAR_SIZE*4];
} CNC_IOSGameSnapshot;

typedef struct {
    int on_map, cell_x, cell_y, object_key, object_kind, own, selected;
    float world_x, world_y;
    char name[32];
} CNC_IOSPick;

typedef struct {
    int cell_x, cell_y, legal;
    float screen_x[4], screen_y[4];
} CNC_IOSPlacementCell;

typedef struct {
    int active, legal, build_type, build_id, cell_x, cell_y;
    /* Tap anchor round-trips through the game's existing place_origin_at:
     * footprint centre in modern mode, origin-cell centre in baseline mode. */
    float screen_x, screen_y;
    float footprint_center_x, footprint_center_y;
    int width_cells, height_cells, cell_count, cells_truncated;
    char name[32], reason[96];
    CNC_IOSPlacementCell cells[CNC_IOS_MAX_PLACE_CELLS];
} CNC_IOSPlacement;

enum {
    CNC_IOS_ORDER_CONTEXT = 0,
    CNC_IOS_ORDER_MOVE_ONLY = 1,
    CNC_IOS_ORDER_ATTACK_MOVE = 2,
    CNC_IOS_ORDER_FORCE_FIRE = 3,
    CNC_IOS_ORDER_FOCUS_FIRE = 4 /* visible, attackable non-own object only */
};
enum {
    CNC_IOS_BUILD_START = 0,
    CNC_IOS_BUILD_HOLD = 1,
    CNC_IOS_BUILD_CANCEL = 2,
    CNC_IOS_BUILD_PLACE_READY = 3
};

int cnc_ios_game_snapshot(CNC_IOSGameSnapshot *out);
int cnc_ios_game_pick(float x, float y, CNC_IOSPick *out);
int cnc_ios_game_project(float world_x, float world_y, float *screen_x, float *screen_y);
/* UIKit supplies its unobscured battlefield in drawable pixels. Nonpositive
 * dimensions restore the full drawable. No UI object is retained. */
void cnc_ios_game_set_field_rect(float x, float y, float width, float height);
void cnc_ios_game_focus_base(void); /* Select/frame own Yard or MCV; sends no order. */
int cnc_ios_game_select(float x, float y, int additive);
int cnc_ios_game_select_box(float x0, float y0, float x1, float y1, int additive);
int cnc_ios_game_select_visible(int kind); /* 0 all mobile, 1 infantry, 2 vehicles */
int cnc_ios_game_tap(float x, float y);
int cnc_ios_game_order(float x, float y, int order_mode);
void cnc_ios_game_stop(void);
void cnc_ios_game_group(int slot, int action); /* 0 recall, 1 add, 2 assign, 3 focus */
void cnc_ios_game_pan(float old_x, float old_y, float new_x, float new_y);
void cnc_ios_game_zoom(float ratio, float anchor_x, float anchor_y);
/* Map-normalized 0..1. Camera navigation never issues an order. */
void cnc_ios_game_minimap(float normalized_x, float normalized_y);
int cnc_ios_game_build_action(int build_type, int build_id, int action);
/* Query positions the preview; current reads it. Neither commits. Confirmation
 * returns whether a request was sent, not an asynchronous engine acceptance. */
int cnc_ios_game_placement_query(float x, float y, CNC_IOSPlacement *out);
int cnc_ios_game_placement_current(CNC_IOSPlacement *out);
int cnc_ios_game_find_placement(CNC_IOSPlacement *out);
int cnc_ios_game_placement_confirm(int absolute_cell_x, int absolute_cell_y);
void cnc_ios_game_placement_cancel(void); /* Leave placement, keep Ready. */
/* Hides the desktop HUD on iOS without disabling simulation/sidebar polling.
 * Its GL placement grid/ghost also hide; draw copied projected cells in UIKit. */
void cnc_ios_game_set_native_hud(int native_hud);
int cnc_ios_prepare_mobile_fixture(void);
/* Explicit diagnostic run only: advances the real simulation and builds a plant.
 * Returns 1 on verified engine witnesses, 0 on failure. No physical-touch claim. */
int cnc_ios_game_selftest(void);

#ifdef __cplusplus
}
#endif
#endif
