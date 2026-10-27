/* iOS-only presentation adapter. Compile this translation unit instead of the
 * game source on iOS. Including the authoritative source grants narrow access to
 * its existing static UI functions without editing or duplicating them. */
#ifndef OPENCNC3D_GAME_SOURCE
#error "Define OPENCNC3D_GAME_SOURCE as the current-main game/cnc_eyes.cpp path"
#endif
#include OPENCNC3D_GAME_SOURCE
#include "ios_game_bridge.h"

static int ios_placement_requests_sent;
static int ios_order_requests_sent,ios_stop_requests_sent;
static int ios_last_order_mode=-1;
static CNC_Handle_Input_t ios_input_forward;
static int ios_input_order_mode;
static float ios_field_x,ios_field_y,ios_field_width=-1,ios_field_height=-1;

/* Observe the exact command export call, rather than counting a UI attempt that
 * the existing picker may refuse. A scope restores the original pointer before
 * returning to the event loop. Engine callbacks still reach the same function. */
static void ios_bridge_counted_input(InputRequestEnum request,unsigned char flags,uint64 player,
                                     int x0,int y0,int x1,int y1)
{
    if(request==INPUT_REQUEST_COMMAND_AT_POSITION) {
        ++ios_order_requests_sent;ios_last_order_mode=ios_input_order_mode;
    }
    ios_input_forward(request,flags,player,x0,y0,x1,y1);
}

struct IOSBridgeCommandWitness {
    CNC_Handle_Input_t saved,previous_forward;
    int previous_mode;
    explicit IOSBridgeCommandWitness(int mode):saved(BrainInput),previous_forward(ios_input_forward),previous_mode(ios_input_order_mode) {
        if(saved){ios_input_forward=saved==ios_bridge_counted_input?previous_forward:saved;ios_input_order_mode=mode;BrainInput=ios_bridge_counted_input;}
    }
    ~IOSBridgeCommandWitness(){BrainInput=saved;ios_input_forward=previous_forward;ios_input_order_mode=previous_mode;}
};

static bool ios_bridge_live()
{
    return g_brainLoaded && g_sbReady && !g_pack.cell.empty();
}

static bool ios_bridge_modal()
{
    return g_optOpen || g_cxOpen || g_editOn || fxp_is_open() || g_gameOver.valid;
}

static void ios_bridge_size(int *w, int *h)
{
    *w = g_fbW; *h = g_fbH;
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    if (window) SDL_GL_GetDrawableSize(window, w, h);
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}

static void ios_bridge_field(float *x,float *y,float *width,float *height)
{
    int w,h;ios_bridge_size(&w,&h);
    if(ios_field_width<=0||ios_field_height<=0){*x=*y=0;*width=(float)w;*height=(float)h;return;}
    *x=std::max(0.0f,std::min((float)w,ios_field_x));
    *y=std::max(0.0f,std::min((float)h,ios_field_y));
    const float right=std::max(*x,std::min((float)w,ios_field_x+ios_field_width));
    const float bottom=std::max(*y,std::min((float)h,ios_field_y+ios_field_height));
    *width=right-*x;*height=bottom-*y;
}

extern "C" void cnc_ios_game_set_field_rect(float x,float y,float width,float height)
{
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0){
        ios_field_x=ios_field_y=0;ios_field_width=ios_field_height=-1;return;
    }
    ios_field_x=x;ios_field_y=y;ios_field_width=width;ios_field_height=height;
}

static bool ios_bridge_field_point(float x,float y,float inset=0)
{
    float fx,fy,fw,fh;ios_bridge_field(&fx,&fy,&fw,&fh);
    return std::isfinite(x)&&std::isfinite(y)&&x>=fx+inset&&y>=fy+inset&&
           x<fx+fw-inset&&y<fy+fh-inset;
}

static bool ios_bridge_field_silhouette(const ScreenPoly &s)
{
    return s.ok&&ios_bridge_field_point(s.c0,s.r0)&&ios_bridge_field_point(s.c1,s.r1);
}

static bool ios_bridge_point(float x, float y)
{
    return ios_bridge_field_point(x,y);
}

static void ios_bridge_name(char *out, size_t capacity, const char *name)
{
    if (capacity) std::snprintf(out, capacity, "%s", name ? name : "");
}

