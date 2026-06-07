#!/usr/bin/env python3
# Render an OBJ with groups color-coded by name prefix, so we can SEE which
# primitive groups are engines vs wheels vs struts. SIDE / BOTTOM / REAR views.
import sys,re,math
from PIL import Image, ImageDraw
obj,out=sys.argv[1],sys.argv[2]
V=[]; faces=[]; cur='?'
for l in open(obj):
    if l.startswith('v '): p=l.split(); V.append((float(p[1]),float(p[2]),float(p[3])))
    elif l.startswith('g '): cur=l[2:].strip()
    elif l.startswith('f '):
        idx=[int(t.split('/')[0])-1 for t in l.split()[1:]]
        for i in range(1,len(idx)-1): faces.append((cur,idx[0],idx[i],idx[i+1]))
# glTF Y-up -> plane coords (yaw 143 only, enough to orient)
yaw=143.0; c,s=math.cos(math.radians(yaw)),math.sin(math.radians(yaw))
P=[]
for x,y,z in V:
    bx,by,bz=x,-z,y
    P.append((bx*c-by*s, bx*s+by*c, bz))   # X=fwd Y=left Z=up
def col(g):
    if re.match('Circle',g):     return (220,40,40,90)    # red   = wheels?
    if re.match('Cylinder',g):   return (40,90,220,90)    # blue  = hubs/fans?
    if re.match('Ellipse',g):    return (30,170,30,110)   # green = nacelle/pylon?
    if re.match('ChamferBox',g): return (210,170,20,90)   # yellow= struts/pylons?
    if re.match('Sphere',g):     return (200,30,200,110)  # magenta=caps?
    return (150,150,150,30)                               # gray  = airframe
S=460;PAD=30
views=[("SIDE +X=right Z=up",0,2,False),("BOTTOM +X=right Y=up",0,1,False),("REAR +Y=right Z=up",1,2,False)]
xs=[p[0] for p in P];ys=[p[1] for p in P];zs=[p[2] for p in P]
rg=lambda i:(min(p[i] for p in P),max(p[i] for p in P))
img=Image.new("RGB",(S*3+PAD*4,S+PAD*2+20),(255,255,255));d=ImageDraw.Draw(img,"RGBA")
for vi,(title,ar,au,fr) in enumerate(views):
    ox=PAD+vi*(S+PAD);oy=PAD+15
    rmn,rmx=rg(ar);umn,umx=rg(au);span=max(rmx-rmn,umx-umn) or 1
    def pr(p):
        r=(p[ar]-rmn)/span;u=(p[au]-umn)/span
        return (ox+r*S,oy+(1-u)*S)
    d.rectangle([ox,oy,ox+S,oy+S],outline=(180,180,180));d.text((ox,oy-13),title,fill=(0,0,0))
    for g,a,b,cc in faces:
        try: d.polygon([pr(P[a]),pr(P[b]),pr(P[cc])],fill=col(g))
        except: pass
d.text((PAD,S+PAD+18),"red=Circle blue=Cylinder green=Ellipse yellow=ChamferBox magenta=Sphere gray=airframe",fill=(0,0,0))
img.save(out);print("wrote",out)
