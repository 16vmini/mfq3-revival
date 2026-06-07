#!/usr/bin/env python3
# Render a gear retract/extend animation to a GIF using the offline MD3 reader.
# Fixed framing across frames so it doesn't jitter. Usage:
#   make_gear_gif.py body.md3 gear.md3 tag out.gif [--az 120 --el 10]
import sys,math,numpy as np
from PIL import Image,ImageDraw
sys.path.insert(0,"C:/source/mfq3/_incoming")
from render_md3 import load_md3,gather

def view_basis(az,el):
    A=math.radians(az);E=math.radians(el)
    fwd=np.array([math.cos(A)*math.cos(E),math.sin(A)*math.cos(E),math.sin(E)]);fwd/=np.linalg.norm(fwd)
    right=np.cross(np.array([0,0,1.0]),fwd);right/=np.linalg.norm(right)
    up=np.cross(fwd,right)
    return right,up,fwd

def frame_img(geoms,right,up,fwd,cx,sc,S,light):
    img=Image.new('RGB',(S,S),(245,247,250));dr=ImageDraw.Draw(img);tris=[]
    for (v,n,t,col) in geoms:
        p=np.column_stack([v@right,v@up,v@fwd])
        sx=(p[:,0]-cx[0])*sc+S/2; sy=S/2-(p[:,1]-cx[1])*sc; depth=p[:,2]
        for tri in t:
            a,b,c=tri; z=(depth[a]+depth[b]+depth[c])/3
            fn=n[a]+n[b]+n[c];fn=fn/(np.linalg.norm(fn)+1e-9)
            sh=0.35+0.65*max(0,fn@light)
            tris.append((z,[(sx[a],sy[a]),(sx[b],sy[b]),(sx[c],sy[c])],tuple(int(min(255,cc*sh)) for cc in col)))
    tris.sort(key=lambda r:-r[0])
    for z,pts,color in tris: dr.polygon(pts,fill=color)
    return img

def main():
    a=sys.argv[1:]; az=120;el=10
    if '--az' in a: az=float(a[a.index('--az')+1])
    if '--el' in a: el=float(a[a.index('--el')+1])
    body,gear,tag,out=a[0],a[1],a[2],a[3]
    B=load_md3(body);G=load_md3(gear)
    xf=B['tags'].get(tag,[(np.zeros(3),np.eye(3))])[0]
    right,up,fwd=view_basis(az,el)
    S=720; light=np.array([0.3,0.4,0.85]);light/=np.linalg.norm(light)
    bv,bn,bt=gather(B,0)
    gvD,_,_=gather(G,47,xf)               # gear down = max extent -> fix bounds on this
    allp=np.vstack([bv,gvD])@np.column_stack([right,up])
    mn=allp.min(0);mx=allp.max(0);span=(mx-mn).max();cx=(mx+mn)/2; sc=S*0.84/span
    seq=[47,41,35,29,23,17,11,5,0]        # down -> up
    frames=[]
    for fr in seq+seq[::-1][1:-1]:        # ping-pong
        gv,gn,gt=gather(G,fr,xf)
        geoms=[(bv,bn,bt,(150,155,165)),(gv,gn,gt,(205,120,55))]
        frames.append(frame_img(geoms,right,up,fwd,cx,sc,S,light))
    frames[0].save(out,save_all=True,append_images=frames[1:],duration=110,loop=0,optimize=True)
    print("wrote",out,len(frames),"frames")

main()
