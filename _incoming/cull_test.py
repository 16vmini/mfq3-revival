#!/usr/bin/env python3
# Render an MD3 with BACK-FACE CULLING (like the game) to detect inverted winding.
# Keeps only triangles whose screen-space winding is front-facing. If a model that
# looks fine without culling goes holey/empty WITH culling, its winding is reversed.
import sys,math,numpy as np
sys.path.insert(0,"C:/source/mfq3/_incoming")
from render_md3 import load_md3,gather
from PIL import Image,ImageDraw
path,out=sys.argv[1],sys.argv[2]
sign=float(sys.argv[3]) if len(sys.argv)>3 else 1.0
az,el=130,18
A,E=math.radians(az),math.radians(el)
fwd=np.array([math.cos(A)*math.cos(E),math.sin(A)*math.cos(E),math.sin(E)]);fwd/=np.linalg.norm(fwd)
right=np.cross([0,0,1.0],fwd);right/=np.linalg.norm(right);up=np.cross(fwd,right)
m=load_md3(path); v,n,t=gather(m,0)
p=np.column_stack([v@right,v@up,v@fwd])
mn=p[:,:2].min(0);mx=p[:,:2].max(0);span=(mx-mn).max();c=(mx+mn)/2;S=600;sc=S*0.85/span
sx=(p[:,0]-c[0])*sc+S/2; sy=S/2-(p[:,1]-c[1])*sc; dep=p[:,2]
img=Image.new('RGB',(S,S),(245,247,250));d=ImageDraw.Draw(img)
tris=[]; kept=0; tot=0
for a,b,cc in t:
    tot+=1
    area=(sx[b]-sx[a])*(sy[cc]-sy[a])-(sx[cc]-sx[a])*(sy[b]-sy[a])  # screen signed area
    if area*sign<=0: continue   # back-facing -> cull
    kept+=1
    z=(dep[a]+dep[b]+dep[cc])/3
    tris.append((z,[(sx[a],sy[a]),(sx[b],sy[b]),(sx[cc],sy[cc])]))
tris.sort(key=lambda r:-r[0])
for z,pts in tris: d.polygon(pts,fill=(110,120,135))
img.save(out)
print("%s: kept %d / %d front-facing tris (sign %+d)"%(out,kept,tot,sign))