static bool ios_bridge_project(float x, float y, float *sx, float *sy)
{
    if (!sx || !sy || !std::isfinite(x) || !std::isfinite(y)) return false;
    int w, h; ios_bridge_size(&w, &h);
    world_to_screen(x, terrain_y(x, y), y, w, h, sx, sy);
    return std::isfinite(*sx) && std::isfinite(*sy);
}

static bool ios_bridge_attackable(const SimObject &o)
{
    int px,py;
    return !is_players(o)&&visible(o)&&cell_is_visible(o.tcx,o.tcy)&&BrainProbe&&
        engine_pixels_for_cell(o.tcx,o.tcy,&px,&py)&&BrainProbe(px,py)==5;
}

/* Finger-sized acquisition, confined to the visible field. A precise silhouette
 * hit keeps its original meaning; nearby acquisition still uses the same picker
 * and engine action tests, so it cannot reach through UI, fog or another object. */
static void ios_bridge_touch_target(float *x,float *y,bool focus_only)
{
    int w,h;ios_bridge_size(&w,&h);
    const PickInfo exact=pick_at(*x,*y,w,h);
    if(exact.index>=0&&exact.index<(int)g_objects.size()&&visible(g_objects[exact.index]))return;
    int logical_w=w,logical_h=h;
    SDL_Window *window=SDL_GL_GetCurrentWindow();
    if(window)SDL_GetWindowSize(window,&logical_w,&logical_h);
    const float scale=std::max((float)w/std::max(1,logical_w),(float)h/std::max(1,logical_h));
    const float radius=22.0f*scale;
    float best=radius*radius,best_x=*x,best_y=*y;
    int best_priority=3;
    for(size_t i=0;i<g_objects.size();++i){
        const SimObject &o=g_objects[i];
        if(o.id<0||o.limbo||o.str<=0||o.dying||!visible(o))continue;
        const bool ours=selectable_by_player(o);
        if(focus_only?!ios_bridge_attackable(o):(!ours&&!ios_bridge_attackable(o)))continue;
        const ScreenPoly silhouette=object_screen_poly(o,w,h,false);
        if(!ios_bridge_field_silhouette(silhouette))continue;
        float cx,cy;poly_centre(silhouette,&cx,&cy);
        const float distance=(*x-cx)*(*x-cx)+(*y-cy)*(*y-cy);
        if(distance>radius*radius)continue;
        const PickInfo candidate=pick_at(cx,cy,w,h);
        if(candidate.index!=(int)i)continue;
        const int priority=!focus_only&&g_selCount==0&&!ours?1:0;
        if(priority<best_priority||(priority==best_priority&&distance<best)){
            best_priority=priority;best=distance;best_x=cx;best_y=cy;
        }
    }
    *x=best_x;*y=best_y;
}

extern "C" int cnc_ios_game_project(float x, float y, float *sx, float *sy)
{
    return ios_bridge_live() && ios_bridge_project(x, y, sx, sy);
}

extern "C" int cnc_ios_game_pick(float x, float y, CNC_IOSPick *out)
{
    if (!out) return 0;
    std::memset(out, 0, sizeof *out);
    out->object_key = out->object_kind = -1;
    if (!ios_bridge_live() || !ios_bridge_point(x, y)) return 0;
    int w, h; ios_bridge_size(&w, &h);
    const PickInfo p = pick_at(x, y, w, h);
    out->on_map = p.onMap; out->cell_x = p.cellX; out->cell_y = p.cellY;
    out->world_x = p.wx; out->world_y = p.wz;
    if (p.index >= 0 && p.index < (int)g_objects.size()) {
        const SimObject &o = g_objects[p.index];
        out->object_key = sel_key(o); out->object_kind = (int)o.kind;
        out->own = is_players(o); out->selected = is_selected(o);
        ios_bridge_name(out->name, sizeof out->name, o.type);
    }
    return 1;
}

