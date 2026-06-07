#!/usr/bin/env python3
# Split gear geometry out of a merged OBJ by tagging triangles whose centroid falls
# inside per-leg 3D boxes (in centered Z-up coords) into new groups (GEAR_*), leaving
# everything else as BODY. Preserves v/vt/vn and per-face usemtl so obj2md3 -PerMaterial
# + -GearLegs can extract & texture the gear. Usage: preprocess_gear.py in.obj out.obj
import sys,numpy as np
inp,out=sys.argv[1],sys.argv[2]
# boxes in CENTERED Z-up coords (X=fwd Y=left Z=up): name -> (xmin,xmax,ymin,ymax,zmin,zmax)
BOXES={
 'GEAR_NOSE':(5.0,6.5,-0.6,0.6,-2.72,-1.25),
 'GEAR_L'   :(-5.2,-4.1, 1.5, 2.6,-2.72,-1.6),
 'GEAR_R'   :(-5.2,-4.1,-2.6,-1.5,-2.72,-1.6),
}
Vx=[];Vt=[];Vn=[];faces=[]   # faces: (groupOriginal, mtl, "v/t/n v/t/n v/t/n" tokens)
curm='default'
for l in open(inp):
    if l.startswith('v '): Vx.append(l.rstrip('\n'))
    elif l.startswith('vt '): Vt.append(l.rstrip('\n'))
    elif l.startswith('vn '): Vn.append(l.rstrip('\n'))
    elif l.startswith('usemtl '): curm=l.split(None,1)[1].strip()
    elif l.startswith('f '):
        toks=l.split()[1:]; faces.append((curm,toks))
# positions for centroid test
P=np.array([[float(x) for x in s.split()[1:4]] for s in Vx])
Q=np.column_stack([P[:,0],-P[:,2],P[:,1]]); ctr=(Q.max(0)+Q.min(0))/2; Q=Q-ctr
def vidx(tok):
    i=int(tok.split('/')[0]); return i-1 if i>0 else len(P)+i
def leg_of(cen):
    for nm,(x0,x1,y0,y1,z0,z1) in BOXES.items():
        if x0<=cen[0]<=x1 and y0<=cen[1]<=y1 and z0<=cen[2]<=z1: return nm
    return 'BODY'
# bucket faces by (group, mtl)
buckets={}
counts={}
for mtl,toks in faces:
    cen=Q[[vidx(t) for t in toks]].mean(0)
    g=leg_of(cen)
    buckets.setdefault((g,mtl),[]).append('f '+' '.join(toks))
    counts[g]=counts.get(g,0)+1
with open(out,'w') as f:
    f.write('\n'.join(Vx)+'\n'); f.write('\n'.join(Vt)+'\n'); f.write('\n'.join(Vn)+'\n')
    # gear groups first (stable order), then body
    order=sorted(buckets, key=lambda k:(k[0]=='BODY',k[0],k[1]))
    lastg=None
    for (g,mtl) in order:
        if g!=lastg: f.write('g '+g+'\n'); lastg=g
        f.write('usemtl '+mtl+'\n'); f.write('\n'.join(buckets[(g,mtl)])+'\n')
print('faces by group:',counts)
