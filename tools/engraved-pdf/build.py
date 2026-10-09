# -*- coding: utf-8 -*-
"""Rebuild MusicXML from a Sibelius-engraved PDF (Opus music fonts, vector text)."""
import lib2, sys, math
from collections import defaultdict

STEPS='CDEFGAB'
HEAD={0x153:'black', 0x2D9:'half', 0x3A9:'whole'}
ACC={0x23:1, 0x62:-1, 0x6E:0}
ACCNAME={1:'sharp', -1:'flat', 0:'natural'}
REST={0x152:1.0, 0x2030:0.5, 0x2248:0.25, 0xAE:0.125, 0x2211:'bar', 0xD3:'bar'}
FLAGS={0x72,0x6A,0x4A,0x52,0xD4,0x4B}
# how many hooks each flag glyph carries (Opus draws a 16th flag as one two-hook glyph)
HOOKS={0x72:2, 0x52:2, 0x6A:1, 0x4A:1, 0xD4:1, 0x4B:1}
ARTIC={0x3E:'accent', 0x5E:'strong-accent', 0x76:'strong-accent',
       0xC6:'staccatissimo', 0x2D:'tenuto', 0x55:'fermata'}
DIGIT={0x30+i:i for i in range(10)}

class Staff(object):
    def __init__(s, lines):
        s.lines=lines; s.bottom=lines[0]; s.top=lines[4]
        s.gap=(lines[4]-lines[0])/4.0
        s.clefs=[]
    def pos(s,y): return (y-s.bottom)/(s.gap/2.0)
    def clefAt(s,x):
        c=s.clefs[0][1] if s.clefs else 'G'
        for cx,ck in sorted(s.clefs):
            if cx<=x+0.5: c=ck
        return c
    def diat(s,y,x):
        p=int(round(s.pos(y)))
        return (32+(p-2)) if s.clefAt(x)=='G' else (24+(p-6))

def find_staves(strokes):
    hor=sorted(set(round(a[1],2) for a,b,w in strokes
                   if abs(a[1]-b[1])<0.5 and abs(a[0]-b[0])>50))
    out=[];i=0
    while i+4<len(hor):
        g=hor[i:i+5]
        if g[4]-g[0]<20: out.append(Staff(g)); i+=5
        else: i+=1
    return out

def quad(points):
    xs=[p[0] for p in points]; ys=[p[1] for p in points]
    return min(xs),max(xs),min(ys),max(ys)

class Page(object):
    def __init__(p, pg):
        p.g, p.strokes, p.fills = lib2.page(pg)
        p.num=pg
        p.staves=find_staves(p.strokes)
        p.staves.sort(key=lambda s:-s.bottom)
        for r in p.g:
            if r['font']=='OpusStd' and r['ch'] in (0x26,0x3F):
                st=p.near(r['y'])
                st.clefs.append((r['x'], 'G' if r['ch']==0x26 else 'F'))
        p.systems=[(p.staves[i], p.staves[i+1]) for i in range(0,len(p.staves)-1,2)]
    def near(p,y):
        return min(p.staves, key=lambda s: min(abs(y-v) for v in s.lines))

def stems_of(strokes):
    out=[]
    for a,b,w in strokes:
        if abs(a[0]-b[0])>0.8: continue
        lo,hi=min(a[1],b[1]),max(a[1],b[1])
        if hi-lo < 3.5: continue
        if w > 0.55: continue           # barlines and braces are heavier
        out.append(dict(x=(a[0]+b[0])/2.0, lo=lo, hi=hi))
    return out

def beams_of(fills):
    """A beam is a thin parallelogram: thickness is measured across it, not from
    its bounding box, because a steep beam has a tall box and a thin body."""
    out=[]
    for p in fills:
        pts=list(p)
        if pts and pts[0]==pts[-1]: pts=pts[:-1]
        if len(pts) != 4: continue
        x0,x1,y0,y1 = quad(pts)
        if x1-x0 < 3: continue
        mx=(x0+x1)/2.0
        left=[q[1] for q in pts if q[0] < mx]; right=[q[1] for q in pts if q[0] >= mx]
        if len(left)!=2 or len(right)!=2: continue
        tl=abs(left[0]-left[1]); tr=abs(right[0]-right[1])
        if not (1.2 < tl < 4.5 and 1.2 < tr < 4.5): continue
        if abs(tl-tr) > 1.0: continue
        # linear top/bottom edge: model centre line through the quad
        out.append(dict(x0=x0,x1=x1,y0=y0,y1=y1,pts=pts))
    return out

def beam_y(bm, x):
    """centre y of the beam at x (linear interpolation of its corners)"""
    pts=sorted(bm['pts'])
    left=[q for q in pts if q[0] < (bm['x0']+bm['x1'])/2.0]
    right=[q for q in pts if q[0] >= (bm['x0']+bm['x1'])/2.0]
    if not left or not right: return (bm['y0']+bm['y1'])/2.0
    ly=sum(q[1] for q in left)/len(left); ry=sum(q[1] for q in right)/len(right)
    lx=sum(q[0] for q in left)/len(left); rx=sum(q[0] for q in right)/len(right)
    if abs(rx-lx)<0.01: return (ly+ry)/2.0
    return ly + (ry-ly)*(x-lx)/(rx-lx)

def head_width(size): return 0.339*size

def analyse_page(pg):
    p=Page(pg)
    stems=stems_of(p.strokes)
    beams=beams_of(p.fills)
    heads=[]; others=[]
    for r in p.g:
        if r['font']=='OpusStd' and r['ch'] in HEAD:
            heads.append(r)
        else:
            others.append(r)
    # attach stems (a chord with a second displaces one head to the far side)
    for r in heads:
        hw=head_width(r['size']); r['stem']=None; r['dir']=None; r['displaced']=False
        best=None
        for s in stems:
            if not (s['lo']-1.2 <= r['y'] <= s['hi']+1.2): continue
            for off,side,disp in ((hw,'up',False),(0.0,'down',False),
                                  (2*hw,'up',True),(-hw,'down',True)):
                if abs(s['x']-(r['x']+off)) < 1.1:
                    score=abs(s['lo']-r['y'])+abs(s['hi']-r['y'])+(4 if disp else 0)
                    if best is None or score<best[0]: best=(score,s,side,disp)
                    break
        if best:
            r['stem']=best[1]; r['dir']=best[2]; r['displaced']=best[3]
    # a stem's direction is decided by where it sits against its own notes
    
    # beams per stem
    for s in stems:
        n=0
        for b in beams:
            if b['x0']-0.8 <= s['x'] <= b['x1']+0.8:
                y=beam_y(b, s['x'])
                if s['lo']-1.6 <= y <= s['hi']+1.6: n+=1
        s['beams']=n
        s['notes']=[]
    for r in heads:
        if r['stem'] is not None: r['stem']['notes'].append(r)
    for s in stems:
        if not s['notes']: continue
        lowest=min(n['y'] for n in s['notes']); highest=max(n['y'] for n in s['notes'])
        d = 'down' if (lowest-s['lo']) > (s['hi']-highest) else 'up'
        for n in s['notes']: n['dir']=d
    # flags
    for r in others:
        if r['font']=='OpusStd' and r['ch'] in FLAGS:
            for s in stems:
                if abs(s['x']-r['x'])<1.4 and s['lo']-2 <= r['y'] <= s['hi']+2:
                    s['beams']=max(s['beams'], 0)
                    s.setdefault('flags',0)
                    s['flags']=s.get('flags',0)+HOOKS.get(r['ch'],1)
                    break
    for s in stems: s.setdefault('flags',0)
    return p, stems, beams, heads, others
