# Modernizing onto ioquake3

## Why ioquake3
[ioquake3](https://ioquake3.org) is a community-maintained continuation of the same id Tech 3
engine MFQ3 forked in 2006 — 20 years of platform/engine work we can adopt:
- **Cross-platform** via SDL2 (`code/sdl/` + `code/sys/`) → **Mac, Linux, and Steam Deck**
- **64-bit** builds (x64 asm + C fallbacks)
- Modern **GL2** renderer alongside classic GL1
- **WebAssembly** build support (`code/web/`)
- `botlib` (bot infrastructure — which MFQ3 had stripped)
- 20 years of netcode/security/bug fixes

The Steam Deck (Linux x86-64, first-class SDL/controller/gyro support) is the textbook target.

## Strategy: keep ioq3's engine, bring MFQ3's gameplay
We do **not** port MFQ3's whole engine. We keep *ioquake3's* engine (renderer, SDL, sys, netcode)
and bring over only MFQ3's **gameplay** modules (`game`/`cgame`/`ui`/`vehiclelib`), then **extend
ioq3's engine** just enough to carry MFQ3's vehicle state. "Converting" = bridging one seam: the
engine↔game contract.

This is a **3-way merge of two descendants of the same id Tech 3 ancestor** — files are
recognizably related (mergeable, not rewritable). Because ioq3 builds the **engine from source**,
we're *allowed* to grow the shared structs (a binary-only Quake would forbid it).

## Merge-surface scoping (measured)
The scariest layer (network serialization) turned out **small and clean**. MFQ3 didn't *grow* the
contract — it **remapped biped → vehicle**, reclaiming the (never-used) infantry fields:

**playerState netfields: MFQ3 49 vs ioq3 48.** MFQ3 removed `legsAnim, torsoAnim, legsTimer,
torsoTimer, bobCycle, movementDir, grapplePoint, weapon, weaponTime, weaponstate` and added
`throttle, fixed_throttle, vehicleAngles[3], turretAngle, gunAngle, ONOFF, tracktarget, objectives,
vehicleAnim, weaponNum, weaponIndex`. **entityState** did the same swap (~12 fields).

- **`usercmd_t` is byte-identical to stock Quake** (`serverTime, angles[3], buttons, weapon,
  forwardmove, rightmove, upmove`) — huge: bot input & input plumbing drop in unchanged.
- Total merge surface for the contract: **~20 playerState + ~12 entityState field swaps** in
  `q_shared.h` and the matching entries in `qcommon/msg.c`. Bounded and comprehensible.

### Coordinate limits (identical in both)
```c
#define MAX_WORLD_COORD ( 128*1024 )   // ±131072  — world/collision clamp
#define FLOAT_INT_BITS  13             // efficient coordinate range ≈ ±4096
```
ioq3 did **not** raise these. MFQ3 worked within them by modeling vehicles ~Quake-human size
(hence the LQM `0.1` scale). **Twist:** because our port is *standalone* (own protocol, no demo/
server compat), we *could* raise these for the first time ever — at the cost of rescaling maps/
models. Filed under "MFQ3 Remastered," not required for the port.

## Base build status — DONE
Stock ioquake3 builds clean in our environment (lean config, optional codecs off):
```
cmake -S ioq3-reference -B build -G "Visual Studio 17 2022" -A Win32 \
  -DBUILD_STANDALONE=ON -DUSE_OPENAL=OFF -DUSE_CODEC_VORBIS=OFF -DUSE_CODEC_OPUS=OFF \
  -DUSE_VOIP=OFF -DUSE_MUMBLE=OFF -DUSE_HTTP=OFF -DBUILD_RENDERER_GL2=OFF -DBUILD_GAME_QVMS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="/SAFESEH:NO" -DCMAKE_SHARED_LINKER_FLAGS="/SAFESEH:NO"
cmake --build build --config Release
```
- One fix needed: **`/SAFESEH:NO`** (the hand-written asm `.obj`s lack SAFESEH metadata).
- Produces: `ioquake3.exe`, `ioq3ded.exe`, `renderer_opengl1.dll`, `SDL2.dll`, and the stock mod
  as **native DLLs** (`cgame.dll`/`qagame.dll`/`ui.dll`) — exactly the slot MFQ3's modules fill.
- `BUILD_GAME_QVMS=OFF`: MFQ3's modules are **C++**, which the QVM bytecode path can't build, so we
  go DLL-only.

## Remaining port steps
1. Stand up `mfq3-ioq3` working tree (copy of the ioq3 base).
2. **Merge the shared structs** — add MFQ3's vehicle fields to ioq3's `playerState_t`/`entityState_t`
   and the matching `qcommon/msg.c` netfield tables (the bounded swap above). *Foundation — do first.*
3. Bring in MFQ3 `game`/`cgame`/`ui`/`vehiclelib`; reconcile the **trap/syscall interface** between
   the two forks; build the DLLs.
4. Re-apply small engine hooks (the aircraft pitch invert; the view-pitch ±90° clamp must be lifted
   for flight — stock Quake clamps it at `cl_input.c`).
5. Build & boot on Windows, then native Linux / Steam Deck.

The pmove dispatch (`PM_VehicleMove` → plane/ground/helo/boat/lqm) lives in the game module, so it
travels with the modules — not an engine hook.

---

## STRATEGY UPDATE — pivot to "SDL-ify the monolith"

While starting the port we found a decisive fact that changed the plan:

**MFQ3 has NO game↔engine module boundary.** Its game code calls engine functions *directly* by
their real names — `Com_Printf(...)`, `SV_LinkEntity(...)` (see `game/g_utils.c:518`) — **zero
`trap_` calls**, no syscall shim, no import struct in use. MFQ3 is a *true monolith*; game and
engine are one statically-linked binary.

ioquake3's engine, by contrast, *requires* the Quake-3 VM boundary: modules must call
`trap_LinkEntity()` → `syscall(G_LINKENTITY)` → engine. So the "MFQ3-as-ioq3-DLLs" port (Path B)
would mean **inserting a module boundary that doesn't exist** — rewriting every direct engine call
across all of `game`/`cgame`/`ui` to `trap_` equivalents (thousands of sites), plus the struct
merge and C++ DLL issues. Too invasive for the benefit.

**Decision: SDL-ify the monolith instead.** Keep MFQ3's engine+game monolith exactly as it is
(it already builds & runs on Windows), and swap only the **platform layer** — replace `win32/`
(WGL / DirectInput / DirectSound / Win32 `sys_`) with ioquake3's **`sdl/` + `sys/`** code, then make
the build cross-platform. Same destination (Linux / Steam Deck), zero gameplay-code changes, the
game stays working throughout.

### The platform seam (what we're swapping)
MFQ3's engine core calls the same Q3-lineage platform interface ioq3 implements via SDL — so it's
a backend swap, not an interface rewrite:
- `GLimp_Init/EndFrame/Shutdown/LogComment` (GL context) → ioq3 `sdl/sdl_glimp.c`
- `IN_Init/Frame/Shutdown/Activate` (input) → ioq3 `sdl/sdl_input.c`
- `SNDDMA_Init/GetDMAPos/BeginPainting/Submit/Shutdown` (sound) → ioq3 `sdl/sdl_snd.c`
- `Sys_*` (47 funcs) + console → ioq3 `sys/sys_main.c`, `sys_unix.c`, `sys_win32.c`, `con_*.c`
- Stub 4 SMP render-thread funcs single-threaded (`GLimp_SpawnRenderThread`, `WakeRenderer`,
  `FrontEndSleep`, `RendererSleep`) — ioq3 dropped SMP too.

### Gating items — all confirmed available
- SDL2 headers + libs bundled in ioq3 `code/thirdparty/SDL2-2.32.8/` (+ win32/win64/macos libs).
- Linux build: WSL present (docker-desktop distro) → build in a Linux container.
- Inline `__asm` (won't compile on GCC) is a bounded set — `qcommon/common.c`, `game/q_math.c`,
  `client/snd_mix.c`, `renderer/tr_shade_calc.c` — lift ioq3's portable C versions.

### Revised plan
1. Working tree `mfq3-portable` = copy of MFQ3's monolith source + ioq3's `sdl/`+`sys/`+SDL2.
2. Swap GL-context/input/sound/sys win32→SDL; **build on Windows with SDL2 first** (testable here).
3. C fallbacks for the inline asm; resolve remaining Win32-isms in engine core.
4. Cross-platform CMake; build native Linux in the WSL/Docker container → Steam Deck.

> Note: `mfq3-ioq3/` (a copy of the ioq3 base) is now only a *reference* for lifting SDL/sys files;
> the active working tree for this path is `mfq3-portable/` (MFQ3-based).
