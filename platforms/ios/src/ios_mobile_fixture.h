/* Included AFTER cnc_eyes.cpp in the iOS engine-wrapper translation unit.
 * This explicitly requested launch fixture uses the existing SCG01EA mission,
 * native selection/context command, and simulation clock. It never grants money,
 * spawns objects, changes terrain, enables cheats, or starts a free building.
 */
#ifndef OPENCNC3D_IOS_MOBILE_FIXTURE_H
#define OPENCNC3D_IOS_MOBILE_FIXTURE_H

static int cnc_ios_fixture_fail(const char* reason)
{
    fprintf(stderr, "IOS|mobile-fixture|FAIL|%s|frame=%d\n", reason, g_engineFrame);
    return 0;
}

extern "C" int cnc_ios_prepare_mobile_fixture(void)
{
    if (g_raModule || strcmp(g_bootScen, "SCG01EA") != 0)
        return cnc_ios_fixture_fail("requires the original SCG01EA mission");
    /* The native brain reports -1 until its first state dump; desktop boot can
       already have observed frame 0. Both are the untouched initial world. */
    if (g_engineFrame > 0)
        return cnc_ios_fixture_fail("must run immediately after mission boot");
    if (!BrainSelect || !BrainClearSel || !BrainInput || !BrainProbe)
        return cnc_ios_fixture_fail("native selection and context-command exports unavailable");

    SDL_Window* window = SDL_GL_GetCurrentWindow();
    if (!window) return cnc_ios_fixture_fail("no current SDL graphics window");
    int width = 0, height = 0;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width < 1 || height < 1)
        return cnc_ios_fixture_fail("invalid drawable dimensions");
    /* The regular draw_frame records these too. Set them before scripted ticks
     * so audio and camera helpers see the actual device drawable immediately. */
    g_fbW = width;
    g_fbH = height;

    const int failures_before = g_scriptFails;
    script_tick(5);
    const int mcv_index = find_object("MCV:@me", 0);
    if (mcv_index < 0 || g_objects[mcv_index].limbo || !is_players(g_objects[mcv_index]))
        return cnc_ios_fixture_fail("the player's initial MCV is unavailable");
    const SimObject mcv = g_objects[mcv_index];
    int engine_x = 0, engine_y = 0;
    if (!engine_pixels_for_cell(mcv.tcx, mcv.tcy, &engine_x, &engine_y))
        return cnc_ios_fixture_fail("the native engine cannot resolve the MCV's cell");

    /* The same exports and SPECIAL_KEYS reset as ui_left_click/ui_order_at. Using
     * the object's native cell makes fixture preparation independent of device
     * pixels. It is setup, not evidence that a physical touch selected the MCV. */
    BrainClearSel(0);
    if (!BrainSelect(0, dll_type_of(mcv.kind), mcv.id))
        return cnc_ios_fixture_fail("native engine refused MCV selection");
    sync_selection_from_brain();
    const int action = BrainProbe(engine_x, engine_y);
    if (action != CUR_ACTION_SELF)
        return cnc_ios_fixture_fail("native engine did not offer MCV deployment");
    BrainInput(INPUT_REQUEST_SPECIAL_KEYS, 0, 0, 0, 0, 0, 0);
    BrainInput(INPUT_REQUEST_COMMAND_AT_POSITION, 0, 0, engine_x, engine_y, 0, 0);
    BrainInput(INPUT_REQUEST_SPECIAL_KEYS, 0, 0, 0, 0, 0, 0);
    script_tick(220);
    sb_poll();

    const int yard_index = find_object("FACT:@me", 0);
    const int plant_index = sb_find("NUKE");
    if (g_scriptFails != failures_before || g_gameOver.valid)
        return cnc_ios_fixture_fail("simulation refused a tick or the mission ended");
    if (yard_index < 0 || g_objects[yard_index].limbo || find_object("MCV:@me", 0) >= 0)
        return cnc_ios_fixture_fail("MCV did not become a live Construction Yard");
    if (!g_sbState.valid || plant_index < 0)
        return cnc_ios_fixture_fail("the real Power Plant build entry is unavailable");
    const SbEntry& plant = g_sbState.entry[plant_index];
    if (g_sbState.credits != 2000 || plant.cost != 300 || plant.completed
        || plant.constructing || plant.onHold || plant.busy || sb_placing())
        return cnc_ios_fixture_fail("unexpected native construction baseline");

    BrainClearSel(0);
    sync_selection_from_brain();
    /* Frame the real base and the known legal first Power Plant area. This does
     * not alter placement legality, reveal fog or select a build location. */
    g_camX = 55.0f;
    g_camZ = 51.0f;
    clamp_camera(width, height);
    const SimObject& yard = g_objects[yard_index];
    printf("IOS|mobile-fixture|PASS|scen=SCG01EA|frame=%d|yard=%d,%d|credits=%d"
           "|plant-cost=%d|plant-ready=%d|plant-building=%d|placing=%d|cheats=unchanged\n",
           g_engineFrame, yard.cx, yard.cy, g_sbState.credits, plant.cost,
           plant.completed ? 1 : 0, plant.constructing ? 1 : 0, sb_placing() ? 1 : 0);
    fflush(stdout);
    return 1;
}

#endif
