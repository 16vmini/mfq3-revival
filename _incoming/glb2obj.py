#!/usr/bin/env python3
# GLB (glTF 2.0 binary) -> OBJ converter that BAKES node-tree world transforms
# and emits each mesh under an OBJ group named after its nearest meaningful node
# (so vtol_19, fan_58, noseGear_45, ... survive into obj2md3.ps1 for part breakout).
import sys, json, struct, math, os, re

def load_glb(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic, ver, length = struct.unpack_from('<4sII', data, 0)
    assert magic == b'glTF', "not a GLB"
    off = 12; js = None; bin_ = None
    while off < length:
        clen, ctype = struct.unpack_from('<II', data, off); off += 8
        chunk = data[off:off+clen]; off += clen
        if ctype == 0x4E4F534A: js = json.loads(chunk.decode('utf-8'))
        elif ctype == 0x004E4942: bin_ = chunk
    return js, bin_

COMP = {5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}
NC = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}

def read_accessor(g, bin_, idx):
    acc = g['accessors'][idx]
    bv = g['bufferViews'][acc['bufferView']]
    comp, csz = COMP[acc['componentType']]; nc = NC[acc['type']]
    base = bv.get('byteOffset',0) + acc.get('byteOffset',0)
    stride = bv.get('byteStride') or (csz*nc)
    out = []
    for i in range(acc['count']):
        o = base + i*stride
        out.append(struct.unpack_from('<'+comp*nc, bin_, o))
    return out

# ---- 4x4 row-major matrix helpers ----
def ident(): return [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
def mul(a,b):
    return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
def from_cols(m):  # glTF matrix is column-major 16
    return [[m[0],m[4],m[8],m[12]],[m[1],m[5],m[9],m[13]],[m[2],m[6],m[10],m[14]],[m[3],m[7],m[11],m[15]]]
def quat_mat(x,y,z,w):
    return [[1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w),   0],
            [2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w),   0],
            [2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y), 0],
            [0,0,0,1]]
def trs(t,r,s):
    T=ident(); T[0][3],T[1][3],T[2][3]=t
    R=quat_mat(*r)
    S=[[s[0],0,0,0],[0,s[1],0,0],[0,0,s[2],0],[0,0,0,1]]
    return mul(mul(T,R),S)
def node_local(n):
    if 'matrix' in n: return from_cols(n['matrix'])
    t=n.get('translation',[0,0,0]); r=n.get('rotation',[0,0,0,1]); s=n.get('scale',[1,1,1])
    return trs(t,r,s)
def xform_pt(m,p):
    x,y,z=p
    return (m[0][0]*x+m[0][1]*y+m[0][2]*z+m[0][3],
            m[1][0]*x+m[1][1]*y+m[1][2]*z+m[1][3],
            m[2][0]*x+m[2][1]*y+m[2][2]*z+m[2][3])
def xform_dir(m,d):  # upper 3x3 (good enough for rot/uniform-scale), renormalised
    x,y,z=d
    nx=m[0][0]*x+m[0][1]*y+m[0][2]*z
    ny=m[1][0]*x+m[1][1]*y+m[1][2]*z
    nz=m[2][0]*x+m[2][1]*y+m[2][2]*z
    l=math.sqrt(nx*nx+ny*ny+nz*nz) or 1.0
    return (nx/l,ny/l,nz/l)

SKIP = re.compile(r'^(Object_\d+|Sketchfab_model|root|RootNode|GLTF_SceneRootNode|.*\.fbx)$', re.I)
def meaningful(name):
    return bool(name) and not SKIP.match(name)

def main():
    glb, out_obj = sys.argv[1], sys.argv[2]
    g, bin_ = load_glb(glb)
    lines=[]; vbase=0
    groups={}  # name -> tri count
    total_tris=[0]

    def emit(node_idx, parent_mat, group):
        nonlocal vbase
        n = g['nodes'][node_idx]
        world = mul(parent_mat, node_local(n))
        nm = n.get('name','')
        grp = nm if meaningful(nm) else group
        if 'mesh' in n:
            mesh = g['meshes'][n['mesh']]
            for prim in mesh['primitives']:
                att = prim['attributes']
                if 'POSITION' not in att: continue
                pos = read_accessor(g, bin_, att['POSITION'])
                nrm = read_accessor(g, bin_, att['NORMAL']) if 'NORMAL' in att else [(0,0,1)]*len(pos)
                uv  = read_accessor(g, bin_, att['TEXCOORD_0']) if 'TEXCOORD_0' in att else [(0,0)]*len(pos)
                if 'indices' in prim:
                    idx=[i[0] for i in read_accessor(g, bin_, prim['indices'])]
                else:
                    idx=list(range(len(pos)))
                mi = prim.get('material')
                mats = g.get('materials',[])
                if mi is not None and mi < len(mats):
                    mname = mats[mi].get('name') or ('mat%d'%mi)
                else:
                    mname = 'default'
                mname = re.sub(r'[^A-Za-z0-9]','_', mname)
                gname = grp or 'body'
                lines.append('g '+gname)
                lines.append('usemtl '+mname)
                for p in pos:
                    wp=xform_pt(world,p); lines.append('v %.6f %.6f %.6f'%wp)
                for t in uv:
                    lines.append('vt %.6f %.6f'%(t[0],t[1]))
                for nn in nrm:
                    wn=xform_dir(world,nn); lines.append('vn %.6f %.6f %.6f'%wn)
                for k in range(0,len(idx)-2,3):
                    a=idx[k]+vbase+1; b=idx[k+1]+vbase+1; c=idx[k+2]+vbase+1
                    lines.append('f %d/%d/%d %d/%d/%d %d/%d/%d'%(a,a,a,b,b,b,c,c,c))
                ntri=len(idx)//3
                groups[gname]=groups.get(gname,0)+ntri; total_tris[0]+=ntri
                vbase+=len(pos)
        for ch in n.get('children',[]):
            emit(ch, world, grp)

    scene = g.get('scene',0)
    for root in g['scenes'][scene]['nodes']:
        emit(root, ident(), None)

    with open(out_obj,'w') as f:
        f.write('\n'.join(lines))
    print("wrote %s : %d verts-emitted, %d tris, %d groups"%(out_obj, vbase, total_tris[0], len(groups)))
    print("--- groups (name: tris) ---")
    for k in sorted(groups, key=lambda x:-groups[x]):
        print("  %-22s %d"%(k, groups[k]))

if __name__=='__main__':
    main()
