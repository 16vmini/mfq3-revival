# Military Forces (MFQ3) — Revival & Modernization

A revival of **Military Forces (MFQ3)**, a late-1990s / early-2000s Quake 3 total-conversion:
vehicular combat (planes, helicopters, tanks, boats) on the id Tech 3 engine. This repo gets the
original source building & running on a modern toolchain, and tracks the work to reactivate
features the original team built-but-shelved, plus a path to cross-platform (Steam Deck).

> **Source preserved from** [marek/MilitaryForces](https://github.com/marek/MilitaryForces).
> Engine is GPLv2 (id Tech 3). The original game-data `.pk3` paks are **not** included here
> (they're the team's unreleased alpha content). The bundled soldier model in `assets/` is CC0.

## Status
- ✅ Builds from source (x86, MSVC v143) — see [docs/01-build-and-run.md](docs/01-build-and-run.md)
- ✅ Runs: menu, maps, flight, on a modern GPU
- ✅ New: aircraft-only pitch invert (`m_invertAircraftPitch`)
- ⏳ Reactivating "little people" (LQM infantry) — model in `assets/lqm_sarge.pk3`
- 🔜 Bots, single-player missions, cross-platform / Steam Deck

Full picture & roadmap: **[docs/README.md](docs/README.md)**

## The guiding scenario ("Mission 1")
> Spawn on a runway as a little person → run to a plane → get in → destroy a target → return to
> base → "Mission 1 complete." — it exercises every dormant system (infantry, enter/exit, bots, missions).

## Build (Windows)
```
cd "Military Forces/source/code"
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```
Needs VS 2022 Build Tools (C++ x86). Run the resulting `MilitaryForces.exe` from a folder
containing an `mfdata/` game-data directory.

## For collaborators
- **Code**: `Military Forces/source/code/` (monolithic engine+game, built as C++).
- **Build**: `Military Forces/source/code/CMakeLists.txt` (reconstructed from the old VS2005 projects).
- **Docs / deep-dives** (with `file:line` citations): `docs/` — feasibility of infantry, bots, missions,
  the ioquake3 port plan, and the full roadmap.
- **Bots** (active work): see [docs/04-bots.md](docs/04-bots.md). A bot = something that fills a
  `usercmd_t` each frame; hook at `game/g_active.c` `ClientThink_real`; reuse the live targeting
  brain `game/g_mfq3util.c::updateTargetTracking` + missile seeker math in `game/g_missile.c`;
  suggested new file `game/g_vehiclebot.c`. Land it on a `feature/bots` branch → PR.
- **LQM (infantry)**: data `game/bg_vehicledata.c` (Sarge row ~3101), model loader
  `cgame/cg_vehicle.c::CG_CacheLQM` (expects `models/vehicles/lqms/<name>/<name>_{legs,torso,head}.md3`
  + `animation.cfg`), movement `game/bg_lqmmove.c`, render `cgame/cg_vehicledraw.c::CG_DrawLQM`.
- **Soldier model** (CC0, built from scratch): `assets/lqm_sarge.pk3` — drop into `mfdata/`.

## Contributing (branches & PRs)
Work on a feature branch (`feature/bots`, `feature/lqm`, …) and open a Pull Request against `main`.
Keep changes scoped; reference the relevant `docs/` section. PRs are reviewed and merged into `main`.
