#!/usr/bin/env python
"""Generate mfq3_carrier.map: an open-sea level with a boxy aircraft-carrier.

All geometry is axis-aligned boxes. Plane winding is made robust by computing
the normal the way q3map2/the engine does -- normal = cross(p2-p0, p1-p0) --
and swapping p1/p2 whenever that disagrees with the desired outward normal.
Scale: MFQ3 runs ~4.3 units/metre (A-10 = 70u). Carrier deck ~1470u (~342m).
"""
import os

def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def sub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def dot(a, b):
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]

def face(p0, p1, p2, n, tex):
    # ensure engine-computed normal cross(p2-p0,p1-p0) points along outward n
    N = cross(sub(p2, p0), sub(p1, p0))
    if dot(N, n) < 0:
        p1, p2 = p2, p1
    pts = " ".join("( %d %d %d )" % p for p in (p0, p1, p2))
    return "%s %s 0 0 0 0.5 0.5 0 0 0" % (pts, tex)

def box(mins, maxs, tex, faces=None):
    """faces: optional dict mapping '+x','-x','+y','-y','+z','-z' -> texture."""
    x0, y0, z0 = mins
    x1, y1, z1 = maxs
    faces = faces or {}
    def t(key):
        return faces.get(key, tex)
    # 8 corners
    c = {
        (0,0,0):(x0,y0,z0),(1,0,0):(x1,y0,z0),(0,1,0):(x0,y1,z0),(1,1,0):(x1,y1,z0),
        (0,0,1):(x0,y0,z1),(1,0,1):(x1,y0,z1),(0,1,1):(x0,y1,z1),(1,1,1):(x1,y1,z1),
    }
    F = []
    F.append(face(c[(1,0,0)], c[(1,1,0)], c[(1,0,1)], (1,0,0),  t('+x')))  # +X
    F.append(face(c[(0,0,0)], c[(0,1,0)], c[(0,0,1)], (-1,0,0), t('-x')))  # -X
    F.append(face(c[(0,1,0)], c[(1,1,0)], c[(0,1,1)], (0,1,0),  t('+y')))  # +Y
    F.append(face(c[(0,0,0)], c[(1,0,0)], c[(0,0,1)], (0,-1,0), t('-y')))  # -Y
    F.append(face(c[(0,0,1)], c[(1,0,1)], c[(0,1,1)], (0,0,1),  t('+z')))  # +Z
    F.append(face(c[(0,0,0)], c[(1,0,0)], c[(0,1,0)], (0,0,-1), t('-z')))  # -Z
    return "{\n" + "\n".join(F) + "\n}\n"

# NOTE: q3map2 auto-prepends "textures/" to face shader names, so these are
# written WITHOUT that prefix (common/caulk is the documented exception).
CAULK = "common/caulk"
WATER = "norway/water1"     # -> textures/norway/water1 (plain image: SOLID + seals).
                            # The animated "textures/norway/water" shader is
                            # surfaceparm nonsolid -> would leak / fall-through.
SKY   = "mfsea/sky"         # -> textures/mfsea/sky (custom blue skybox, shipped in pk3)
METAL = "field/metal"       # -> textures/field/metal (plain image in pak0)

WATERVOL = "mfsea/water"   # real CONTENTS_WATER volume shader (shipped in pk3)

# --- world extents ---
WX = 12000        # half horizontal extent
SEA_TOP = 0       # water surface z=0
SEABED_TOP = -256 # water is a VOLUME from the seabed up to the surface
FLOOR_BOT = -320
CEIL = 6000
WALL = 64         # wall/ceiling thickness

brushes = []