extern "C" int cnc_ios_game_snapshot(CNC_IOSGameSnapshot *out)
{
    if (!out) return 0;
    std::memset(out, 0, sizeof *out);
    ios_bridge_size(&out->drawable_width, &out->drawable_height);
    out->first_mobile_key = -1;
    out->live = ios_bridge_live();
    if (!out->live) return 0;
    sb_poll();
    out->modal = ios_bridge_modal();
    const int w = out->drawable_width, h = out->drawable_height;
    out->tactical_right = sb_tactical_right(w, h);
    ios_bridge_field(&out->field_x,&out->field_y,&out->field_width,&out->field_height);
    out->credits = g_sbState.credits;
    out->power_produced = g_sbState.powerProduced;
    out->power_drained = g_sbState.powerDrained;
    out->selected_count = g_selCount;
    out->tick = g_engineFrame;
    out->placement_requests_sent = ios_placement_requests_sent;
    out->order_requests_sent=ios_order_requests_sent;
    out->stop_requests_sent=ios_stop_requests_sent;
    out->last_order_mode=ios_last_order_mode;
    ios_bridge_name(out->selection_name, sizeof out->selection_name, g_selWhat);
    out->map_x = g_mapX; out->map_y = g_mapY;
    out->map_width = g_mapW; out->map_height = g_mapH;
    out->camera_x = g_camX; out->camera_y = g_camZ;
    const float left=out->field_x,right=left+out->field_width;
    const float top=out->field_y,bottom=top+out->field_height;
    const float vx[4] = {left,right,right,left};
    const float vy[4] = {top,top,bottom,bottom};
    for (int i = 0; i < 4; ++i) {
        float x = 0, y = 0;
        screen_to_world(vx[i], vy[i], w, h, &x, &y);
        out->view_x[i] = g_mapW ? (x - g_mapX) / g_mapW : 0;
        out->view_y[i] = g_mapH ? (y - g_mapY) / g_mapH : 0;
    }
    std::vector<int> selected;
    for (const SimObject &o : g_objects) {
        if (o.id < 0 || !is_players(o) || o.limbo || o.str <= 0) continue;
        if (o.kind == K_BUILDING) ++out->own_structure_count;
        if (o.group >= 0 && o.group < 10) ++out->group_counts[o.group];
        if (is_selected(o)) selected.push_back(sel_key(o));
        if (out->first_mobile_key < 0 && o.kind == K_INFANTRY && visible(o)) {
            float x, y;
            const ScreenPoly silhouette=object_screen_poly(o,w,h,false);
            if(!ios_bridge_field_silhouette(silhouette))continue;
            poly_centre(silhouette,&x,&y);
            const PickInfo picked=pick_at(x,y,w,h);
            if (ios_bridge_point(x,y)&&picked.index>=0&&sel_key(g_objects[picked.index])==sel_key(o)) {
                out->first_mobile_key = sel_key(o);
                out->first_mobile_x = x; out->first_mobile_y = y;
            }
        }
    }
    std::sort(selected.begin(), selected.end());
    uint64_t signature = UINT64_C(1469598103934665603);
    for (int key : selected) {
        signature ^= (uint32_t)key;
        signature *= UINT64_C(1099511628211);
    }
    out->selection_signature = selected.empty() ? 0 : signature;
    out->build_count = std::min((int)g_sbState.entry.size(), CNC_IOS_MAX_BUILDS);
    out->builds_truncated = (int)g_sbState.entry.size() > out->build_count;
    for (int i = 0; i < out->build_count; ++i) {
        const SbEntry &e = g_sbState.entry[i];
        CNC_IOSBuildEntry &b = out->builds[i];
        ios_bridge_name(b.name, sizeof b.name, e.name);
        b.build_type = e.btype; b.build_id = e.bid; b.object_type = e.type;
        b.column = e.column; b.cost = e.cost; b.power = e.power;
        b.build_ticks = e.buildTime; b.progress = e.progress;
        b.completed = e.completed; b.constructing = e.constructing;
        b.on_hold = e.onHold; b.busy = e.busy;
        b.is_structure = e.type == BUILDING_TYPE;
    }
    out->placing = sb_placing();
    /* The requested mobile navigation map is always accessible. Only its
     * availability changes: the existing plotter still applies explored-terrain,
     * object visibility and cloak rules. No engine radar/fog flag is changed. */
    out->radar_available = g_dbPack && g_mapW>0 && g_mapH>0;
    if (out->radar_available)
        h6_radar_plot(out->radar_rgba, CNC_IOS_RADAR_SIZE, CNC_IOS_RADAR_SIZE,
                      g_dbPack, 1);
    /* Hidden terrain and aspect bars must be opaque black in a UIKit image. */
    for (size_t i = 3; i < sizeof out->radar_rgba; i += 4) out->radar_rgba[i] = 255;
    return 1;
}

extern "C" int cnc_ios_game_select(float x, float y, int additive)
{
    if (!ios_bridge_live() || ios_bridge_modal() || sb_placing() || !ios_bridge_point(x,y)) return 0;
    int w,h; ios_bridge_size(&w,&h);
    ui_left_click(x,y,w,h,additive != 0);
    return g_selCount;
}

