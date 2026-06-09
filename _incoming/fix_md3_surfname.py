#!/usr/bin/env python3
# Repair MD3s written with a non-standard 32-byte surface NAME field (engine expects
# 64): expands each surface header by inserting 32 zero bytes after the name and
# bumping the surface-relative offsets (+32) and the file-level ofsEnd. The garbage
# this caused in R_LoadMD3 was crashing the GL driver on first draw (LQM sarge).
# Usage: fix_md3_surfname.py in.md3 out.md3
import sys, struct
inp, outp = sys.argv[1], sys.argv[2]
d = bytearray(open(inp, 'rb').read())
nSurf  = struct.unpack_from('<i', d, 84)[0]
ofsSurf= struct.unpack_from('<i', d, 100)[0]
out = bytearray(d[:ofsSurf])
so = ofsSurf
for s in range(nSurf):
    ident = bytes(d[so:so+4])
    name  = bytes(d[so+4:so+36])                  # 32-byte variant name
    flags, = struct.unpack_from('<i', d, so+36)
    nF,nSh,nV,nT,oT,oSh,oST,oX,oE = struct.unpack_from('<9i', d, so+40)
    body  = bytes(d[so+76:so+oE])                 # tris..xyz, offsets were relative to surface start
    hdr   = ident + name.ljust(64, b'\0') + struct.pack('<10i', flags, nF, nSh, nV, nT,
              oT+32, oSh+32, oST+32, oX+32, oE+32)
    assert len(hdr) == 108
    out += hdr + body
    so += oE
struct.pack_into('<i', out, 104, len(out))        # file ofsEnd
open(outp, 'wb').write(out)
print("fixed %s -> %s (%d surfaces, %d -> %d bytes)" % (inp, outp, nSurf, len(d), len(out)))
