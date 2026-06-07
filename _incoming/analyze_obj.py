#!/usr/bin/env python3
# Analyze an OBJ's orientation: bbox extents per axis, which axis is "up" (thinnest),
# which is the fuselage (nose<->tail, detected by the tall tail-fin at one end), and
# which way the nose points. Helps pick the obj2md3 yaw / axis remap for an aircraft.
import sys
path=sys.argv[1]
xs=[];ys=[];zs=[]
with open(path) as f:
    for line in f:
        if line.startswith('v '):
            p=line.split()
            xs.append(float(p[1])); ys.append(float(p[2])); zs.append(float(p[3]))
n=len(xs)
ax={'X':xs,'Y':ys,'Z':zs}
mn={k:min(v) for k,v in ax.items()}; mx={k:max(v) for k,v in ax.items()}
ext={k:mx[k]-mn[k] for k in ax}
print("verts:",n)
for k in 'XYZ': print("  %s extent %8.2f  [%.1f .. %.1f]"%(k,ext[k],mn[k],mx[k]))
up=min(ext,key=ext.get)                       # thinnest axis = up (height)
horiz=[k for k in 'XYZ' if k!=up]
print("UP axis (thinnest) =",up)
# For each horizontal axis, measure tail-fin asymmetry: split at midpoint, compare
# the UP-extent of each half. The fuselage axis has one tall end (vertical stab).
def up_extent(idxs):
    if not idxs: return 0.0
    vals=[ax[up][i] for i in idxs]; return max(vals)-min(vals)
best=None
for H in horiz:
    mid=(mn[H]+mx[H])/2
    lo=[i for i in range(n) if ax[H][i]<mid]; hi=[i for i in range(n) if ax[H][i]>=mid]
    ulo=up_extent(lo); uhi=up_extent(hi)
    asym=abs(uhi-ulo)/(max(uhi,ulo) or 1)
    tall='HIGH(+)' if uhi>ulo else 'LOW(-)'
    print("  axis %s: split up-extent  low=%.1f high=%.1f  asym=%.2f  (tail/fin end=%s)"%(H,ulo,uhi,asym,tall))
    if best is None or asym>best[1]: best=(H,asym,'+' if uhi>ulo else '-')
fus,asym,tailend=best
wing=[k for k in horiz if k!=fus][0]
noseend='-' if tailend=='+' else '+'
print("=> FUSELAGE axis =",fus,"| WINGSPAN axis =",wing,"| UP =",up)
print("=> tail (tall fin) at %s%s, so NOSE points %s%s"%(fus,tailend,fus,noseend))
print("   (obj is glTF Y-up; obj2md3 maps glTF X->fwd, glTF Z->left/right, glTF Y->up.")
print("    Want FUSELAGE->X with nose=+X, WINGSPAN->Z, UP->Y for a clean -Yaw 0 import.)")
