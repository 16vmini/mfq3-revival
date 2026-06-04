#!/usr/bin/env python3
"""
fix_f16_pylons.py  --  repair the F-16 model's weapon-pylon tags.

THE BUG
-------
MFQ3 builds each plane's weapon loadout by reading weapon-mount ("pylon") tags
out of the vehicle .md3 model. The engine looks for tags whose name *begins with*
"tag_P" (bg_loadouts.c -> MF_distributeWeaponsOnPylons -> MF_getTagsContaining,
which does Q_strncmp(name, "tag_P", 5)). If it finds none, the loadout is empty
and the plane spawns with NO air-to-air missiles (only the gun + flares).

The stock F-16 model (models/vehicles/planes/f-16/f-16.md3 inside pak0) has 19
perfectly-positioned pylon tags, but they are named with a "PY..." prefix instead
of "tag_P...". So the engine never finds them -> F-16 spawns missile-less, while
planes whose models use the correct "tag_P" prefix (e.g. the Saab/jas-39) work.

THE FIX
-------
Rename every "PY..." tag to a valid "tag_P" pylon name. md3 tag names live in a
fixed 64-byte field, so this is a pure in-place byte overwrite -- no md3 offsets
change. The name format the engine parses is:

    "tag_P" + pos(1 hex) + group(1 hex) + flags(4 hex) + side('L'/'R')   (12 chars)

where `flags` is an OR of the PF_* pylon-capability bits (bg_public.h). We set
flags = 0x00FF (all AA + AG munition types, no control bits) so the Sidewinder
(PF_AA_LT = 0x0001) and AMRAAM (PF_AA_MED = 0x0002) both fit. Pylons are ranked
by |y| so the outermost stations are filled first (Sidewinders to the wingtips).

Tag names are identical across all md3 frames, so we rewrite the name in every
frame's copy of each pylon tag.

USAGE
-----
    python tools/fix_f16_pylons.py <in_f-16.md3> <out_f-16.md3>

Extract the input from pak0 first, e.g.:
    unzip play/mfdata/pak0.pk3 models/vehicles/planes/f-16/f-16.MD3
Then pack the output into an override pak (zz_f16_pylons.pk3) under
play/mfdata/ as  models/vehicles/planes/f-16/f-16.md3  (forward slashes).
"""

import struct
import sys


def fix(in_path, out_path):
    with open(in_path, 'rb') as f:
        d = bytearray(f.read())

    if d[0:4] != b'IDP3':
        raise SystemExit(f"{in_path}: not an md3 file (bad magic)")

    (_flags, numFrames, numTags, _numSurfaces, _numSkins,
     _ofsFrames, ofsTags, _ofsSurfaces, _ofsEnd) = struct.unpack_from('<9i', d, 72)
    TAGSZ = 112  # name[64] + origin[3] + axis[3][3]

    def tag_off(frame, t):
        return ofsTags + (frame * numTags + t) * TAGSZ

    def name_of(frame, t):
        return d[tag_off(frame, t):tag_off(frame, t) + 64].split(b'\0')[0].decode('latin1')

    def origin_y(frame, t):
        return struct.unpack_from('<3f', d, tag_off(frame, t) + 64)[1]

    # collect pylon tags (frame-0 name begins with "PY"); keep side + |y|
    pylons = []
    for t in range(numTags):
        nm = name_of(0, t)
        if nm.startswith('PY'):
            side = 'L' if nm[-1:].upper() == 'L' else 'R'
            pylons.append((t, abs(origin_y(0, t)), side, nm))

    if not pylons:
        raise SystemExit(f"{in_path}: no 'PY' pylon tags found (already fixed?)")

    pylons.sort(key=lambda p: -p[1])   # outer wing first -> lowest pos

    FLAGS = "00FF"   # PF_AA_LT|MED|HVY|PHX | PF_AG_LT|MED|HVY|GDA  (no control bits)
    print(f"{in_path}: frames={numFrames} tags={numTags} pylons={len(pylons)}")
    for rank, (t, absy, side, oldnm) in enumerate(pylons):
        newnm = f"tag_P{rank & 0xF:X}{(rank >> 4) & 0xF:X}{FLAGS}{side}"  # 12 chars
        assert len(newnm) == 12, newnm
        nb = newnm.encode('latin1') + b'\0' * (64 - len(newnm))
        for fr in range(numFrames):
            off = tag_off(fr, t)
            d[off:off + 64] = nb
        print(f"  tag[{t:2}] {oldnm:>12} -> {newnm}  (|y|={absy:.1f})")

    with open(out_path, 'wb') as f:
        f.write(d)
    print(f"written: {out_path} ({len(d)} bytes)")


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    fix(sys.argv[1], sys.argv[2])
