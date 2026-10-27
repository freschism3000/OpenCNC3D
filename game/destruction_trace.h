/* Read-only counters for the opt-in frame trace. No output or allocation here.
   Includes only after all building/model/effect types exist. Fragment run counts
   are actual submitted glDrawArrays calls; vertices and bytes include late-culled
   commands, whose data was uploaded before visibility was tested. */
static int destruction_trace_pass_kind() {
    if(g_m3CasterDepth) return 1;
    if(g_m3NormalPass) return 2;
    if(g_meshBakePass) return 3;
    return 0;
}
static void destruction_trace_frame_begin() {
    if(perf_trace_frame_active()) memset(g_destructTrace,0,sizeof g_destructTrace);
}
static void destruction_trace_gather() {
    if(!perf_trace_frame_active()) return;
    Uint64 value[42] = {};
    for(const auto& item : g_ebldBld) {
        const EbldBld& E = item.second;
        if(E.stage==EB_HELD || E.stage==EB_DAMAGED) value[0]++;
        if(E.stage==EB_WRECKED) value[1]++;
        for(int which=0;which<2;which++) {
            const auto& bodies = which ? E.wreck : E.cut;
            for(const auto& B : bodies) {
                if(!B.live) continue;
                value[2]++;
                if(B.asleep) value[4]++;
                if(B.pinned) value[5]++;
                if(!B.asleep && !B.pinned && !(which && E.sinking)) value[3]++;
            }
        }
    }
    value[6]=(Uint64)g_efxNPart; value[7]=(Uint64)g_efxNChunk;
    for(int i=0;i<g_efxNPart;i++) if(g_efxPart[i].sys<10) value[8+g_efxPart[i].sys]++;
    for(int i=0;i<g_efxNChunk;i++) if(g_efxChunk[i].sys<10) value[8+g_efxChunk[i].sys]++;
    value[18]=g_ebldLights.size(); value[19]=(Uint64)g_fxNLight;
    for(size_t i=0;i<22;i++) {
        value[20+i]=g_destructTrace[i];
    }
    perf_trace_extra("ebld_damaged,ebld_wrecked,ebld_bodies_live,ebld_bodies_moving,ebld_bodies_asleep,ebld_bodies_pinned,particle_sprites,particle_chunks,particle_sys0,particle_sys1,particle_sys2,particle_sys3,particle_sys4,particle_sys5,particle_sys6,particle_sys7,particle_sys8,particle_sys9,building_lights,selected_lights,fragment_color_commands,fragment_color_runs,fragment_color_vertices,fragment_color_upload_bytes,fragment_shadow_commands,fragment_shadow_runs,fragment_shadow_vertices,fragment_shadow_upload_bytes,fragment_normal_commands,fragment_normal_runs,fragment_normal_vertices,fragment_normal_upload_bytes,fragment_bake_commands,fragment_bake_runs,fragment_bake_vertices,fragment_bake_upload_bytes,ebld_cut_builds,ebld_texture_loads,ebld_texture_bytes,ebld_cut_cpu_us,ebld_texture_cpu_us,ebld_step_cpu_us", value, 42);
}