# Water VOLUME (all faces = water shader -> CONTENTS_WATER, nonsolid). Boats float
# and move here (bg_boatmove gates velocity on waterlevel); aircraft ditch in it.
# Only the top face (z=0) is exposed and renders the animated surface. The model
# origin sits inside this (water = not solid -> q3map2 bakes it, no "in solid").
brushes.append(box((-WX, -WX, SEABED_TOP), (WX, WX, SEA_TOP), WATERVOL))
# Seabed: solid floor beneath the water (boats/sunk planes rest here; seals bottom).
brushes.append(box((-WX, -WX, FLOOR_BOT), (WX, WX, SEABED_TOP), CAULK))
# Skybox enclosure: ceiling + 4 walls spanning seabed..ceiling (seal the sides).
brushes.append(box((-WX-WALL, -WX-WALL, CEIL), (WX+WALL, WX+WALL, CEIL+WALL), SKY))
brushes.append(box((WX, -WX-WALL, FLOOR_BOT), (WX+WALL, WX+WALL, CEIL), SKY))
brushes.append(box((-WX-WALL, -WX-WALL, FLOOR_BOT), (-WX, WX+WALL, CEIL), SKY))
brushes.append(box((-WX-WALL, WX, FLOOR_BOT), (WX+WALL, WX+WALL, CEIL), SKY))
brushes.append(box((-WX-WALL, -WX-WALL, FLOOR_BOT), (WX+WALL, -WX, CEIL), SKY))

# --- Carrier ---
# The VISIBLE hull is the real CVN-65 OBJ, baked into the BSP as a misc_model
# (see entities below). q3map2-baked model surfaces are render-only, so we add
# invisible CAULK clip brushes (solid + nodraw) for collision, aligned to the
# model's geometry.  ===== TUNABLE: nudge these two then rebuild =====
MODEL_SCALE = 3.08   # ~342m carrier at MFQ3's ~4.3 u/m
# Vertical: with the -90 pre-rotation the baked flight deck lands at origin_z + 139
# and the keel at origin_z + 12 (measured at scale 3.08). MODEL_Z=-61 -> deck z~78,
# keel ~z-49. The origin (z=-61) lands in the hidden underwater pocket (between the
# sea sheet at -16 and the floor at -448), so it's NOT in solid and bakes fine.
MODEL_Z     = -69   # deck ~z70, keel ~z-57 (z51 was too low; z78 a touch high)
MODEL_OX, MODEL_OY = -20, -21   # centre the rotated model's bbox on (0,0)

# Clip geometry aligned to the baked (upright) model: deck at MODEL_Z+139.
SC  = MODEL_SCALE / 3.08
HL  = 730    # half deck length (X)
HDw = 197    # half deck beam (Y)
HBw = 158    # half hull beam (kept inside the tapered visible hull to avoid an
             # invisible wall the boat hits driving alongside)
DECK_Z = round(MODEL_Z + 139 * SC)            # flight-deck surface height (~78)
KEEL_Z = round(MODEL_Z + 12 * SC)             # ~ -49 (under the sea, hidden)

# hull clip: a touch under the deck down past the waterline (keel is hidden by sea)
brushes.append(box((-HL, -HBw, KEEL_Z), (HL, HBw, DECK_Z - 4), CAULK))
# flight-deck clip: emitted as a func_runway ENTITY (below), not worldspawn -
# the engine's canLandOnIt() only treats func_runway/plat/train/door surfaces as
# landable, so a worldspawn deck makes parked planes "un-land" and stall-dive.
# DECK_TOP a few units above the visible deck so wheels (which hang below the
# bbox mins) sit on the surface instead of sinking through it.
DECK_TOP = DECK_Z + 5
deck_brush = box((-HL, -HDw, DECK_TOP - 8), (HL, HDw, DECK_TOP), CAULK)
# island / tower clip (starboard side, rough block so you don't fly through it)
brushes.append(box((40, 60, DECK_Z), (320, HDw, DECK_Z + 180), CAULK))

# Boat-spawn pads: small caulk blocks BELOW the surface. The boat spawn traces down
# (MASK_SOLID, ignores water) and seats on the first solid; its own +-10 surface-clamp
# then snaps it up onto the water (z0). Pad top must sit clear BELOW the floating
# hull bottom (z-2.3) or the boat scrapes it -- the "invisible wall" bug. Top at -12
# gives ~10u clearance, still within the spawn-clamp's reach so the boat snaps up.
for px, py in [(900, 650), (-900, 650), (900, -650), (-900, -650)]:
    brushes.append(box((px - 48, py - 48, -20), (px + 48, py + 48, -12), CAULK))

