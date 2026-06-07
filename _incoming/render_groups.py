#!/usr/bin/env python3
# Render an OBJ with each GROUP in a distinct color (SIDE/BOTTOM/REAR) + legend,
# so we can see how a model is split (find gear, canopy, nacelles, etc).
import sys,math
from PIL import Image, ImageDraw
obj,out=sys.argv[1],sys.argv[2]
yaw=float(sys.argv[3]) if len(sys.argv)>3 else 0.0
V=[]; faces=[]; cur='?'; order=[]
for l in open(obj):
    if l.startswith('v '): p=l.split(); V.append((float(p[1]),float(p[2]),float(p[3])))
    elif l.startswith('g '):
        cur=l[2:].strip()
        if cur not in order: order.append(cur)
    elif l.startswith('f '):
        idx=[int(t.split('/')[0])-1 for t in l.split()[1:]]
        for i in range(1,len(idx)-1): faces.append((cur,idx[0],idx[i],idx[i+1]))
c,s=math.cos(math.radians(yaw)),math.sin(math.radians(yaw))
P=[]
for x,y,z in V:
    bx,by,bz=x,-z,y
    P.append((bx*c-by*s, bx*s+by*c, bz))     # X=fwd Y=left Z=up
PAL=[(220,40,40),(40,120,220),(30,170,30),(230,160,20),(190,40,200),(20,180,180),
     (240,110,30),(120,90,200),(150,150,40),(220,60,130),(60,160,90),(110,110,110)]
cmap={g:PAL[i%len(PAL)] for i,g in enumerate(order)}
S=440;PAD=24
views=[("SIDE +X=right Z=up",0,2),("BOTTOM +X=right Y=up",0,1),("REAR +Y=right Z=up",1,2)]
rg=lambda i:(min(p[i] for p in P),max(p[i] for p in P))
img=Image.new("RGB",(S*3+PAD*4,S+PAD*2+150),(255,255,255));d=ImageDraw.Draw(img,"RGBA")
for vi,(title,ar,au) in enumerate(views):
    ox=PAD+vi*(S+PAD);oy=PAD+12
    rmn,rmx=rg(ar);umn,umx=rg(au);span=max(rmx-rmn,umx-umn) or 1
    def pr(p):
        r=(p[ar]-rmn)/span;u=(p[au]-umn)/span
        return (ox+r*S,oy+(1-u)*S)
    d.rectangle([ox,oy,ox+S,oy+S],outline=(170,170,170));d.text((ox,oy-11),title,fill=(0,0,0))
    for g,a,b,cc in faces:
        col=cmap[g]+(70,)
        try: d.polygon([pr(P[a]),pr(P[b]),pr(P[cc])],fill=col)
        except: pass
# legend
ly=S+PAD+20
for i,g in enumerate(order):
    yy=ly+(i//3)*22; xx=PAD+(i%3)*460
    d.rectangle([xx,yy,xx+16,yy+16],fill=cmap[g])
    d.text((xx+22,yy+3),g,fill=(0,0,0))
img.save(out);print("wrote",out,"groups:",len(order))
