#!/usr/bin/env python3
# Render an OBJ as 3 orthographic silhouettes (TOP/SIDE/FRONT) to a single PNG so we
# can SEE the model's orientation (nose/wings/tail) without flying it in-game.
# Coords are glTF (Y-up) as emitted by glb2obj. Usage: render_obj.py model.obj out.png
import sys
from PIL import Image, ImageDraw
obj, out = sys.argv[1], sys.argv[2]
V=[]; F=[]
with open(obj) as f:
    for line in f:
        if line.startswith('v '):
            p=line.split(); V.append((float(p[1]),float(p[2]),float(p[3])))
        elif line.startswith('f '):
            idx=[int(t.split('/')[0])-1 for t in line.split()[1:]]
            for i in range(1,len(idx)-1): F.append((idx[0],idx[i],idx[i+1]))
xs=[v[0] for v in V]; ys=[v[1] for v in V]; zs=[v[2] for v in V]
S=420; PAD=30
panels=[("TOP  (look down Y/up):  right=+X  up=+Z", 0,2, False, True),
        ("SIDE (look down Z):     right=+X  up=+Y", 0,1, False, False),
        ("FRONT(look down X/nose):right=+Z  up=+Y", 2,1, False, False)]
img=Image.new("RGB",(S*3+PAD*4, S+PAD*2+20),(255,255,255))
d=ImageDraw.Draw(img,"RGBA")
def rng(i): vals=[v[i] for v in V]; return min(vals),max(vals)
for pi,(title,ax_r,ax_u,fr,fu) in enumerate(panels):
    ox=PAD+pi*(S+PAD); oy=PAD+15
    rmin,rmax=rng(ax_r); umin,umax=rng(ax_u)
    span=max(rmax-rmin,umax-umin) or 1.0
    def proj(v):
        r=(v[ax_r]-rmin)/span; u=(v[ax_u]-umin)/span
        if fr: r=1-r
        x=ox+r*S; y=oy+(1-u)*S          # screen y is down
        return (x,y)
    d.rectangle([ox,oy,ox+S,oy+S],outline=(180,180,180))
    d.text((ox,oy-13),title,fill=(0,0,0))
    for (a,b,c) in F:
        try: d.polygon([proj(V[a]),proj(V[b]),proj(V[c])],fill=(70,110,170,18))
        except: pass
img.save(out)
print("wrote",out,"  verts",len(V),"tris",len(F))
print("  X[%.0f..%.0f] Y[%.0f..%.0f] Z[%.0f..%.0f]"%(min(xs),max(xs),min(ys),max(ys),min(zs),max(zs)))