extern "C" int cnc_ios_game_select_box(float x0,float y0,float x1,float y1,int additive)
{
    if (!ios_bridge_live() || ios_bridge_modal() || sb_placing()) return 0;
    if (!std::isfinite(x0)||!std::isfinite(y0)||!std::isfinite(x1)||!std::isfinite(y1)) return 0;
    int w,h; ios_bridge_size(&w,&h);
    float fx,fy,fw,fh;ios_bridge_field(&fx,&fy,&fw,&fh);
    x0=std::max(fx,std::min(fx+fw,x0));x1=std::max(fx,std::min(fx+fw,x1));
    y0=std::max(fy,std::min(fy+fh,y0));y1=std::max(fy,std::min(fy+fh,y1));
    return ui_band_select(x0,y0,x1,y1,w,h,additive != 0);
}

extern "C" int cnc_ios_game_select_visible(int kind)
{
    if (!ios_bridge_live() || ios_bridge_modal() || sb_placing() || !BrainClearSel || !BrainSelect) return 0;
    int w,h;ios_bridge_size(&w,&h);
    BrainClearSel(0);
    for (const SimObject &o : g_objects) {
        if (!selectable_by_player(o) || !visible(o) || o.str <= 0 || o.limbo || o.dying) continue;
        const bool infantry = o.kind == K_INFANTRY;
        const bool vehicle = o.kind == K_UNIT || o.kind == K_AIRCRAFT || o.kind == K_VESSEL;
        if (!(infantry || vehicle) || (kind == 1 && !infantry) || (kind == 2 && !vehicle)) continue;
        if(!ios_bridge_field_silhouette(object_screen_poly(o,w,h,false)))continue;
        BrainSelect(0,dll_type_of(o.kind),o.id);
    }
    sync_selection_from_brain();
    return g_selCount;
}

extern "C" int cnc_ios_game_tap(float x,float y)
{
    if (!ios_bridge_live() || ios_bridge_modal() || !ios_bridge_point(x,y)) return 0;
    int w,h; ios_bridge_size(&w,&h);
    if (sb_placing()) {
        int cx,cy;
        if (!place_origin_at(x,y,w,h,&cx,&cy)) return 0;
        sb_set_hover(cx,cy);
        return 1;
    }
    ios_bridge_touch_target(&x,&y,false);
    IOSBridgeCommandWitness witness(CNC_IOS_ORDER_CONTEXT);
    ui_click_action(x,y,w,h,false,false,false);
    return 1;
}

extern "C" int cnc_ios_game_order(float x,float y,int mode)
{
    if (!ios_bridge_live() || ios_bridge_modal() || sb_placing() || !ios_bridge_point(x,y) || !g_selCount) return 0;
    int w,h; ios_bridge_size(&w,&h);
    if(mode==CNC_IOS_ORDER_FOCUS_FIRE) {
        ios_bridge_touch_target(&x,&y,true);
        const PickInfo picked=pick_at(x,y,w,h);
        if(picked.index<0 || picked.index>=(int)g_objects.size())return 0;
        const SimObject &target=g_objects[picked.index];
        if(!ios_bridge_attackable(target))return 0;
    }
    const int requests_before=ios_order_requests_sent;
    IOSBridgeCommandWitness witness(mode);
    amq_reset_selection("iOS order");
    if (mode == CNC_IOS_ORDER_ATTACK_MOVE) {
        am_set(true,"iOS attack move");
        ui_click_action(x,y,w,h,false,false,false);
    } else {
        ui_order_at(x,y,w,h,mode == CNC_IOS_ORDER_FORCE_FIRE,mode == CNC_IOS_ORDER_MOVE_ONLY);
    }
    return ios_order_requests_sent>requests_before?1:0;
}

extern "C" void cnc_ios_game_stop(void)
{
    if (ios_bridge_live() && !ios_bridge_modal() && BrainUnitReq && g_selCount>0) {
        ui_stop();++ios_stop_requests_sent;
    }
}

extern "C" void cnc_ios_game_group(int slot,int action)
{
    if (!ios_bridge_live() || ios_bridge_modal() || sb_placing()) return;
    int w,h; ios_bridge_size(&w,&h); ui_group(slot,action,w,h);
}

