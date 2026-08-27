set(TIBDAWN_SRC
    aadata.cpp
    abstract.cpp
    adata.cpp
    aircraft.cpp
    anim.cpp
    audio.cpp
    base.cpp
    bbdata.cpp
    bdata.cpp
    building.cpp
    bullet.cpp
    cargo.cpp
    ccini.cpp
    cdata.cpp
    cell.cpp
    checkbox.cpp
    cheklist.cpp
    colrlist.cpp
    combat.cpp
    confdlg.cpp
    conquer.cpp
    const.cpp
    coord.cpp
    credits.cpp
    debug.cpp
    dial8.cpp
    dialog.cpp
    display.cpp
    door.cpp
    drive.cpp
    drop.cpp
    edit.cpp
    ending.cpp
    event.cpp
    expand.cpp
    facing.cpp
    factory.cpp
    findpath.cpp
    flasher.cpp
    fly.cpp
    foot.cpp
    fuse.cpp
    gamedlg.cpp
    gauge.cpp
    globals.cpp
    goptions.cpp
    gscreen.cpp
    hdata.cpp
    heap.cpp
    help.cpp
    house.cpp
    iconset.cpp
    idata.cpp
    infantry.cpp
    init.cpp
    intro.cpp
    iomap.cpp
    ioobj.cpp
    jshell.cpp
    layer.cpp
    lib.cpp
    list.cpp
    loaddlg.cpp
    logic.cpp
    map.cpp
    mapeddlg.cpp
    mapedit.cpp
    mapedplc.cpp
    mapedsel.cpp
    mapedtm.cpp
    mapsel.cpp
    menus.cpp
    miscasm.cpp
    mission.cpp
    monoc.cpp
    mouse.cpp
    mplayer.cpp
    msgbox.cpp
    msglist.cpp
    nulldlg.cpp
    object.cpp
    odata.cpp
    options.cpp
    overlay.cpp
    power.cpp
    profile.cpp
    queue.cpp
    radar.cpp
    radio.cpp
    reinf.cpp
    rules.cpp
    saveload.cpp
    scenario.cpp
    scenarioini.cpp
    score.cpp
    scroll.cpp
    sdata.cpp
    shapebtn.cpp
    sidebar.cpp
    slider.cpp
    smudge.cpp
    sounddlg.cpp
    special.cpp
    startup.cpp
    stats.cpp
    super.cpp
    tab.cpp
    tarcom.cpp
    target.cpp
    tdata.cpp
    team.cpp
    teamtype.cpp
    techno.cpp
    template.cpp
    terrain.cpp
    textbtn.cpp
    theme.cpp
    trigger.cpp
    turret.cpp
    txtlabel.cpp
    udata.cpp
    unit.cpp
    visudlg.cpp
    winstub.cpp
)

# CNC3D XL: this fork builds exactly one artifact, the TiberianDawnXL shared
# library (docs/design-xl-brain.md, BUILD). The VanillaTD executable, the icon
# and version-resource generation, the map-editor COM interface's non-Windows
# guard, and the remaster_mods resource copy from the vanilla file are all gone
# with the targets that used them. The networking sources stay in the tree (the
# fork copies tiberiandawn/ whole) but nothing here compiles them.

# CNC3D: dllinterface.{h,cpp} (the CNC_* API) is written for MSVC and uses __int64,
# __declspec and types declared inside anonymous unions. Clang/GCC accept all of that
# with the Microsoft-extension flags, so the API compiles unmodified off Windows.
if(NOT MSVC)
    set(CNC3D_MS_FLAGS -fms-extensions)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-fdeclspec CNC3D_HAS_FDECLSPEC)
    if(CNC3D_HAS_FDECLSPEC)
        list(APPEND CNC3D_MS_FLAGS -fdeclspec)
    endif()
endif()

# CNC3D: the map-editor interface is COM-based (SAFEARRAY/HRESULT/VT_UI1) and is
# Windows-only. The headless brain does not need it, so it is dropped off Windows.
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CNC3D_EDITOR_IFACE dllinterfaceeditor.cpp)
else()
    set(CNC3D_EDITOR_IFACE "")
endif()

file(GLOB_RECURSE TIBDAWN_HEADERS "*.h")

add_library(TiberianDawnXL SHARED
    dllinterface.cpp
    ${CNC3D_EDITOR_IFACE}
    sidebarglyphx.cpp
    ${TIBDAWN_SRC}
    ${TIBDAWN_HEADERS}
)
# No MEGAMAPS / EIGHTPLAYERS defines: their ifdefs are collapsed to the ON
# values in the source -- the tree IS the fork (docs/design-xl-brain.md).
target_compile_definitions(TiberianDawnXL PUBLIC $<$<CONFIG:Debug>:_DEBUG> ${REMASTER_DEFS} TIBERIAN_DAWN XLMAPS)
if(CNC3D_MS_FLAGS)
    target_compile_options(TiberianDawnXL PRIVATE ${CNC3D_MS_FLAGS})
    target_compile_options(commonr PRIVATE ${CNC3D_MS_FLAGS})
endif()
target_include_directories(TiberianDawnXL PUBLIC ${CMAKE_SOURCE_DIR} .)
target_link_libraries(TiberianDawnXL commonr ${REMASTER_LIBS} ${STATIC_LIBS})
set_target_properties(TiberianDawnXL PROPERTIES PREFIX "")
