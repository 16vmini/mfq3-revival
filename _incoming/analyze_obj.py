#!/usr/bin/env python3
# Analyze an OBJ's orientation and recommend exact obj2md3 -Yaw -Pitch -Roll.
# glTF (Y-up) input. Pipeline mirrored from obj2md3: Y-up->Z-up, then yaw(Z),
# pitch(Y), roll(X). Uses 3D PCA: largest principal axis = fuselage, middle =
# wingspan, smallest = up. Reports the tilts that remain so we can null them out.
#   usage: analyze_obj.py model.obj [yaw]   (omit yaw to auto-solve heading)
import sys, math, numpy as np
path=sys.argv[1]
V=[]
with open(path) as f:
    for line in f:
        if line.startswith('v '):
            p=line.split(); V.append((float(p[1]),float(p[2]),float(p[3])))
V=np.array(V); n=len(V)
ext=V.max(0)-V.min(0)
print("verts:",n," glTF extents X%.0f Y%.0f Z%.0f"%(ext[0],ext[1],ext[2]))

def pca(P):
    w,v=np.linalg.eigh(np.cov((P-P.mean(0)).T)); return w,v   # ascending eigenvalues

# --- 1. heading (yaw): horizontal PCA on glTF X-Z, nose = end without tall fin ---
if len(sys.argv)>2:
    yaw=float(sys.argv[2]); print("using given -Yaw",yaw)
else:
    XZ=V[:,[0,2]]; w,v=pca(XZ); d=v[:,1]                      # principal horizontal dir
    proj=XZ@d; thr=(proj.max()-proj.min())*0.15
    hi=V[proj>proj.max()-thr,1]; lo=V[proj<proj.min()+thr,1]
    finhi=hi.max()-hi.min(); finlo=lo.max()-lo.min()
    nose = -d if finhi>finlo else d                            # nose = shorter-fin end
    nbx,nby = nose[0], -nose[1]                                # glTF->(bx,by) for obj2md3
    yaw=(-math.degrees(math.atan2(nby,nbx)))%360
    print("auto heading: fuselage %.1fdeg, tail-fin hi=%.0f lo=%.0f -> -Yaw %.1f"%(
          math.degrees(math.atan2(d[1],d[0])),finhi,finlo,yaw))

# --- 2. apply Y-up->Z-up + yaw, exactly like the converter ---
B=np.column_stack([V[:,0], -V[:,2], V[:,1]])
c,s=math.cos(math.radians(yaw)),math.sin(math.radians(yaw))
G=np.column_stack([B[:,0]*c-B[:,1]*s, B[:,0]*s+B[:,1]*c, B[:,2]])  # X=fwd Y=left Z=up

# --- 3. pitch: tilt of fuselage axis in the fwd-up (X-Z) plane ---
w,v=pca(G); f=v[:,2].copy()
if f[0]<0: f=-f
pitch=math.degrees(math.atan2(f[2],f[0]))
P=math.radians(pitch); cp,sp=math.cos(P),math.sin(P)
G2=np.column_stack([G[:,0]*cp+G[:,2]*sp, G[:,1], -G[:,0]*sp+G[:,2]*cp])  # level pitch

# --- 4. roll: tilt of wingspan axis in the left-up (Y-Z) plane, after pitch ---
w2,v2=pca(G2); wg=v2[:,1].copy()
if wg[1]<0: wg=-wg
roll=math.degrees(math.atan2(wg[2],wg[1]))
print("residual fuselage pitch = %+.2f deg, wing-line roll = %+.2f deg"%(pitch,roll))
print("\n=> obj2md3.ps1 -Yaw %.1f -Pitch %.2f -Roll %.2f  -FlipV:$false"%(yaw,pitch,roll))
print("   (rounded: -Yaw %d -Pitch %.1f -Roll %.1f)"%(round(yaw),pitch,roll))
