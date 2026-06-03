# Feature: Helicopters

**Verdict: helos were never "removed" — they were unfinished WIP, and one missing asset
was bricking the gameset they lived in.** Unlike LQM (deliberately commented out), all three
helo rows were live in `bg_vehicledata.c`. The physics (`bg_helomove.c`), server entity
(`g_helo.c`), and renderer (`cg_helo.c` / `CG_DrawHelo`) were all intact. The feature just
needed **unblocking**, not reviving.

## What was wrong (and the fix)

1. **A missing model bricked the modern gameset.** The `RAH-66 Comanche` row pointed at
   `comanche.md3`, which never shipped, and `CG_CacheHelo` did a fatal `Com_Error` on it.
   Helos only exist in `MF_GAMESET_MODERN`, so that one missing file made *all* modern content
   unreachable. (Fixed earlier in `a545385`: warn-and-skip instead of fatal.)

2. **No map advertised a helo spawn.** The vehicle-select menu only lists a category if the
   level's `mf_lvcat` bitfield (built from spawn-point `category` keys) includes it
   (`ui_main.c:3577`, `(levelCats & (1 << vehicleCat))`). Norway's spawns are tagged only
   `1` (plane) and `2` (ground). **Fix:** override pk3 `zz_norway_helo.pk3` re-tags the BSP
   entity lump — plane spawns `1`→`5` (plane|helo), ground spawns `2`→`6` (ground|helo).
   Same-length single-digit byte edits, so BSP lump offsets don't move.

3. **No vehicle-select icons.** The `*_icon` assets aren't in the paks. **Fix:** override pk3
   `zz_helo_icons.pk3` with 480×360 JPG icons (UH-1 cropped from an in-game screenshot;
   generic `helo` + Comanche/Hind as silhouettes).

4. **Helo machine guns could never fire — a data bug.** MFQ3 only fires a vehicle MG from
   `weapons[0]` (`WP_MACHINEGUN`); `bg_pmove.c:636` gates on `weapons[WP_MACHINEGUN] > 0`,
   and `EV_FIRE_WEAPON` early-returns for `WP_MACHINEGUN` (`bg_pmove.c:650`). Every helo row
   had `weapons[0] = 0` with the gun mis-placed in slot 1, so the fire path was skipped.
   **Fix:** moved the gun to slot 0 with real ammo (1500 rds) on all three helos.

## The three helos now

- **UH-1 "Huey"** — complete shipped model (body + gun + turret + main/tail rotor + blur).
  Transport/resupply: 7.62mm MG + fuel/health/ammo parachute crates.
- **generic "helo"** — low-poly placeholder body; 2×30mm + Stinger + Hellfire.
- **Mi-24 Hind** — repointed the dead Comanche slot at the **Hind model already in pak2**
  (full part set: body, rotors+blur, gear, gunpod, turret, weapon pods). Zero new art.
  Attack class: 2×30mm + Stinger + Hellfire.

### Wiring the Hind (override pk3 `zz_hind.pk3`)
The Hind was authored for a later MFQ3 with a different file/skin scheme, so the alpha's
`CG_CacheHelo` naming convention needs renamed copies:
- `hind_body.md3` → `hind.md3`  (body; alpha expects `{model}.md3`)
- `24_turret.md3` → `hind_tur.md3`
- `hind_gunpod.md3` → `hind_gun.md3`
- `24_icon.jpg` → `hind_icon.jpg`
The rotor-blur models (`hind_mrotor_blur.md3`, `hind_trotor_blur.md3`) and textures
(`24_hind.tga`, `mrotor_blur.tga`, `trotor_blur.tga`) already have correct names/paths in
pak2 and resolve via the VFS. Body tags `tag_mrotor`/`tag_trotor`/`tag_turret` match the
renderer's `helo_tags[]`.

## Override pk3s (NOT in repo — game data, gitignored)
Built into `play/mfdata/`; rebuild recipe is in this doc + the session transcript:
- `zz_norway_helo.pk3` — BSP entity-lump re-tag (plane/ground spawns → also helo)
- `zz_helo_icons.pk3` — three 480×360 vehicle-select icons
- `zz_hind.pk3` — renamed Hind parts for the alpha's loader

## Remaining punch-list
- **Comanche** replaced by the Hind (no Comanche model ships). A real Apache (AH-64) would be
  an art project: CC-BY models exist on Sketchfab but need Blender→MD3 conversion with the
  right part splits and tag points (`tag_mrotor`/`tag_trotor`/`tag_turret`/`tag_weap`).
- **Hind gun barrel** may sit slightly off — its turret tag is `tag_weapon` vs the engine's
  expected `tag_weap` (cosmetic).
- **Client vehicle-sync quirk** — shared with LQM (`SYNCtrace ... -> skip`); the client's
  vehicle index can fail to sync, which is what the early "xyz marker" no-render was.
- **Other maps** still have no helo spawns (only Norway is re-tagged).
