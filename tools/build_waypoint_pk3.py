#!/usr/bin/env python3
"""
build_waypoint_pk3.py - package (and optionally rescale) the waypoint gate model
into zz_waypoint.pk3 and deploy it next to the game.

Re-runnable: drop the gate model + texture under the source dir, run this, and it
rebuilds the pk3. Use --scale to resize the .md3 in-engine without regenerating
the model (handy for dialling the gate size in once it's in the world).

Layout expected under the source dir (default: play/mfdata/_waypoint_src/):
    models/mapobjects/gate/gate.md3
    models/mapobjects/gate/gate.tga
    scripts/gate.shader            (optional)

Usage:
    python tools/build_waypoint_pk3.py                 # package as-is
    python tools/build_waypoint_pk3.py --scale 1.5     # 50% bigger gate
    python tools/build_waypoint_pk3.py --src some/dir --out play/mfdata/zz_waypoint.pk3
"""
import argparse
import os
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_SRC = os.path.join(ROOT, "play", "mfdata", "_waypoint_src")
DEFAULT_OUT = os.path.join(ROOT, "play", "mfdata", "zz_waypoint.pk3")


# ---- MD3 rescale -----------------------------------------------------------
# MD3 stores positions in two places we must scale together:
#   * per-frame bounds/origin/radius (floats)
#   * per-tag origins (floats)
#   * per-vertex XYZ (int16, in 1/64 units)
def scale_md3(data: bytes, factor: float) -> bytes:
    if factor == 1.0:
        return data
    buf = bytearray(data)
    magic, version = struct.unpack_from("<4si", buf, 0)
    if magic != b"IDP3":
        raise ValueError("not an MD3 file (bad magic)")
    (num_frames, num_tags, num_surfaces, num_skins,
     ofs_frames, ofs_tags, ofs_surfaces, ofs_end) = struct.unpack_from("<8i", buf, 76)

    # frames: mins[3], maxs[3], origin[3], radius, name[16] = 56 bytes
    for i in range(num_frames):
        off = ofs_frames + i * 56
        vals = list(struct.unpack_from("<10f", buf, off))      # 9 coords + radius
        vals = [v * factor for v in vals]
        struct.pack_into("<10f", buf, off, *vals)

    # tags: name[64], origin[3], axis[9] = 112 bytes; scale origin only
    for i in range(num_frames * num_tags):
        off = ofs_tags + i * 112 + 64
        o = [v * factor for v in struct.unpack_from("<3f", buf, off)]
        struct.pack_into("<3f", buf, off, *o)

    # surfaces: chain via each surface's ofsEnd
    soff = ofs_surfaces
    for _ in range(num_surfaces):
        (s_frames, s_shaders, s_verts, s_tris,
         s_ofs_tris, s_ofs_shaders, s_ofs_st, s_ofs_xyz, s_ofs_end) = \
            struct.unpack_from("<9i", buf, soff + 72)
        # XyzNormal: x,y,z int16 (1/64 units) + normal uint16 = 8 bytes
        base = soff + s_ofs_xyz
        for v in range(s_frames * s_verts):
            voff = base + v * 8
            x, y, z = struct.unpack_from("<3h", buf, voff)
            x = max(-32768, min(32767, int(round(x * factor))))
            y = max(-32768, min(32767, int(round(y * factor))))
            z = max(-32768, min(32767, int(round(z * factor))))
            struct.pack_into("<3h", buf, voff, x, y, z)
        soff += s_ofs_end
    return bytes(buf)


# ---- packaging -------------------------------------------------------------
def build(src: str, out: str, scale: float) -> None:
    if not os.path.isdir(src):
        sys.exit(f"source dir not found: {src}\n"
                 f"Create it and drop the gate model under "
                 f"models/mapobjects/gate/ first.")

    files = []
    for dirpath, _, names in os.walk(src):
        for n in names:
            full = os.path.join(dirpath, n)
            rel = os.path.relpath(full, src).replace(os.sep, "/")   # pk3 = forward slashes
            files.append((full, rel))
    if not files:
        sys.exit(f"no files under {src}")

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for full, rel in sorted(files, key=lambda t: t[1]):
            with open(full, "rb") as f:
                payload = f.read()
            if rel.lower().endswith(".md3") and scale != 1.0:
                payload = scale_md3(payload, scale)
                print(f"  scaled {rel} x{scale}")
            z.writestr(rel, payload)
            print(f"  + {rel}")
    print(f"\nwrote {out}  ({len(files)} files, scale x{scale})")
    print("Restart the game (or vid_restart) to load it. The gate path is set in "
          "g_missions.c -> GATE_MARKER_MODEL.")


def main() -> None:
    ap = argparse.ArgumentParser(description="Build/deploy zz_waypoint.pk3")
    ap.add_argument("--src", default=DEFAULT_SRC, help="source asset dir")
    ap.add_argument("--out", default=DEFAULT_OUT, help="output pk3 path")
    ap.add_argument("--scale", type=float, default=1.0,
                    help="rescale .md3 geometry by this factor (default 1.0)")
    args = ap.parse_args()
    build(args.src, args.out, args.scale)


if __name__ == "__main__":
    main()
