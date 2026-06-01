# Military Forces (MFQ3) — Revival & Modernization

This repo is a preservation/revival of **Military Forces (MFQ3)**, a late-1990s/early-2000s
Quake 3 total-conversion: vehicular combat (planes, helicopters, tanks, boats) on the id Tech 3
engine. This documentation captures (a) how we got the original building and running again on a
modern machine, (b) the plan to modernize it onto **ioquake3**, and (c) deep feasibility recon
of three dormant features the original team built but never shipped.

## The North-Star scenario ("Mission 1")

Everything we're building points at one 60-second loop the project owner described:

> **Spawn on a runway as a little person → run to a plane → get in → fly out and destroy a
> target → return to base → "Mission 1 complete."**

That single experience exercises *every* dormant system below:
| Step | System |
|---|---|
| spawn as a little person | **Infantry / LQM** (`CAT_LQM`) |
| run to a plane, get in | **Enter/Exit vehicle** (the one genuinely-new mechanic) |
| destroy a target | **Bots / NPC AI** |
| return to base, "complete" | **Single-Player / Missions** (objectives) |

See [06-roadmap.md](06-roadmap.md) for how Mission 1 decomposes into concrete milestones.

## Current status

| Milestone | Status |
|---|---|
| Classic MFQ3 builds from source (x86, MSVC v143) | ✅ done |
| Classic MFQ3 runs — menu, maps, flight | ✅ done (boots to a live map) |
| New feature: aircraft-only pitch invert (`m_invertAircraftPitch`) | ✅ done |
| Stock ioquake3 base builds (client + ded + GL1 + SDL2 + mod DLLs) | ✅ done |
| MFQ3 ported onto ioquake3 engine | ⏳ in progress |
| Reactivate LQM "little people" | 🔜 next |
| Vehicle bots / NPCs | 🔜 planned |
| Single-player missions | 🔜 planned |
| Native Linux / Steam Deck build | 🔜 later |

## The big discovery

The original team's **unreleased rewrite left a trove of nearly-complete but disabled systems.**
Across three independent code investigations we found that infantry, bots, and missions are
mostly *reactivation* work, not from-scratch features:
- **Infantry ("LQM" = Little Quaky Man): ~80% complete**, fully wired, just commented out.
- **Bots:** a clean hook point exists; the live targeting brain is reusable; ioquake3 donates the
  plumbing. Feasible.
- **Missions:** a working in-game editor + file format + parser exist; only the server-side
  spawner is commented out (the client/server "last mile").

## Document index
- [01-build-and-run.md](01-build-and-run.md) — toolchain, building & running classic MFQ3, fixes applied
- [02-ioq3-port.md](02-ioq3-port.md) — modernizing onto ioquake3: rationale, merge scoping, status
- [03-infantry-lqm.md](03-infantry-lqm.md) — the dormant soldier system
- [04-bots.md](04-bots.md) — vehicle bots (air/ground/sea)
- [05-singleplayer-missions.md](05-singleplayer-missions.md) — SP & the in-game mission editor
- [06-roadmap.md](06-roadmap.md) — unified incremental roadmap toward Mission 1

## Key locations
- Original source (revived): `Military Forces/source/code/` (+ `CMakeLists.txt` we authored)
- Playable build + game data: `play/` (`MilitaryForces.exe` + `mfdata/`)
- ioquake3 base (modern engine): `ioq3-reference/`
- Steam Deck demo package (Proton): `dist/MFQ3-SteamDeck.zip`
