# CMakeLists.txt — modern (CMake/MSVC v143) build of Military Forces Q3 (MFQ3) 1.09
#
# Reconstructed from the original Visual Studio 2005 (.vcproj) files. This is a
# MONOLITHIC, standalone, x86/Win32 build: the id Tech 3 engine plus the mod's
# game/cgame/ui/vehiclelib are compiled as static libs and linked into a single
# MilitaryForces.exe (no QVM, no game DLLs). Build x86 ONLY — there is MSVC
# inline __asm in the engine that only assembles for 32-bit targets.
#
#   cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.20)
project(MilitaryForces C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "MFQ3 must be configured for x86/Win32 (inline __asm). Re-run with: -A Win32")
endif()

# --- shared compile settings -------------------------------------------------
# Quiet the sea of VS2005-era deprecation noise so real errors are visible.
add_compile_definitions(
    WIN32
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
    _WINSOCK_DEPRECATED_NO_WARNINGS
    _USE_MATH_DEFINES
)
# Note: BUILD_FREETYPE deliberately NOT defined — the ft2/ sources are absent
# from the repo and tr_font.c guards all FreeType use behind that macro.

if(MSVC)
    add_compile_options(/MP)                 # parallel compile
    add_compile_options(/wd4996 /wd4244 /wd4305 /wd4018 /wd4090 /wd4101 /wd4146)
    # Old code is not warning-clean; do NOT treat warnings as errors.
endif()

# The ENTIRE original solution set CompileAs="2" — every translation unit,
# including all .c files, jpeg-6 and the renderer, was built as C++ (because the
# mod's .c files include C++/STL headers from the in-progress GameEntity and
# vehiclelib migration). Force the C++ compiler on every .c file by setting the
# source LANGUAGE property (the VS generator then emits CompileAsCpp, which the
# bare /TP option could not override).
file(GLOB_RECURSE MFQ3_C_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/jpeg-6/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/win32/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/qcommon/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/game/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cgame/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.c
)
set_source_files_properties(${MFQ3_C_SOURCES} PROPERTIES LANGUAGE CXX)

set(CODE ${CMAKE_CURRENT_SOURCE_DIR})

# Cross-module includes: the mod files reach into sibling dirs liberally.
include_directories(
    ${CODE}
    ${CODE}/game
    ${CODE}/cgame
    ${CODE}/ui
    ${CODE}/qcommon
    ${CODE}/client
    ${CODE}/server
    ${CODE}/renderer
    ${CODE}/win32
    ${CODE}/vehiclelib
    ${CODE}/jpeg-6
)

# =============================================================================
# jpeg-6  (static lib)
# =============================================================================
add_library(jpeg6 STATIC
    jpeg-6/jcapimin.c jpeg-6/jccoefct.c jpeg-6/jccolor.c  jpeg-6/jcdctmgr.c
    jpeg-6/jchuff.c   jpeg-6/jcinit.c   jpeg-6/jcmainct.c jpeg-6/jcmarker.c
    jpeg-6/jcmaster.c jpeg-6/jcomapi.c  jpeg-6/jcparam.c  jpeg-6/jcphuff.c
    jpeg-6/jcprepct.c jpeg-6/jcsample.c jpeg-6/jctrans.c  jpeg-6/jdapimin.c
    jpeg-6/jdapistd.c jpeg-6/jdatadst.c jpeg-6/jdatasrc.c jpeg-6/jdcoefct.c
    jpeg-6/jdcolor.c  jpeg-6/jddctmgr.c jpeg-6/jdhuff.c   jpeg-6/jdinput.c
    jpeg-6/jdmainct.c jpeg-6/jdmarker.c jpeg-6/jdmaster.c jpeg-6/jdpostct.c
    jpeg-6/jdsample.c jpeg-6/jdtrans.c  jpeg-6/jerror.c   jpeg-6/jfdctflt.c
    jpeg-6/jidctflt.c jpeg-6/jmemmgr.c  jpeg-6/jmemnobs.c jpeg-6/jutils.c
)