extern "C" void cnc_ios_game_pan(float x0,float y0,float x1,float y1)
{
    if (!ios_bridge_live() || ios_bridge_modal()) return;
    if (!std::isfinite(x0)||!std::isfinite(y0)||!std::isfinite(x1)||!std::isfinite(y1)) return;
    int w,h; ios_bridge_size(&w,&h);
    pan_drag(x0,y0,x1,y1,w,h); clamp_camera(w,h);
}

extern "C" void cnc_ios_game_zoom(float ratio,float x,float y)
{
    if (!ios_bridge_live() || ios_bridge_modal() || !std::isfinite(ratio) || ratio <= 0 || !ios_bridge_point(x,y)) return;
    int w,h; ios_bridge_size(&w,&h);
    float ax=0,ay=0,bx=0,by=0;
    const bool anchored=screen_to_world(x,y,w,h,&ax,&ay);
    ratio=std::max(.5f,std::min(2.0f,ratio));
    if (g_camMode == CAM_N64) set_dist(g_dist/ratio); else set_zoom(g_zoom*ratio);
    if (anchored && screen_to_world(x,y,w,h,&bx,&by)) { g_camX+=ax-bx; g_camZ+=ay-by; }
    clamp_camera(w,h);
}

extern "C" void cnc_ios_game_minimap(float x,float y)
{
    if (!ios_bridge_live() || ios_bridge_modal() || !std::isfinite(x) || !std::isfinite(y)) return;
    int w,h; ios_bridge_size(&w,&h);
    g_camX=g_mapX+std::max(0.0f,std::min(1.0f,x))*g_mapW;
    g_camZ=g_mapY+std::max(0.0f,std::min(1.0f,y))*g_mapH;
    clamp_camera(w,h);
}

extern "C" void cnc_ios_game_focus_base(void)
{
    if(!ios_bridge_live()||ios_bridge_modal()||sb_placing())return;
    int w,h;ios_bridge_size(&w,&h);
    ui_base_jump(w,h);
    for(const SimObject &o:g_objects){
        if(!is_selected(o)||!is_players(o)||o.limbo||
           (std::strcmp(o.type,"FACT")&&std::strcmp(o.type,"MCV")))continue;
        float sx,sy,fx,fy,fw,fh;
        if(!ios_bridge_project(o.ex,o.ez,&sx,&sy))break;
        ios_bridge_field(&fx,&fy,&fw,&fh);
        pan_drag(sx,sy,fx+fw*.5f,fy+fh*.5f,w,h);
        clamp_camera(w,h);
        break;
    }
}

extern "C" int cnc_ios_game_build_action(int type,int id,int action)
{
    if (!ios_bridge_live() || ios_bridge_modal()) return 0;
    sb_poll();
    int idx=-1;
    for (size_t i=0;i<g_sbState.entry.size();++i)
        if (g_sbState.entry[i].btype==type && g_sbState.entry[i].bid==id) {idx=(int)i;break;}
    if (idx<0) return 0;
    const SbEntry e=g_sbState.entry[idx];
    switch(action) {
        case CNC_IOS_BUILD_START:
            if (e.completed || (e.busy && !e.onHold)) return 0;
            sb_start(idx); break;
        case CNC_IOS_BUILD_HOLD: sb_hold(idx); break;
        case CNC_IOS_BUILD_CANCEL: sb_cancel(idx); break;
        case CNC_IOS_BUILD_PLACE_READY:
            if (!e.completed || e.type!=BUILDING_TYPE) return 0;
            sb_start_placement(idx); break;
        default: return 0;
    }
    sb_poll();
    return 1;
}

