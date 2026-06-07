#!/usr/bin/env python3
# Analyze an OBJ's orientation and recommend an EXACT obj2md3 -Yaw.
# Uses PCA on the horizontal (glTF X-Z) plane to find the fuselage direction (works
# even when the model is rotated by a non-90 angle), then finds the nose end (the end
# WITHOUT the tall tail fin) and computes the yaw that points the nose to Q +X (forward).
import sys, math
path=sys.argv[1]
X=[];Y=[];Z=[]
with open(path) as f:
    for line in f:
        if line.startswith('v '):
            p=line.split(); X.append(float(p[1])); Y.append(float(p[2])); Z.append(float(p[3]))
n=len(X)
ext={'X':max(X)-min(X),'Y':max(Y)-min(Y),'Z':max(Z)-min(Z)}
print("verts:",n,"  extents",{k:round(v,1) for k,v in ext.items()})
up=min(ext,key=ext.get)
print("UP axis (thinnest) =",up,"(expected Y for glTF; if not, model isn't Y-up)")
# PCA on horizontal plane (glTF X,Z)
mx=sum(X)/n; mz=sum(Z)/n
sxx=sum((x-mx)**2 for x in X)/n
szz=sum((z-mz)**2 for z in Z)/n
sxz=sum((X[i]-mx)*(Z[i]-mz) for i in range(n))/n
t=sxx+szz; d=sxx*szz-sxz*sxz
lam=(t+math.sqrt(max(t*t-4*d,0)))/2
# principal eigenvector (fuselage direction) in (X,Z)
if abs(sxz)>1e-6: dx,dz=lam-szz,sxz
else: dx,dz=(1.0,0.0) if sxx>=szz else (0.0,1.0)
L=math.hypot(dx,dz); dx,dz=dx/L,dz/L
ang=math.degrees(math.atan2(dz,dx))
print("FUSELAGE direction in glTF X-Z = (%.3f, %.3f)  (%.1f deg from +X)"%(dx,dz,ang))
# nose vs tail: project onto fuselage dir, compare tail-fin height at each end
proj=[X[i]*dx+Z[i]*dz for i in range(n)]
pmin,pmax=min(proj),max(proj); thr=(pmax-pmin)*0.15
plus =[Y[i] for i in range(n) if proj[i]>pmax-thr]
minus=[Y[i] for i in range(n) if proj[i]<pmin+thr]
hplus=(max(plus)-min(plus)) if plus else 0
hminus=(max(minus)-min(minus)) if minus else 0
print("  +end fin-height=%.0f   -end fin-height=%.0f  (taller end = TAIL)"%(hplus,hminus))
# nose direction = end with the SHORTER fin
if hplus>hminus: ndx,ndz=-dx,-dz; print("  tail at +end -> nose points to -dir")
else:            ndx,ndz= dx, dz; print("  tail at -end -> nose points to +dir")
# obj2md3 maps (bx,by)=(gltfX,-gltfZ) then yaw: gx=bx*c-by*s, gy=bx*s+by*c.
# want nose -> Q +X (gx=+, gy=0). nose in (bx,by):
nbx,nby=ndx,-ndz
phi=math.atan2(nby,nbx)
yaw=(-math.degrees(phi))%360
print("\n=> RECOMMENDED  obj2md3.ps1 -Yaw %.1f  (FlipV:$false for glTF)"%yaw)
print("   (rounded: -Yaw %d)"%round(yaw))