# =============================================================================
# renderer  (static lib) — note FreeType (ft2) intentionally excluded
# =============================================================================
add_library(renderer STATIC
    renderer/tr_animation.c renderer/tr_backend.c   renderer/tr_bsp.c
    renderer/tr_cmds.c      renderer/tr_curve.c     renderer/tr_flares.c
    renderer/tr_font.c      renderer/tr_image.c     renderer/tr_init.c
    renderer/tr_light.c     renderer/tr_main.c      renderer/tr_marks.c
    renderer/tr_mesh.c      renderer/tr_model.c     renderer/tr_noise.c
    renderer/tr_scene.c     renderer/tr_shade.c     renderer/tr_shade_calc.c
    renderer/tr_shader.c    renderer/tr_shadows.c   renderer/tr_sky.c
    renderer/tr_surface.c   renderer/tr_world.c
    win32/win_gamma.c       win32/win_glimp.c       win32/win_qgl.c
)

# =============================================================================
# vehiclelib  (static lib) — MFQ3 vehicle/weapon definition system (C++)
# =============================================================================
add_library(vehiclelib STATIC
    vehiclelib/bg_datamanager.cpp        vehiclelib/bg_md3utils.cpp
    vehiclelib/bg_vehicleextra.cpp       vehiclelib/bg_vehicleinfo.cpp
    vehiclelib/bg_vehicleinfo_boats.cpp  vehiclelib/bg_vehicleinfo_ground.cpp
    vehiclelib/bg_vehicleinfo_helos.cpp  vehiclelib/bg_vehicleinfo_infantry.cpp
    vehiclelib/bg_vehicleinfo_planes.cpp vehiclelib/bg_vehicleinfo_vehicle.cpp
    vehiclelib/bg_weaponinfo.cpp         vehiclelib/bg_weaponinfo_bombs.cpp
    vehiclelib/bg_weaponinfo_guns.cpp    vehiclelib/bg_weaponinfo_missiles.cpp
    vehiclelib/bg_weaponinfo_other.cpp
)
target_compile_definitions(vehiclelib PRIVATE _LIB)

# =============================================================================
# game  (static lib) — server-side game logic, mid C->C++ GameEntity migration
# =============================================================================
add_library(game STATIC
    game/bg_boatmove.c   game/bg_groundmove.c game/bg_helomove.c
    game/bg_loadouts.c   game/bg_lqmmove.c    game/bg_md3util.c
    game/bg_mfq3util.c   game/bg_misc.c       game/bg_planemove.c
    game/bg_pmove.c      game/bg_slidemove.c  game/bg_vehicledata.c
    game/bg_weapondata.c
    game/g_active.c      game/g_arenas.c      game/g_boat.cpp
    game/g_client.c      game/g_client.cpp    game/g_cmds.c
    game/g_combat.c      game/g_entity.cpp    game/g_groundinstallation.c
    game/g_groundvehicle.cpp                  game/g_helo.cpp
    game/g_items.c       game/g_level.cpp     game/g_lqm.cpp
    game/g_main.c        game/g_mem.c         game/g_mfq3ents.c
    game/g_mfq3util.c    game/g_misc.c        game/g_miscvehicle.c
    game/g_missile.c     game/g_missions.c    game/g_mover.c
    game/g_plane.cpp     game/g_scripts.c     game/g_session.c
    game/g_spawn.c       game/g_svcmds.c      game/g_target.c
    game/g_team.c        game/g_trigger.c     game/g_utils.c
    game/g_vehicle.cpp   game/g_waypoint.c    game/g_weapon.c
    game/g_world.cpp     game/game.cpp        game/mf_client.c
    game/mf_vehiclespawn.c
)
target_compile_definitions(game PRIVATE _LIB)