static int ios_bridge_placement(CNC_IOSPlacement *out)
{
    if (!out) return 0;
    std::memset(out,0,sizeof *out);
    out->cell_x=out->cell_y=-1; out->build_type=out->build_id=-1;
    if (!ios_bridge_live() || !sb_placing()) return 0;
    out->active=1; out->cell_x=g_sbHoverX; out->cell_y=g_sbHoverY;
    out->build_type=g_sbPlaceBType; out->build_id=g_sbPlaceBId;
    if (g_sbPlaceIdx<0 || g_sbPlaceIdx>=(int)g_sbState.entry.size()) {
        ios_bridge_name(out->reason,sizeof out->reason,"Producer unavailable; keep or cancel Ready");
        return 1;
    }
    const SbEntry &e=g_sbState.entry[g_sbPlaceIdx];
    ios_bridge_name(out->name,sizeof out->name,e.name);
    sb_place_dims(&out->width_cells,&out->height_cells);
    if(out->cell_x>=0 && out->cell_y>=0) {
        ios_bridge_project(out->cell_x+out->width_cells*.5f,
                           out->cell_y+out->height_cells*.5f,
                           &out->footprint_center_x,&out->footprint_center_y);
        ios_bridge_project(out->cell_x+(place_modern()?out->width_cells*.5f:.5f),
                           out->cell_y+(place_modern()?out->height_cells*.5f:.5f),
                           &out->screen_x,&out->screen_y);
    }
    out->legal=sb_legal_origin(out->cell_x,out->cell_y);
    const char *reason=out->legal?"Tap again to build here":sb_place_blocker(out->cell_x,out->cell_y);
    if (!reason) reason=out->cell_x<0?"Tap a location":!sb_footprint_on_map(out->cell_x,out->cell_y)?"Outside playable map":"Outside build range or occupied";
    ios_bridge_name(out->reason,sizeof out->reason,reason);
    if (out->cell_x<0 || out->cell_y<0) return 1;
    const int origin=out->cell_y*SB_STRIDE+out->cell_x;
    out->cell_count=std::min((int)e.occupy.size(),CNC_IOS_MAX_PLACE_CELLS);
    out->cells_truncated=(int)e.occupy.size()>out->cell_count;
    const float dx[4]={0,1,1,0},dy[4]={0,0,1,1};
    for(int i=0;i<out->cell_count;++i) {
        const int cell=origin+e.occupy[i];
        CNC_IOSPlacementCell &c=out->cells[i];
        c.cell_x=cell%SB_STRIDE;c.cell_y=cell/SB_STRIDE;
        const int gi=sb_grid_index(c.cell_x,c.cell_y);
        c.legal=gi>=0 && gi<(int)g_sbPlaceClear.size() && g_sbPlaceClear[gi] && sb_on_play_map(c.cell_x,c.cell_y);
        for(int k=0;k<4;++k) ios_bridge_project(c.cell_x+dx[k],c.cell_y+dy[k],&c.screen_x[k],&c.screen_y[k]);
    }
    return 1;
}

static bool ios_bridge_foundation_in_field(int x,int y,float inset)
{
    if(g_sbPlaceIdx<0||g_sbPlaceIdx>=(int)g_sbState.entry.size())return false;
    const SbEntry &entry=g_sbState.entry[g_sbPlaceIdx];
    if(entry.occupy.empty()||entry.occupy.size()>CNC_IOS_MAX_PLACE_CELLS)return false;
    const int origin=y*SB_STRIDE+x;
    const float dx[4]={0,1,1,0},dy[4]={0,0,1,1};
    for(short offset:entry.occupy){
        const int cell=origin+offset,cx=cell%SB_STRIDE,cy=cell/SB_STRIDE;
        for(int k=0;k<4;++k){
            float sx,sy;
            if(!ios_bridge_project(cx+dx[k],cy+dy[k],&sx,&sy)||!ios_bridge_field_point(sx,sy,inset))return false;
        }
    }
    return true;
}

extern "C" int cnc_ios_game_placement_query(float x,float y,CNC_IOSPlacement *out)
{
    if (!ios_bridge_live() || ios_bridge_modal() || !ios_bridge_point(x,y)) {
        if(out)std::memset(out,0,sizeof *out);return 0;
    }
    sb_poll();
    if (!sb_placing()) return ios_bridge_placement(out);
    int w,h,cx=-1,cy=-1;ios_bridge_size(&w,&h);
    if(place_origin_at(x,y,w,h,&cx,&cy))sb_set_hover(cx,cy);
    else sb_set_hover(-1,-1);
    return ios_bridge_placement(out);
}

extern "C" int cnc_ios_game_placement_current(CNC_IOSPlacement *out)
{
    if(ios_bridge_live())sb_poll();
    return ios_bridge_placement(out);
}

