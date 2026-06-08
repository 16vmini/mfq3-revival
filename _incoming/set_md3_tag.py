#!/usr/bin/env python3
# Update an existing tag's origin (and optionally axis) in a single-frame MD3, in place.
# Usage: set_md3_tag.py body.md3 tag x y z [identity|flip]
#   identity = axis [1,0,0, 0,1,0, 0,0,1]   (effect faces along body +X / fwd)
#   flip     = axis [-1,0,0, 0,-1,0, 0,0,1] (180 about Z -> faces aft)
import sys,struct
path,tag=sys.argv[1],sys.argv[2]
x,y,z=float(sys.argv[3]),float(sys.argv[4]),float(sys.argv[5])
mode=sys.argv[6] if len(sys.argv)>6 else None
AX={'identity':[1.0,0,0, 0,1.0,0, 0,0,1.0],'flip':[-1.0,0,0, 0,-1.0,0, 0,0,1.0]}
d=bytearray(open(path,'rb').read())
nTags=struct.unpack_from('<i',d,80)[0]
ofsTags=struct.unpack_from('<i',d,96)[0]
for i in range(nTags):
    base=ofsTags+i*112
    if d[base:base+64].split(b'\0')[0].decode('latin1')==tag:
        struct.pack_into('<3f',d,base+64,x,y,z)
        if mode in AX: struct.pack_into('<9f',d,base+76,*AX[mode])
        open(path,'wb').write(d)
        print("set %s -> origin(%.1f,%.1f,%.1f) axis=%s"%(tag,x,y,z,mode or 'unchanged')); sys.exit(0)
print("tag %s not found"%tag); sys.exit(1)
