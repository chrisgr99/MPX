# -*- coding: utf-8 -*-
"""Write MusicXML from the measures recovered out of the PDF."""
import measures, build, lib2
from solve import headx
from fractions import Fraction as F
from collections import defaultdict
import xml.sax.saxutils as sx

DIV = 96
STEPS = 'CDEFGAB'
TYPES = [(F(8),'breve'),(F(4),'whole'),(F(2),'half'),(F(1),'quarter'),
         (F(1,2),'eighth'),(F(1,4),'16th'),(F(1,8),'32nd'),(F(1,16),'64th')]
DYN = {'p','pp','ppp','pppp','f','ff','fff','mp','mf','sf','sfz','fp'}

def notetype(base):
    b=F(base).limit_denominator(64)
    best=min(TYPES, key=lambda t: abs(float(t[0])-float(b)))
    return best[1]

def esc(s): return sx.escape(s)

# ---------------------------------------------------------------- page extras
def page_extras(pg, p, others):
    """dynamics, worded directions, hairpins, the one 8va, ties"""
    out={'dyn':[], 'words':[], 'wedge':[], 'ottava':[]}
    runs=[r for r in others if r['font']=='OpusTextStd' and r['ch'] not in (0x33,0x36)]
    runs.sort(key=lambda r:(round(r['y'],1), r['x']))
    grp=[]
    for r in runs:
        if grp and abs(r['y']-grp[-1][-1]['y'])<0.5 and r['x']-grp[-1][-1]['x'] < 9:
            grp[-1].append(r)
        else: grp.append([r])
    for g in grp:
        w=''.join(chr(r['ch']) for r in g)
        if w in DYN: out['dyn'].append((g[0]['x'], g[0]['y'], w))
    for r in others:
        if r['font'].startswith(('TimesNewRoman','PlantinMTStd')) and r['size']>6:
            pass
    return out

def words_of(pg):
    """italic performance words, joined by the real glyph advances"""
    g,l,f = lib2.page(pg)
    runs=[]; cur=None
    for r in g:
        if not r['font'].startswith(('TimesNewRoman','PlantinMTStd')): continue
        ch=chr(r['ch'])
        if cur is not None and abs(r['y']-cur['y'])<0.6 and r['x']-cur['xe'] < 1.2:
            cur['s']+=ch; cur['xe']=r['x']+r.get('adv',0)
        else:
            if cur: runs.append(cur)
            cur=dict(s=ch, x=r['x'], y=r['y'], xe=r['x']+r.get('adv',0), size=r['size'])
    if cur: runs.append(cur)
    keep=[]
    for r in runs:
        s=' '.join(r['s'].split())
        if len(s)<3: continue
        low=s.lower()
        if 'musescore' in low or 'hansen' in low or 'nberg' in low: continue
        if 'klavier' in low or 'edition' in low: continue
        if s.replace('-','').replace(' ','')=='' : continue
        keep.append((r['x'], r['y'], s))
    return keep

def hairpins(strokes, staves):
    ends=defaultdict(list)
    for a,b,w in strokes:
        if w>0.8: continue
        if abs(a[0]-b[0])<6: continue
        if abs(a[1]-b[1])>5: continue
        for p,q in ((a,b),(b,a)):
            ends[(round(p[0],1),round(p[1],1))].append(q)
    out=[]
    for (px,py),qs in ends.items():
        if len(qs)!=2: continue
        (q1,q2)=qs
        if abs(q1[0]-q2[0])>1.0: continue
        if abs(q1[1]-q2[1])<2.0: continue
        if abs((q1[1]+q2[1])/2.0 - py) > 1.2: continue
        x0,x1=min(px,q1[0]), max(px,q1[0])
        if x1-x0 < 5: continue
        kind='crescendo' if px<q1[0] else 'diminuendo'
        out.append((x0,x1,py,kind))
    return out