extern "C" int cnc_ios_game_find_placement(CNC_IOSPlacement *out)
{
    if(!ios_bridge_live() || ios_bridge_modal()){if(out)std::memset(out,0,sizeof *out);return 0;}
    sb_poll();if(!sb_placing())return ios_bridge_placement(out);
    int w,h;ios_bridge_size(&w,&h);
    float fx,fy,fw,fh;ios_bridge_field(&fx,&fy,&fw,&fh);
    float best=1e30f;int bestx=-1,besty=-1,bw=1,bh=1;sb_place_dims(&bw,&bh);
    for(int y=g_sb.mapY;y<g_sb.mapY+g_sb.mapH;++y)
        for(int x=g_sb.mapX;x<g_sb.mapX+g_sb.mapW;++x) {
            if(!sb_legal_origin(x,y)||!ios_bridge_foundation_in_field(x,y,12))continue;
            float sx,sy;
            if(!ios_bridge_project(x+bw*.5f,y+bh*.5f,&sx,&sy))continue;
            const float d=(sx-(fx+fw*.5f))*(sx-(fx+fw*.5f))+(sy-(fy+fh*.5f))*(sy-(fy+fh*.5f));
            if(d<best){best=d;bestx=x;besty=y;}
        }
    if(bestx<0){ios_bridge_placement(out);return 0;}
    const int oldx=g_sbHoverX,oldy=g_sbHoverY;
    sb_set_hover(bestx,besty);
    const int result=ios_bridge_placement(out);
    sb_set_hover(oldx,oldy); /* Diagnostic search never moves the player's ghost. */
    return result;
}

extern "C" int cnc_ios_game_placement_confirm(int x,int y)
{
    if(!ios_bridge_live() || ios_bridge_modal())return 0;
    sb_poll();
    if(!sb_placing() || x!=g_sbHoverX || y!=g_sbHoverY || !sb_legal_origin(x,y)||!ios_bridge_foundation_in_field(x,y,0))return 0;
    const bool sent=sb_place_at(x,y);
    if(sent)++ios_placement_requests_sent;
    return sent?1:0;
}

extern "C" void cnc_ios_game_placement_cancel(void)
{
    if(ios_bridge_live() && !ios_bridge_modal()) {sb_cancel_placement();sb_poll();}
}

extern "C" void cnc_ios_game_set_native_hud(int native_hud)
{
    g_sbOn=!native_hud;
    g_cursorOn=!native_hud;
    if(native_hud){g_ucShownNow=false;g_confinePointer=0;}
}

#include "ios_mobile_fixture.h"

static int ios_bridge_test_check(bool condition,const char *name)
{
    std::printf("IOS|mobile-selftest|%s|%s|tick=%d\n",condition?"PASS":"FAIL",name,g_engineFrame);
    std::fflush(stdout);
    return condition?1:0;
}

