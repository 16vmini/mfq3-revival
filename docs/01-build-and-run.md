# Building & Running Classic MFQ3

The original MFQ3 1.0.9 source is a **monolithic, standalone** id Tech 3 fork: the full engine
(renderer, client, server, qcommon, bundled jpeg-6) plus the mod's `game`/`cgame`/`ui`/`vehiclelib`
all compile into a **single `MilitaryForces.exe`** (no QVM, no game DLLs). It does **not** need
retail Quake 3 — `mfdata/` is its own standalone base directory.

## Toolchain
- **Visual Studio 2022 Build Tools** (MSVC v143) + the VS-bundled CMake. No full IDE needed.
- **x86 / Win32 only.** There is hand-written MSVC `__asm` in the engine (`snd_mix.c`,
  `q_math.c`, `common.c`, `tr_shade_calc.c`, `win_shared.c`) that only assembles for 32-bit.

## Build
CMake build authored at `Military Forces/source/code/CMakeLists.txt` (reconstructed from the
original VS2005 `.vcproj` files — uses their exact source lists, not globbing):
```
cd "Military Forces/source/code"
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```
Output: `build/Release/MilitaryForces.exe` (~1.26 MB, x86). For comparison the original 2006
binary is 1.22 MB — within ~40 KB, as expected from a newer compiler.

## The three fixes required (that's all it took)
1. **Compile everything as C++.** The original projects all set `CompileAs="2"` (`/TP`); the mod's
   `.c` files include C++/STL headers from the in-progress GameEntity & vehiclelib migration.
   In CMake this is done via a per-source `LANGUAGE CXX` property (a bare `/TP` gets overridden by
   the VS generator).
2. **Five C++11 literal fixes** in `game/g_cmds.c`: `"text"EC` (string glued to the `EC` macro) is
   read as a user-defined literal in modern C++ → add spaces: `"text" EC`.
3. **One archival-data guard** in `client/snd_dma.c`: a zero-byte `sound/feedback/hit.wav` in the
   shipped paks made `S_AddLoopingSound` fatally `Com_Error`. Changed to warn + skip so corrupt
   20-year-old assets can't kill the session.

## Run
The play folder `play/` mirrors the original layout: `MilitaryForces.exe` next to `mfdata/`
(the base game dir holding `pak0..2.pk3`, maps, models, menus). The 2006 binary is kept beside
ours as `MilitaryForces.2006.exe`.
```
cd play
.\MilitaryForces.exe +set r_fullscreen 0 +set r_mode 3   # windowed 640x480
.\MilitaryForces.exe                                      # fullscreen, native res
```
Confirmed working: boots the full menu, loads a live BSP map (Norway), runs the renderer on a
modern GPU (verified OpenGL 4.6 on Intel Graphics), reaches the Limbo team/vehicle-select, and
flies. Console log → `play/mfdata/qconsole.log`.

## New feature added during revival: aircraft-only pitch invert
Quake's view-pitch convention is built for an FPS (positive pitch = look *down*), so aircraft
always felt "backwards" to stick flyers — and the stock `m_pitch` invert was global (it also
flipped on-foot aim and ground vehicles). We added a scoped invert:
- New archived cvar **`m_invertAircraftPitch`** (default `0`). When on, pitch is negated **only**
  while piloting a `CAT_PLANE`/`CAT_HELO` vehicle (tanks, boats, on-foot unaffected).
- Implemented engine-side in `client/cl_input.c` (`CL_MouseMove`), `client/cl_main.c`, `client/client.h`.
- Usage: `\m_invertAircraftPitch 1` to enable.

Note: this lives **engine-side** because raw mouse deltas only exist in the engine; it reads
game-side concepts (`PM_VEHICLE`, `availableVehicles[].cat`, `CAT_PLANE`) — possible only because
MFQ3 is a monolith where the engine can see the game's data.

## Missing assets (non-fatal)
The alpha paks reference 4 vehicle models that aren't included (Comanche helo + `turret_aaa`/
`turret_samup`/`turret_aa` NPC turrets) — harmless warnings. There are also **no player/biped
models** in any pak (needed later for LQM infantry — see [03-infantry-lqm.md](03-infantry-lqm.md)).
