#!/usr/bin/env python
# glTF (.glb) -> Quake3 .md3 converter for MFQ3 vehicles.
# Bakes the glTF node-graph transforms, combines all meshes into one md3 surface,
# reorients Y-up -> Z-up with the long axis along +X (bow), centres on origin,
# scales to a target length (kept under md3's +/-511u vertex range), and writes a
# single-frame md3 plus a .tga skin from the model's albedo.
#
# Usage: python glb_to_md3.py <in.glb> <out.md3> <shaderName> <targetLen> [albedo.png] [--flip]
import sys, struct, json, math

def load_glb(path):
    d = open(path, "rb").read()
    magic, ver, _ = struct.unpack("<4sII", d[:12])
    assert magic == b"glTF", "not a glb"
    off = 12; js = None; bin_ = b""
    while off < len(d):
        clen, ctype = struct.unpack("<I4s", d[off:off+8]); off += 8
        chunk = d[off:off+clen]; off += clen
        if ctype == b"JSON": js = json.loads(chunk)
        elif ctype == b"BIN\0": bin_ = chunk
    return js, bin_

CTYPE = {5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}
NCOMP = {"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4,"MAT4":16}

def read_accessor(js, bin_, idx):
    a = js["accessors"][idx]
    bv = js["bufferViews"][a["bufferView"]]
    comp, csz = CTYPE[a["componentType"]]; n = NCOMP[a["type"]]
    base = bv.get("byteOffset",0) + a.get("byteOffset",0)
    stride = bv.get("byteStride", csz*n)
    out = []
    for i in range(a["count"]):
        s = base + i*stride
        out.append(struct.unpack_from("<"+comp*n, bin_, s))
    return out

# ---- 4x4 column-major matrix helpers (glTF convention) ----
def mat_identity(): return [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
def mat_mul(a,b):
    r=[0.0]*16
    for c in range(4):
        for row in range(4):
            r[c*4+row]=sum(a[k*4+row]*b[c*4+k] for k in range(4))
    return r
def mat_from_trs(t,q,s):
    if t is None: t=[0,0,0]
    if q is None: q=[0,0,0,1]
    if s is None: s=[1,1,1]
    x,y,z,w=q
    R=[1-2*(y*y+z*z), 2*(x*y+z*w),   2*(x*z-y*w),   0,
       2*(x*y-z*w),   1-2*(x*x+z*z), 2*(y*z+x*w),   0,
       2*(x*z+y*w),   2*(y*z-x*w),   1-2*(x*x+y*y), 0,
       0,0,0,1]
    S=[s[0],0,0,0, 0,s[1],0,0, 0,0,s[2],0, 0,0,0,1]
    M=mat_mul(R,S)
    M[12],M[13],M[14]=t[0],t[1],t[2]
    return M
def node_local(n):
    if "matrix" in n: return list(n["matrix"])
    return mat_from_trs(n.get("translation"),n.get("rotation"),n.get("scale"))
def xform_pt(M,p):
    x,y,z=p
    return (M[0]*x+M[4]*y+M[8]*z+M[12], M[1]*x+M[5]*y+M[9]*z+M[13], M[2]*x+M[6]*y+M[10]*z+M[14])
def xform_nrm(M,p):
    x,y,z=p  # ignore translation; approximate (no inverse-transpose)
    return (M[0]*x+M[4]*y+M[8]*z, M[1]*x+M[5]*y+M[9]*z, M[2]*x+M[6]*y+M[10]*z)

def pack_normal(n):
    x,y,z=n
    L=math.sqrt(x*x+y*y+z*z) or 1.0
    x,y,z=x/L,y/L,z/L
    if x==0 and y==0:
        return 0 if z>=0 else (128<<8)
    lng=int(round(math.atan2(y,x)*(255.0/(2*math.pi))))&0xff
    lat=int(round(math.acos(max(-1.0,min(1.0,z)))*(255.0/(2*math.pi))))&0xff
    return (lat<<8)|lng

def main():
    glb=sys.argv[1]; out=sys.argv[2]; shader=sys.argv[3]; target=float(sys.argv[4])
    albedo=sys.argv[5] if len(sys.argv)>5 and not sys.argv[5].startswith("--") else None
    flip = "--flip" in sys.argv
    flipV = "--flipv" in sys.argv or True  # glTF UV top-left -> md3, flip V by default
    js,bin_=load_glb(glb)

    # world matrix per node (walk scene graph)
    world={}
    def walk(ni,parent):
        n=js["nodes"][ni]
        M=mat_mul(parent,node_local(n))
        world[ni]=M
        for c in n.get("children",[]): walk(c,M)
    roots=js["scenes"][js.get("scene",0)]["nodes"]
    for r in roots: walk(r,mat_identity())

    verts=[]; norms=[]; uvs=[]; tris=[]
    for ni,n in enumerate(js["nodes"]):
        if "mesh" not in n: continue
        M=world[ni]
        for prim in js["meshes"][n["mesh"]]["primitives"]:
            at=prim["attributes"]
            P=read_accessor(js,bin_,at["POSITION"])
            N=read_accessor(js,bin_,at["NORMAL"]) if "NORMAL" in at else [(0,0,1)]*len(P)
            T=read_accessor(js,bin_,at["TEXCOORD_0"]) if "TEXCOORD_0" in at else [(0,0)]*len(P)
            idx=read_accessor(js,bin_,prim["indices"]) if "indices" in prim else [(i,) for i in range(len(P))]
            base=len(verts)
            for p,nn,t in zip(P,N,T):
                verts.append(xform_pt(M,p)); norms.append(xform_nrm(M,nn)); uvs.append((t[0],t[1]))
            flat=[i[0] for i in idx]
            for k in range(0,len(flat),3):
                tris.append((base+flat[k],base+flat[k+1],base+flat[k+2]))

    # reorient glTF (Y-up) -> Q3 (Z-up):  q=(x,-z,y)
    verts=[(x,-z,y) for (x,y,z) in verts]
    norms=[(x,-z,y) for (x,y,z) in norms]

    # bbox
    def bbox(vs):
        mn=[min(v[i] for v in vs) for i in range(3)]; mx=[max(v[i] for v in vs) for i in range(3)]
        return mn,mx
    mn,mx=bbox(verts); dim=[mx[i]-mn[i] for i in range(3)]
    # longest horizontal axis (X or Y) -> rotate so it's X
    if dim[1]>dim[0]:
        verts=[(y,-x,z) for (x,y,z) in verts]; norms=[(y,-x,z) for (x,y,z) in norms]
        mn,mx=bbox(verts); dim=[mx[i]-mn[i] for i in range(3)]
    if flip:  # bow points -X -> spin 180 about Z
        verts=[(-x,-y,z) for (x,y,z) in verts]; norms=[(-x,-y,z) for (x,y,z) in norms]
        mn,mx=bbox(verts); dim=[mx[i]-mn[i] for i in range(3)]
    # centre on origin
    ctr=[(mn[i]+mx[i])/2 for i in range(3)]
    verts=[(x-ctr[0],y-ctr[1],z-ctr[2]) for (x,y,z) in verts]
    # scale to target length (X), keep < 511u
    scale=target/dim[0]
    verts=[(x*scale,y*scale,z*scale) for (x,y,z) in verts]
    mn,mx=bbox(verts)
    print(f"verts={len(verts)} tris={len(tris)} dims(after)={[round(mx[i]-mn[i],1) for i in range(3)]} scale={scale:.4f}")
    mxabs=max(abs(c) for v in verts for c in v)
    print(f"max |coord| = {mxabs:.1f} (md3 limit 511)")

    write_md3(out, shader, verts, norms, uvs, tris, mn, mx)
    if albedo:
        from PIL import Image
        img=Image.open(albedo).convert("RGB")
        tga=out.rsplit(".",1)[0]+".tga"
        img.save(tga)  # PIL writes uncompressed TGA
        print("wrote",tga,img.size)

def write_md3(path, shader, verts, norms, uvs, tris, mn, mx):
    nv=len(verts); ntri=len(tris)
    name=b"warsub".ljust(64,b"\0")
    # frame
    rad=max(math.sqrt(v[0]**2+v[1]**2+v[2]**2) for v in verts)
    frame=struct.pack("<3f3f3ff16s", mn[0],mn[1],mn[2], mx[0],mx[1],mx[2], 0,0,0, rad, b"frame".ljust(16,b"\0"))
    # surface
    sname=b"warsub_skin".ljust(64,b"\0")
    shaders=struct.pack("<64si", shader.encode().ljust(64,b"\0"), 0)
    # reverse winding: glTF/OpenGL front faces are CCW, Q3 md3 expects the
    # opposite, otherwise the model renders inside-out (outer faces culled).
    tribuf=b"".join(struct.pack("<3i",t[0],t[2],t[1]) for t in tris)
    stbuf=b"".join(struct.pack("<2f",u,(1.0-v)) for (u,v) in uvs)  # flip V for Q3
    xyzbuf=b""
    for (x,y,z),n in zip(verts,norms):
        xyzbuf+=struct.pack("<3hH", int(round(x*64)),int(round(y*64)),int(round(z*64)), pack_normal(n))
    # surface header (108 bytes)
    SURF_HDR=108
    ofsShaders=SURF_HDR
    ofsTris=ofsShaders+len(shaders)
    ofsST=ofsTris+len(tribuf)
    ofsXYZ=ofsST+len(stbuf)
    ofsEnd=ofsXYZ+len(xyzbuf)
    surfhdr=struct.pack("<4s64siiiiiiiiii", b"IDP3", sname, 0, 1, 1, nv, ntri, ofsTris, ofsShaders, ofsST, ofsXYZ, ofsEnd)
    surf=surfhdr+shaders+tribuf+stbuf+xyzbuf
    # md3 header (108 bytes)
    MD3_HDR=108
    ofsFrames=MD3_HDR
    ofsTags=ofsFrames+len(frame)
    ofsSurfs=ofsTags+0
    md3End=ofsSurfs+len(surf)
    hdr=struct.pack("<4si64siiiiiiiii", b"IDP3", 15, name, 0, 1, 0, 1, 0, ofsFrames, ofsTags, ofsSurfs, md3End)
    open(path,"wb").write(hdr+frame+surf)
    print("wrote",path, nv,"verts", ntri,"tris")

if __name__=="__main__":
    main()