# =============================================================================
# cgame  (static lib) — client game / rendering of the world & HUD
# =============================================================================
add_library(cgame STATIC
    cgame/cg.cpp             cgame/cg_boat.c          cgame/cg_consolecmds.c
    cgame/cg_draw.c          cgame/cg_drawnewhud.c    cgame/cg_drawoldhud.c
    cgame/cg_drawtools.c     cgame/cg_effects.c       cgame/cg_ents.c
    cgame/cg_event.c         cgame/cg_groundvehicle.c cgame/cg_helo.c
    cgame/cg_info.c          cgame/cg_localents.c     cgame/cg_lqm.c
    cgame/cg_main.c          cgame/cg_marks.c         cgame/cg_miscvehicle.c
    cgame/cg_missioneditor.c cgame/cg_particles.c     cgame/cg_plane.c
    cgame/cg_players.c       cgame/cg_playerstate.c   cgame/cg_predict.c
    cgame/cg_scoreboard.c    cgame/cg_servercmds.c    cgame/cg_snapshot.c
    cgame/cg_testmodel.c     cgame/cg_util.c          cgame/cg_vehicle.c
    cgame/cg_vehicledraw.c   cgame/cg_view.c          cgame/cg_weapons.c
    cgame/ui_displaycontext_cg.cpp                    cgame/ui_utils_cg.cpp
)
target_compile_definitions(cgame PRIVATE _LIB)

# =============================================================================
# ui  (static lib) — the menu / front-end system
# =============================================================================
add_library(ui STATIC
    ui/ui.cpp                ui/ui_atoms.c            ui/ui_commanddef.cpp
    ui/ui_controls.cpp       ui/ui_displaycontext.cpp ui/ui_displaycontext_ui.cpp
    ui/ui_gameinfo.c         ui/ui_hashutils.cpp      ui/ui_itemcapture.cpp
    ui/ui_keywordhash.cpp    ui/ui_main.c             ui/ui_memtools.cpp
    ui/ui_precomp.cpp        ui/ui_util.c             ui/ui_utils.cpp
    ui/ui_utils_ui.cpp
)
target_compile_definitions(ui PRIVATE _LIB)

# =============================================================================
# MilitaryForces.exe  — the engine (client + server + qcommon + win32 + sound),
# linking all the static libs above. WinMain entry => Windows GUI subsystem.
# =============================================================================
add_executable(MilitaryForces WIN32
    client/cl_cgame.c    client/cl_cin.c      client/cl_console.c
    client/cl_input.c    client/cl_keys.c     client/cl_main.c
    client/cl_net_chan.c client/cl_parse.c    client/cl_scrn.c
    client/cl_ui.c
    qcommon/cm_load.c    qcommon/cm_patch.c   qcommon/cm_polylib.c
    qcommon/cm_test.c    qcommon/cm_trace.c   qcommon/cmd.c
    qcommon/common.c     qcommon/cvar.c       qcommon/files.c
    qcommon/huffman.c    qcommon/md4.c        qcommon/msg.c
    qcommon/net_chan.c   qcommon/unzip.c
    game/q_math.c        game/q_shared.c
    client/snd_adpcm.c   client/snd_dma.c     client/snd_mem.c
    client/snd_mix.c     client/snd_wavelet.c
    server/sv_bot.c      server/sv_ccmds.c    server/sv_client.c
    server/sv_client.cpp server/sv_entity.cpp server/sv_game.c
    server/sv_init.c     server/sv_main.c     server/sv_net_chan.c
    server/sv_server.cpp server/sv_snapshot.c server/sv_world.c
    win32/win_input.c    win32/win_main.c     win32/win_net.c
    win32/win_shared.c   win32/win_snd.c      win32/win_syscon.c
    win32/win_wndproc.c
    win32/winquake.rc
)
target_compile_definitions(MilitaryForces PRIVATE _WINDOWS)
target_link_libraries(MilitaryForces
    cgame game ui vehiclelib renderer jpeg6
    winmm wsock32 dxguid dsound dinput8 ole32 user32 gdi32 advapi32
)

set_target_properties(MilitaryForces PROPERTIES OUTPUT_NAME "MilitaryForces")
