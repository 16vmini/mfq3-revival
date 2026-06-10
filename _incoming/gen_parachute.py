#!/usr/bin/env python3
# Generate a parachute canopy MD3 (hemisphere dome + suspension lines) at world
# scale for the MFQ3 LQM (drawn unscaled above the soldier). Writes parachute.md3
# + parachute.tga into the staging dir given as argv[1].
import sys, os, math, struct
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
SHADER = b"models/vehicles/lqms/parachute"

verts=[]; tris=[]; st=[]
def V(x,y,z,u,v):
    verts.append((x,y,z)); st.append((u,v)); return len(verts)-1

# dome: rings x segments hemisphere, radius 8, squashed, apex z=14.4, rim z=9.6
R=8.0; SEG=10; RINGS=3; Z0=9.6; ZS=0.6
ring_idx=[]
for r in range(RINGS+1):
    phi = (math.pi/2) * r/RINGS          # 0=apex .. pi/2=rim
    rad = R*math.sin(phi); z = Z0 + R*math.cos(phi)*ZS
    row=[]
    for s in range(SEG):
        a = 2*math.pi*s/SEG
        row.append(V(rad*math.cos(a), rad*math.sin(a), z, 0.5+0.45*math.sin(phi)*math.cos(a), 0.5+0.45*math.sin(phi)*math.sin(a)))
    ring_idx.append(row)
for r in range(RINGS):
    for s in range(SEG):
        s2=(s+1)%SEG
        a,b,c,d = ring_idx[r][s], ring_idx[r][s2], ring_idx[r+1][s], ring_idx[r+1][s2]
        if r>0: tris.append((a,b,c))
        tris.append((b,d,c))
        # inside faces (visible from below)
        if r>0: tris.append((a,c,b))
        tris.append((b,c,d))
# suspension lines: 4 thin triangles rim -> harness point (0,0,1.8)
for k in range(4):
    a = 2*math.pi*k/4 + math.pi/4
    rim = V(R*math.cos(a), R*math.sin(a), Z0, 0.05,0.05)
    rim2= V(R*math.cos(a+0.06), R*math.sin(a+0.06), Z0, 0.06,0.05)
    low = V(0,0,1.8, 0.05,0.06)
    tris.append((rim,rim2,low)); tris.append((rim2,rim,low))

def enc_normal(n):
    x,y,z=n; l=math.sqrt(x*x+y*y+z*z) or 1; x,y,z=x/l,y/l,z/l
    lat=int(math.atan2(y,x)*255/(2*math.pi)) & 255
    lng=int(math.acos(max(-1,min(1,z)))*255/(2*math.pi)) & 255
    return (lat<<8)|lng

mins=[min(v[i] for v in verts) for i in range(3)]
maxs=[max(v[i] for v in verts) for i in range(3)]
rad=max(math.sqrt(v[0]**2+v[1]**2+v[2]**2) for v in verts)

surf  = b'IDP3' + b'canopy'.ljust(64,b'\0') + struct.pack('<i',0)
hdr10 = struct.pack('<9i', 1,1,len(verts),len(tris), 108, 108+len(tris)*12, 108+len(tris)*12+68, 108+len(tris)*12+68+len(verts)*8, 0)
tri_b = b''.join(struct.pack('<3i',*t) for t in tris)
sh_b  = SHADER.ljust(64,b'\0') + struct.pack('<i',0)
st_b  = b''.join(struct.pack('<2f',u,v) for u,v in st)
xyz_b = b''.join(struct.pack('<3hH', int(v[0]*64),int(v[1]*64),int(v[2]*64), enc_normal((v[0],v[1],max(v[2]-Z0,0.1)))) for v in verts)
sEnd  = 108+len(tri_b)+len(sh_b)+len(st_b)+len(xyz_b)
surf  = b'IDP3' + b'canopy'.ljust(64,b'\0') + struct.pack('<10i',0,1,1,len(verts),len(tris),108,108+len(tri_b),108+len(tri_b)+68,108+len(tri_b)+68+len(st_b),sEnd)
surf += tri_b + sh_b + st_b + xyz_b

frame = struct.pack('<3f',*mins)+struct.pack('<3f',*maxs)+struct.pack('<3f',0,0,0)+struct.pack('<f',rad)+b'chute'.ljust(16,b'\0')
ofsFrames=108; ofsTags=ofsFrames+56; ofsSurf=ofsTags
hdr = b'IDP3'+struct.pack('<i',15)+b'parachute'.ljust(64,b'\0')+struct.pack('<i',0)
hdr += struct.pack('<4i',1,0,1,0)+struct.pack('<4i',ofsFrames,ofsTags,ofsSurf,ofsSurf+len(surf))
open(os.path.join(out,'parachute.md3'),'wb').write(hdr+frame+surf)

# texture: olive canopy with cream panels, 32x32
import itertools
W=H=32; px=bytearray()
for y,x in itertools.product(range(H),range(W)):
    panel=((x*6)//W)%2
    col=(96,112,72) if panel else (210,205,180)
    px += bytes((col[2],col[1],col[0]))     # TGA = BGR
tga=bytearray(18); tga[2]=2; struct.pack_into('<H',tga,12,W); struct.pack_into('<H',tga,14,H); tga[16]=24; tga[17]=0x20
open(os.path.join(out,'parachute.tga'),'wb').write(bytes(tga)+bytes(px))
print("wrote parachute.md3 (%d verts %d tris) + parachute.tga"%(len(verts),len(tris)))
