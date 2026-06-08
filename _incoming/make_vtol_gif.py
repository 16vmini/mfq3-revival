#!/usr/bin/env python3
# Render the F-35B VTOL transition to a GIF, offline from the MD3s:
#   special (doors/nozzle) 0->47, gear 0->47, lift-fan spins, then back.
# Usage: make_vtol_gif.py <f35dir> out.gif [--az 112 --el 14]
import sys,math,os,numpy as np
sys.path.insert(0,"C:/source/mfq3/_incoming")
from render_md3 import load_md3,gather,decode
from make_gear_gif import view_basis,frame_img

def main():
    a=sys.argv[1:]; az=112;el=14
    if '--az' in a: az=float(a[a.index('--az')+1])
    if '--el' in a: el=float(a[a.index('--el')+1])
    D=a[0]; out=a[1]
    body=load_md3(os.path.join(D,'f35.md3'))
    special=load_md3(os.path.join(D,'f35_special.md3'))
    gear=load_md3(os.path.join(D,'f35_gear.md3'))
    fan=load_md3(os.path.join(D,'f35_prop.md3'))
    tS=body['tags'].get('tag_special',[(np.zeros(3),np.eye(3))])[0]
    tG=body['tags'].get('tag_gear',[(np.zeros(3),np.eye(3))])[0]
    tP=body['tags'].get('tag_prop1',[(np.zeros(3),np.eye(3))])[0]
    right,up,fwd=view_basis(az,el); S=720
    light=np.array([0.3,0.45,0.82]);light/=np.linalg.norm(light)
    bv,bn,bt=gather(body,0)
    # fixed framing from body + fully-deployed gear/special
    gD,_,_=gather(gear,47,tG); sD,_,_=gather(special,47,tS)
    allp=np.vstack([bv,gD,sD])@np.column_stack([right,up])
    mn=allp.min(0);mx=allp.max(0);span=(mx-mn).max();cx=(mx+mn)/2;sc=S*0.82/span
    def fan_geom(angle):
        v,n=decode(fan['surfs'][0]['xyz'][0])
        ca,sa=math.cos(angle),math.sin(angle)
        Rz=np.array([[ca,-sa,0],[sa,ca,0],[0,0,1.0]])   # spin about local vertical
        v=v@Rz.T; n=n@Rz.T
        o,ax=tP; v=o+v@ax; n=n@ax
        return v,n,fan['surfs'][0]['tris']
    # sequence: deploy 0->1 (8), hold+spin (5), retract 1->0 (6)
    steps=[i/8 for i in range(9)] + [1.0]*5 + [i/6 for i in range(5,-1,-1)]
    frames=[];spin=0.0
    for t in steps:
        spin+=0.9
        fr=int(round(t*47))
        gv,gn,gt=gather(gear,47-fr,tG)   # gear animates OPPOSITE to the VTOL special part
        sv,sn,st=gather(special,fr,tS)
        fv,fn,ft=fan_geom(spin)
        geoms=[(bv,bn,bt,(150,155,165)),
               (sv,sn,st,(210,120,55)),     # doors/nozzle
               (gv,gn,gt,(120,140,170)),    # gear
               (fv,fn,ft,(70,200,225))]     # lift fan (cyan, so the spin reads)
        frames.append(frame_img(geoms,right,up,fwd,cx,sc,S,light))
    frames[0].save(out,save_all=True,append_images=frames[1:],duration=110,loop=0,optimize=True)
    print("wrote",out,len(frames),"frames")

main()
