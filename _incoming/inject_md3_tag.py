#!/usr/bin/env python3
# Inject extra tag(s) into an existing single-frame MD3 (e.g. tag_ab1/tag_ab2 for
# the afterburner flame). Coords are in the md3's body-local space (+X fwd, +Y left,
# +Z up, centred). Axis = 180deg about Z so an attached effect points aft (out the
# exhaust). Usage: inject_md3_tag.py body.md3 tag_ab1:-66,9,-1 tag_ab2:-66,-9,-1
import sys,struct
path=sys.argv[1]; specs=sys.argv[2:]
d=bytes(open(path,'rb').read())
nFrames=struct.unpack_from('<i',d,76)[0]
nTags  =struct.unpack_from('<i',d,80)[0]
ofsTags=struct.unpack_from('<i',d,96)[0]
ofsSurf=struct.unpack_from('<i',d,100)[0]
ofsEof =struct.unpack_from('<i',d,104)[0]
assert nFrames==1, "only single-frame bodies supported (this is %d)"%nFrames
assert ofsSurf-ofsTags==nTags*112, "unexpected tag block size"
AXIS=[-1.0,0,0, 0,-1.0,0, 0,0,1.0]    # 180 about Z -> attached effect faces aft
newtags=b''
for s in specs:
    name,coords=s.split(':'); x,y,z=[float(v) for v in coords.split(',')]
    nm=name.encode('latin1')[:63].ljust(64,b'\0')
    newtags+=nm+struct.pack('<3f',x,y,z)+struct.pack('<9f',*AXIS)
K=len(specs); added=K*112
out=bytearray(d[:ofsTags] + d[ofsTags:ofsSurf] + newtags + d[ofsSurf:])
struct.pack_into('<i',out,80,nTags+K)          # numTags
struct.pack_into('<i',out,100,ofsSurf+added)   # ofsSurfaces
struct.pack_into('<i',out,104,ofsEof+added)    # ofsEof (file end)
open(path,'wb').write(out)
print("injected %d tag(s) into %s -> numTags now %d"%(K,path,nTags+K))
