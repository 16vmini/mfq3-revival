# MFQ3 Revival — Model & Asset Credits

Third-party 3D models added during the revival, with sources and licenses.
**CC-BY requires we credit the author** (in-game credits and/or this file).
Record EVERY downloaded asset here: name, author, source URL, license, date.

## Aircraft models

| Vehicle | Author | Source | License | File | Notes |
|---------|--------|--------|---------|------|-------|
| **F-35B Lightning II** | AF267 (@jsong.js.us) | https://sketchfab.com/3d-models/lockheed-martin-f-35b-lightning-ii-5d54a6af45974ad386ae74d42b33374a | CC-BY 4.0 | `lockheed_martin_f-35b_lightning_ii.glb` | Fully articulated (gear, VTOL nozzle/fan/doors, control surfaces). Used for the VTOL F-35B. |
| **C-5 Galaxy** | manilov.ap | https://sketchfab.com/3d-models/c5-056cfd8c9b984719b1f96fd7829c981d | CC-BY 4.0 | `056cfd8c9b984719b1f96fd7829c981d.glb` | ~22k tris, embedded textures. Transport class. No landing gear in model (cruise config). |
| **SR-71 Blackbird** | KOG_THORNS (@ioai25312) | https://sketchfab.com/3d-models/lockheed-sr-71-blackbird-e2400e6119f5414c89e075654a82d30a | CC-BY 4.0 | `lockheed_sr-71_blackbird.glb` | 49k tris, 7 textures. Fast recon/interceptor (fighter class). Has gear (merged into body group, not yet animated). |
| **MQ-9 Reaper** | _TO CONFIRM_ | _TO CONFIRM_ (downloaded OBJ) | _TO CONFIRM_ | `uploads_files_800272_MQ-9.obj` + Textures.zip | Converted to the in-game Reaper (spinning prop + retract gear). **Please confirm the download page/author/license.** |
| **B-52 Stratofortress** (planned) | bohmerang | https://sketchfab.com/3d-models/boeing-b-52-stratofortress-38b0c64bd552431394efa8625d7f5144 | CC-BY 4.0 | (pending download) | Bomber class. |

## Naval models

| Vehicle | Author | Source | License | File | Notes |
|---------|--------|--------|---------|------|-------|
| **Submarine — "Red October"** (Typhoon-class) | DigitalGreaseMonkey | https://sketchfab.com/3d-models/red-october-0bdbd4ce120c4440ba2a343572b7bf7a | CC-BY 4.0 | `red_october.glb` → `models/vehicles/sea/warsub` | Player submarine. glTF→MD3 via `tools/glb_to_md3.py`, rescaled to ~1000u, Y-up→Z-up, bow +X. Texture = model albedo. |
| **Waypoint markers** (gate ring / flag / buoy) | "Uncle Mark's" model bot (commissioned) | custom for this project | project-owned | `models/mapobjects/waypoints`, `.../gate` | Mission course markers + the earlier 6-part warsub rig (now replaced by Red October). |

## Notes
- Existing original MFQ3 content (F-16, GR-7 Harrier, Hind, etc.) is from the
  original mod team and is not listed here.
- When downloading a new model: grab the **author name + page URL + license** and
  add a row above before converting it.