extern "C" int cnc_ios_game_selftest(void)
{
    CNC_IOSGameSnapshot before={},after={};
    if(!ios_bridge_test_check(cnc_ios_game_snapshot(&before)&&before.live&&!before.modal,
                              "native fixture available"))return 0;
    const int original_failures=g_scriptFails;
    if(!ios_bridge_test_check(cnc_ios_game_select_visible(1)>0,"select real visible infantry"))return 0;
    cnc_ios_game_snapshot(&before);
    cnc_ios_game_minimap(.5f,.5f);
    cnc_ios_game_snapshot(&after);
    if(!ios_bridge_test_check(before.selection_signature==after.selection_signature,
                              "navigation preserves exact recipients"))return 0;
    cnc_ios_game_minimap((before.camera_x-before.map_x)/before.map_width,
                         (before.camera_y-before.map_y)/before.map_height);

    /* Find a real engine-approved move near the selection. The witness below is
     * NavCom or actual travel after ticks, never the adapter's return code. */
    int base_x=-1,base_y=-1;
    for(const SimObject &o:g_objects)if(is_selected(o)&&o.kind==K_INFANTRY){base_x=o.tcx;base_y=o.tcy;break;}
    bool ordered=false;
    std::map<int,std::pair<float,float> > start;
    for(const SimObject &o:g_objects)if(is_selected(o))start[sel_key(o)]={o.ex,o.ez};
    for(int radius=3;radius<=7&&!ordered;++radius)
        for(int dy=-radius;dy<=radius&&!ordered;++dy)
            for(int dx=-radius;dx<=radius&&!ordered;++dx){
                if(std::abs(dx)!=radius&&std::abs(dy)!=radius)continue;
                const int cx=base_x+dx,cy=base_y+dy;
                int px,py;float sx,sy;
                if(cx<g_mapX||cy<g_mapY||cx>=g_mapX+g_mapW||cy>=g_mapY+g_mapH)continue;
                if(!engine_pixels_for_cell(cx,cy,&px,&py)||!BrainProbe||BrainProbe(px,py)!=1)continue;
                if(!ios_bridge_project(cx+.5f,cy+.5f,&sx,&sy)||!ios_bridge_point(sx,sy))continue;
                ordered=cnc_ios_game_order(sx,sy,CNC_IOS_ORDER_MOVE_ONLY)!=0;
            }
    if(!ios_bridge_test_check(ordered,"send move through native command API"))return 0;
    script_tick(8);
    bool moving=false;
    for(const SimObject &o:g_objects){
        const auto it=start.find(sel_key(o));if(it==start.end())continue;
        if(o.nav>=0||std::fabs(o.ex-it->second.first)+std::fabs(o.ez-it->second.second)>.01f)moving=true;
    }
    if(!ios_bridge_test_check(moving,"engine NavCom or actual position confirms move"))return 0;
    cnc_ios_game_stop();script_tick(3);
    bool stopped=true;
    for(const SimObject &o:g_objects)if(is_selected(o)&&o.nav>=0)stopped=false;
    if(!ios_bridge_test_check(stopped,"engine clears selected movement on Stop"))return 0;

    cnc_ios_game_snapshot(&before);
    int type=-1,id=-1,cost=0;
    for(int i=0;i<before.build_count;++i)if(!std::strcmp(before.builds[i].name,"NUKE")){
        type=before.builds[i].build_type;id=before.builds[i].build_id;cost=before.builds[i].cost;break;
    }
    if(!ios_bridge_test_check(type>=0&&cost>0&&cnc_ios_game_build_action(type,id,CNC_IOS_BUILD_START),
                              "start actual Power Plant production"))return 0;
    bool ready=false,debit=false;
    for(int step=0;step<120&&!ready;++step){
        script_tick(10);cnc_ios_game_snapshot(&after);
        debit=debit||after.credits<before.credits;
        for(int i=0;i<after.build_count;++i)if(after.builds[i].build_type==type&&after.builds[i].build_id==id)
            ready=after.builds[i].completed!=0;
    }
    if(!ios_bridge_test_check(ready&&debit,"native credits debited and completed Ready reported"))return 0;
    if(!ios_bridge_test_check(cnc_ios_game_build_action(type,id,CNC_IOS_BUILD_PLACE_READY),"enter real placement"))return 0;
    script_tick(1);
    CNC_IOSPlacement legal={};
    if(!ios_bridge_test_check(cnc_ios_game_find_placement(&legal)&&legal.legal,"find engine-legal visible foundation"))return 0;
    CNC_IOSPlacement preview={};
    if(!ios_bridge_test_check(cnc_ios_game_placement_query(legal.screen_x,legal.screen_y,&preview)&&preview.legal,
                              "position preview without committing"))return 0;
    cnc_ios_game_snapshot(&after);
    if(!ios_bridge_test_check(after.own_structure_count==before.own_structure_count&&
                              after.placement_requests_sent==before.placement_requests_sent,
                              "first location press sends no placement"))return 0;
    cnc_ios_game_placement_cancel();script_tick(1);cnc_ios_game_snapshot(&after);
    ready=false;
    for(int i=0;i<after.build_count;++i)if(after.builds[i].build_type==type&&after.builds[i].build_id==id)
        ready=after.builds[i].completed!=0;
    if(!ios_bridge_test_check(!after.placing&&ready,"cancel preview preserves completed Ready"))return 0;
    cnc_ios_game_build_action(type,id,CNC_IOS_BUILD_PLACE_READY);script_tick(1);
    if(!ios_bridge_test_check(cnc_ios_game_find_placement(&legal)&&
        cnc_ios_game_placement_query(legal.screen_x,legal.screen_y,&preview)&&preview.legal&&
        cnc_ios_game_placement_confirm(preview.cell_x,preview.cell_y),"explicit confirm sends legal placement"))return 0;
    script_tick(10);cnc_ios_game_snapshot(&after);
    const bool built=after.own_structure_count==before.own_structure_count+1&&
        after.placement_requests_sent==before.placement_requests_sent+1&&!after.placing&&
        g_scriptFails==original_failures;
    ios_bridge_test_check(built,"engine owns one new structure after confirmation");
    std::printf("IOS|mobile-selftest|%s|structures=%d->%d|credits=%d->%d|placement-requests=%d\n",
        built?"PASS":"FAIL",before.own_structure_count,after.own_structure_count,
        before.credits,after.credits,after.placement_requests_sent);
    std::fflush(stdout);
    return built?1:0;
}
