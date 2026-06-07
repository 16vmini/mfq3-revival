#!/usr/bin/env python3
# Offline MD3 viewer: load a Quake3 MD3 (+ optionally attach a second MD3 at a tag
# at a chosen frame), render a shaded 3/4 + side view to PNG. Lets us verify models
# and the gear retract animation WITHOUT launching the game.
#   render_md3.py body.md3 out.png [gear.md3 tag frame] [--az 135 --el 22]
import sys,struct,math,numpy as np
from PIL import Image,ImageDraw

def load_md3(path):
    d=open(path,'rb').read()
    ident,ver=struct.unpack_from('<4si',d,0)
    name=d[8:72].split(b'\0')[0].decode('latin1')
    flags,nFrames,nTags,nSurf,nSkin,ofsFrames,ofsTags,ofsSurf,ofsEnd=struct.unpack_from('<9i',d,72)
    # tags
    tags={}  # name -> list per frame of (origin(3), axis(3x3))
    tp=ofsTags
    for fr in range(nFrames):
        for t in range(nTags):
            base=tp+(fr*nTags+t)*112
            tn=d[base:base+64].split(b'\0')[0].decode('latin1')
            vals=struct.unpack_from('<12f',d,base+64)
            origin=np.array(vals[0:3]); axis=np.array(vals[3:12]).reshape(3,3)
            tags.setdefault(tn,[None]*nFrames)[fr]=(origin,axis)
    # surfaces
    surfs=[]; so=ofsSurf
    for s in range(nSurf):
        sid=d[so:so+4]; sname=d[so+4:so+68].split(b'\0')[0].decode('latin1')
        sflags,sFrames,sShaders,sVerts,sTris,oTris,oShaders,oST,oXyz,sEnd=struct.unpack_from('<10i',d,so+68)
        tris=np.frombuffer(d,dtype='<i4',count=sTris*3,offset=so+oTris).reshape(sTris,3)
        xyz=np.frombuffer(d,dtype='<i2',count=sFrames*sVerts*4,offset=so+oXyz).reshape(sFrames,sVerts,4)
        surfs.append({'name':sname,'tris':tris,'xyz':xyz,'nFrames':sFrames,'nVerts':sVerts})
        so+=sEnd
    return {'nFrames':nFrames,'tags':tags,'surfs':surfs}

def decode(xyz_frame):
    v=xyz_frame[:,:3].astype(np.float64)/64.0
    n=xyz_frame[:,3].astype(np.uint16)
    lat=((n>>8)&255)*(math.pi/128.0); lng=(n&255)*(math.pi/128.0)
    nx=np.cos(lat)*np.sin(lng); ny=np.sin(lat)*np.sin(lng); nz=np.cos(lng)
    return v,np.column_stack([nx,ny,nz])

def gather(md3,frame,xform=None):
    """return (verts Nx3, normals Nx3, tris Mx3 global) for a frame, optional 4x4-ish (origin,axis)."""
    allv=[];alln=[];allt=[];base=0
    for s in md3['surfs']:
        f=min(frame,s['nFrames']-1)
        v,n=decode(s['xyz'][f])
        if xform is not None:
            origin,axis=xform
            v=origin+v@axis            # world = origin + v * axis (rows = basis)
            n=n@axis
        allv.append(v);alln.append(n);allt.append(s['tris']+base);base+=len(v)
    return np.vstack(allv),np.vstack(alln),np.vstack(allt)

def render(geoms,out,az=135,el=22,S=900):
    # geoms = list of (verts,normals,tris,basecolor)
    A=math.radians(az);E=math.radians(el)
    # view basis: rotate az about Z(up), el about side
    ca,sa,ce,se=math.cos(A),math.sin(A),math.cos(E),math.sin(E)
    # world axes: X fwd, Y left, Z up. camera dir:
    right=np.array([ -sa, ca, 0])
    up0=np.array([0,0,1.0])
    fwd=np.array([ca*ce, sa*ce, se]); fwd=fwd/np.linalg.norm(fwd)
    right=np.cross(np.array([0,0,1.0]),fwd); right/=np.linalg.norm(right)
    up=np.cross(fwd,right)
    light=np.array([0.3,0.4,0.85]); light/=np.linalg.norm(light)
    allv=np.vstack([g[0] for g in geoms])
    pr_all=np.column_stack([allv@right,allv@up,allv@fwd])
    mn=pr_all[:,:2].min(0);mx=pr_all[:,:2].max(0)
    span=(mx-mn).max();cx=(mx+mn)/2
    sc=S*0.86/span
    img=Image.new('RGB',(S,S),(245,247,250));dr=ImageDraw.Draw(img)
    tris=[]
    for (v,n,t,col) in geoms:
        p=np.column_stack([v@right,v@up,v@fwd])
        sx=(p[:,0]-cx[0])*sc+S/2; sy=S/2-(p[:,1]-cx[1])*sc; depth=p[:,2]
        wn=n@np.column_stack([right,up,fwd])   # normals in view space (approx)
        for tri in t:
            a,b,c=tri
            z=(depth[a]+depth[b]+depth[c])/3
            fn=n[a]+n[b]+n[c]; fn=fn/(np.linalg.norm(fn)+1e-9)
            sh=0.35+0.65*max(0,fn@light)
            color=tuple(int(min(255,cc*sh)) for cc in col)
            tris.append((z,[(sx[a],sy[a]),(sx[b],sy[b]),(sx[c],sy[c])],color))
    tris.sort(key=lambda r:-r[0])     # painter's: far first
    for z,pts,color in tris:
        dr.polygon(pts,fill=color)
    img.save(out)
    print('wrote',out)

if __name__=='__main__':
    args=sys.argv[1:]
    az=135;el=22
    if '--az' in args: az=float(args[args.index('--az')+1])
    if '--el' in args: el=float(args[args.index('--el')+1])
    pos=[a for a in args if not a.startswith('--')]
    # filter out the az/el values
    skip=set()
    for fl in ('--az','--el'):
        if fl in args: skip.add(args[args.index(fl)+1])
    pos=[a for a in pos if a not in skip]
    body=load_md3(pos[0]); out=pos[1]
    geoms=[]
    bv,bn,bt=gather(body,0)
    geoms.append((bv,bn,bt,(150,155,165)))
    if len(pos)>=5:
        gear=load_md3(pos[2]); tagname=pos[3]; frame=int(pos[4])
        xf=body['tags'].get(tagname,[ (np.zeros(3),np.eye(3)) ])[0]
        gv,gn,gt=gather(gear,frame,xf)
        geoms.append((gv,gn,gt,(200,120,60)))
    render(geoms,out,az,el)