# Coarse lightgrid: a 24000-unit world at the default 64-unit grid would be a
# ~50MB lightgrid lump (engine cap is 8MB). 512 keeps it tiny.
worldspawn = ('{\n"classname" "worldspawn"\n"message" "MFQ3 Carrier Sea"\n'
              '"_color" "1 1 1"\n"ambient" "60"\n"gridsize" "512 512 512"\n'
              + "".join(brushes) + "}\n")

# --- point entities ---
# MFQ3 filters spawn points by vehicle category (spot.ent_category_ & vehicle.cat).
# CAT bits: PLANE 1, GROUND 2, HELO 4, LQM 8, BOAT 16.
CAT_AIR     = 1 | 4        # planes + helos -> high airborne spawns
CAT_SURFACE = 2 | 8 | 16   # ground + infantry + boats -> sea-surface spawns
ents = []
def spawn(x, y, z, cat, start=False):
    cls = "info_player_start" if start else "info_player_deathmatch"
    return ('{\n"classname" "%s"\n"origin" "%d %d %d"\n'
            '"angle" "%d"\n"category" "%d"\n}\n' % (cls, x, y, z, 0, cat))

# air spawns: high over the carrier (planes/helos start flying)
for x, y in [(-500, 0), (0, 0), (500, 0)]:
    ents.append(spawn(x, y, 1200, CAT_AIR))
ents.append(spawn(0, 0, 1200, CAT_AIR, start=True))
# boat (and ground/infantry) spawns: in open water alongside the carrier. z=100 ->
# the spawn traces down 512u onto the solid sea (z=0) and seats the hull there.
for x, y in [(900, 650), (-900, 650), (900, -650), (-900, -650)]:
    ents.append(spawn(x, y, 100, CAT_SURFACE))
# the real CVN-65, baked into the BSP. _remap collapses all 11 (textureless)
# materials to haze-gray metal. Geometry is baked at compile -> no model files
# need to ship in the pk3, only the recompiled .bsp.
ents.append(
    '{\n"classname" "misc_model"\n'
    '"model" "models/mapobjects/cvn65/cvn65_tex.obj"\n'
    '"origin" "%d %d %d"\n' % (MODEL_OX, MODEL_OY, MODEL_Z) +
    '"modelscale" "%.3f"\n' % MODEL_SCALE +
    '"angle" "0"\n}\n')
# the flight deck as a func_runway brush entity -> planes can park/land on it
ents.append('{\n"classname" "func_runway"\n' + deck_brush + '}\n')

# arrestor-wire trap zone: a thin volume over the aft+mid deck. A landed plane
# rolling through it is yanked to a stop (g_missions.c G_ArrestorFrame).
arrestor = box((-690, -180, DECK_TOP - 6), (420, 180, DECK_TOP + 60), CAULK)
ents.append('{\n"classname" "func_arrestor"\n' + arrestor + '}\n')

# catapult zone: the aft-to-mid deck where you spawn/park. Press the action key
# while parked here for a steam-cat shot off the bow (deck-only; see mf_client.c).
catapult = box((-700, -180, DECK_TOP - 6), (300, 180, DECK_TOP + 60), CAULK)
ents.append('{\n"classname" "func_catapult"\n' + catapult + '}\n')

# a couple of lights so the metal reads (sky already lights via -light)
for x in (-600, 600):
    ents.append('{\n"classname" "light"\n"origin" "%d 0 1500"\n"light" "8000"\n}\n' % x)

out = worldspawn + "".join(ents)

dst = os.path.join(os.path.dirname(__file__), "mapsrc")
os.makedirs(dst, exist_ok=True)
path = os.path.join(dst, "mfq3_carrier.map")
with open(path, "w", newline="\n") as f:
    f.write(out)
print("wrote", path)
print("brushes:", len(brushes), " entities:", len(ents)+1)
print("carrier clip: %dx%d units, deck z=%d (model scale %.2f, z=%d)"
      % (HL*2, HDw*2, DECK_Z, MODEL_SCALE, MODEL_Z))
