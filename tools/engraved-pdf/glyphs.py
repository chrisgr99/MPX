import lib, struct
from PIL import Image, ImageDraw

def tables(d):
    num = struct.unpack('>H', d[4:6])[0]; t={}
    for i in range(num):
        off=12+16*i
        t[d[off:off+4].decode('latin-1')] = struct.unpack('>II', d[off+8:off+16])
    return t

def outlines(d, gid):
    t = tables(d)
    ho,_ = t['head']; fmt = struct.unpack('>h', d[ho+50:ho+52])[0]
    lo,ll = t['loca']
    if fmt==0:
        offs = struct.unpack('>%dH'%(ll//2), d[lo:lo+ll]); offs=[o*2 for o in offs]
    else:
        offs = struct.unpack('>%dI'%(ll//4), d[lo:lo+ll])
    go,_ = t['glyf']
    s,e = go+offs[gid], go+offs[gid+1]
    if s>=e: return []
    nc = struct.unpack('>h', d[s:s+2])[0]
    if nc < 0: return []
    p = s+10
    ends = struct.unpack('>%dH'%nc, d[p:p+2*nc]); p+=2*nc
    il = struct.unpack('>H', d[p:p+2])[0]; p+=2+il
    npt = ends[-1]+1
    flags=[]
    while len(flags)<npt:
        f=d[p]; p+=1; flags.append(f)
        if f & 8:
            r=d[p]; p+=1; flags += [f]*r
    xs=[]; v=0
    for f in flags:
        if f&2:
            dx=d[p]; p+=1; v += dx if f&16 else -dx
        elif not f&16:
            v += struct.unpack('>h', d[p:p+2])[0]; p+=2
        xs.append(v)
    ys=[]; v=0
    for f in flags:
        if f&4:
            dy=d[p]; p+=1; v += dy if f&32 else -dy
        elif not f&32:
            v += struct.unpack('>h', d[p:p+2])[0]; p+=2
        ys.append(v)
    contours=[]; st=0
    for en in ends:
        pts=[(xs[i],ys[i],bool(flags[i]&1)) for i in range(st,en+1)]
        contours.append(pts); st=en+1
    return contours

def render(d, gid, size=110):
    cs = outlines(d, gid)
    if not cs: return None
    allp=[p for c in cs for p in c]
    minx=min(p[0] for p in allp); maxx=max(p[0] for p in allp)
    miny=min(p[1] for p in allp); maxy=max(p[1] for p in allp)
    w=max(maxx-minx,1); h=max(maxy-miny,1); sc=size/max(w,h)
    im=Image.new('L',(int(w*sc)+8,int(h*sc)+8),255)
    dr=ImageDraw.Draw(im)
    for c in cs:
        poly=[]
        n=len(c)
        # quadratic -> polyline
        pts=c[:]
        # ensure start on-curve
        if not pts[0][2]:
            for i,p in enumerate(pts):
                if p[2]: pts=pts[i:]+pts[:i]; break
        i=0; cur=(pts[0][0],pts[0][1]); poly.append(cur)
        i=1
        while i<=len(pts):
            p=pts[i%len(pts)]
            if p[2]:
                poly.append((p[0],p[1])); cur=(p[0],p[1]); i+=1
            else:
                nx=pts[(i+1)%len(pts)]
                end=(nx[0],nx[1]) if nx[2] else ((p[0]+nx[0])/2,(p[1]+nx[1])/2)
                for k in range(1,9):
                    t=k/8.0
                    x=(1-t)**2*cur[0]+2*(1-t)*t*p[0]+t*t*end[0]
                    y=(1-t)**2*cur[1]+2*(1-t)*t*p[1]+t*t*end[1]
                    poly.append((x,y))
                cur=end; i+= 1 if not nx[2] else 2
        sp=[(4+(x-minx)*sc, 4+(maxy-y)*sc) for x,y in poly]
        dr.polygon(sp, fill=0)
    return im
