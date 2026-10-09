# -*- coding: utf-8 -*-
import build, math
from build import STEPS, HEAD, ACC, ACCNAME, REST, FLAGS, ARTIC
from collections import defaultdict

AUGDOT = ('OpusSpecialStd', 0x2122)
STACC  = ('OpusStd', 0x2E)
TUPNUM = ('OpusTextStd', 0x33, 0x36)

def union(parent,a,b):
    ra,rb=find(parent,a),find(parent,b)
    if ra!=rb: parent[ra]=rb
def find(parent,a):
    while parent[a]!=a: parent[a]=parent[parent[a]]; a=parent[a]
    return a

def ties(fills, beams, heads):
    """A tie is a short, almost flat curve joining two noteheads of one pitch.
    Sibelius draws it as a filled shape, so it is found by its two ends."""
    bmset={(round(b['x0'],2),round(b['y0'],2)) for b in beams}
    out=[]
    for f in fills:
        pts=list(f)
        if pts and pts[0]==pts[-1]: pts=pts[:-1]
        if not (2 <= len(pts) <= 3): continue
        xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
        if (round(min(xs),2),round(min(ys),2)) in bmset: continue
        a=min(pts); b=max(pts)
        if abs(a[1]-b[1])>2.0: continue
        if not (3 < b[0]-a[0] < 40): continue
        ha=[h for h in heads if abs(h['x']+build.head_width(h['size'])/2-a[0])<7
            and abs(h['y']-a[1])<5]
        hb=[h for h in heads if abs(h['x']+build.head_width(h['size'])/2-b[0])<7
            and abs(h['y']-b[1])<5]
        if not (ha and hb): continue
        pa=min(ha,key=lambda h:abs(h['y']-a[1])); pb=min(hb,key=lambda h:abs(h['y']-b[1]))
        if abs(pa['y']-pb['y'])>=1.0 or pa is pb: continue
        out.append((pa,pb))
    return out


def page_events(pg):
    p, stems, beams, heads, others = build.analyse_page(pg)
    # ---- beam groups ----
    idx={id(s):i for i,s in enumerate(stems)}
    parent=list(range(len(stems)))
    for b in beams:
        members=[]
        for s in stems:
            if b['x0']-0.8 <= s['x'] <= b['x1']+0.8:
                y=build.beam_y(b, s['x'])
                if s['lo']-1.6 <= y <= s['hi']+1.6: members.append(idx[id(s)])
        for m in members[1:]: union(parent, members[0], m)
    groups=defaultdict(list)
    for s in stems:
        if s['notes']: groups[find(parent, idx[id(s)])].append(s)
    # ---- chords ----
    events=[]
    for s in stems:
        if not s['notes']: continue
        hd=s['notes'][0]['ch']
        if hd==0x153: base = 1.0/(2**max(s['beams'], s['flags']))
        elif hd==0x2D9: base = 2.0
        else: base = 4.0
        events.append(dict(kind='note', x=s['x'], stem=s, base=base,
                           dir=s['notes'][0]['dir'], group=find(parent, idx[id(s)]),
                           notes=s['notes']))
    # ---- rests ----
    for r in others:
        if r['font']=='OpusStd' and r['ch'] in REST:
            v=REST[r['ch']]
            events.append(dict(kind='rest', x=r['x'], base=v, glyph=r,
                               dir=None, group=None, notes=[]))
    # ---- augmentation dots ----
    dots=[r for r in others if (r['font'],r['ch'])==AUGDOT]
    for e in events:
        if e['kind']=='note':
            ref = max(e['notes'], key=lambda n:n['y'])
            rx = ref['x']+build.head_width(ref['size']); ry=ref['y']
        else:
            ref=e['glyph']; rx=ref['x']+2.5; ry=ref['y']
        n=0
        for d in sorted(dots, key=lambda d:d['x']):
            if rx-0.5 < d['x'] < rx+9 and abs(d['y']-ry) < 2.8: n+=1
        if n: e['base'] *= (2.0 - 0.5**n)
        e['dots']=n
    # ---- tuplets ----
    tups=[r for r in others if r['font']=='OpusTextStd' and r['ch'] in (0x33,0x36)]
    # A TUPLET NUMBER SITS BEYOND THE BEAM, on the side the stems point. Measuring
    # to the noteheads instead picks the wrong voice where two are beamed apart.
    gbeam={}
    for gid, ss in groups.items():
        up=0; free=[]
        for s2 in ss:
            lowest=min(n['y'] for n in s2['notes']); highest=max(n['y'] for n in s2['notes'])
            if (s2['hi']-highest) > (lowest-s2['lo']): up+=1; free.append(s2['hi'])
            else: up-=1; free.append(s2['lo'])
        gbeam[gid]=('up' if up>=0 else 'down', sum(free)/float(len(free)))
    for t in tups:
        best=None; fallback=None
        for gid, ss in groups.items():
            xs=[s2['x'] for s2 in ss]
            if not (min(xs)-7 <= t['x'] <= max(xs)+7): continue
            side, by = gbeam[gid]
            d=abs(t['y']-by)
            if d >= 60: continue      # a group on another system of the same page
            if fallback is None or d<fallback[0]: fallback=(d,gid)
            if side=='up'   and t['y'] < by - 1: continue
            if side=='down' and t['y'] > by + 1: continue
            if best is None or d<best[0]: best=(d,gid)
        # Sibelius sometimes drags a number to the far side; take the nearest group then
        if best is None: best=fallback
        if best and best[0] < 60:
            n = 3 if t['ch']==0x33 else 6
            for e in events:
                if e['group']==best[1]:
                    e['tuplet']=(n, 2 if n==3 else 4)
    for e in events: e.setdefault('tuplet', None)
    for e in events:
        if e['tuplet']:
            a,b=e['tuplet']; e['dur']=e['base']*b/float(a)
        else: e['dur']=e['base']
    # ---- articulations ----
    # ONE MARK BELONGS TO ONE NOTEHEAD: the nearest one it sits over or under.
    marks=[r for r in others if r['font']=='OpusStd' and r['ch'] in ARTIC]
    marks += [r for r in others if (r['font'],r['ch'])==STACC]
    marks += [r for r in others if r['font']=='OpusSpecialStd' and r['ch'] in (0x3A9,0xBA)]
    for e in events: e['artic']=[]
    owner={}
    for e in events:
        if e['kind']!='note': continue
        for n in e['notes']: owner[id(n)]=e
    heads=[n for e in events if e['kind']=='note' for n in e['notes']]
    for a in marks:
        if a['font']=='OpusStd' and a['ch'] in ARTIC: name=ARTIC[a['ch']]
        elif a['font']=='OpusSpecialStd': name='staccatissimo'
        else: name='staccato'
        cxa = a['x'] + build.head_width(a['size'])/2.0
        # AN ARTICULATION IS CENTRED ON ITS NOTEHEAD but sits beyond the stem, so
        # the horizontal line-up is what identifies it, not how far above it is.
        cand=[n for n in heads
              if abs(n['x']+build.head_width(n['size'])/2.0-cxa) < 4.5
              and abs(n['y']-a['y']) < 34 and abs(n['y']-a['y']) > 1.0]
        best=None
        if cand:
            above = all(a['y'] > n['y'] for n in cand)
            below = all(a['y'] < n['y'] for n in cand)
            if above: pick=max(cand, key=lambda n:n['y'])
            elif below: pick=min(cand, key=lambda n:n['y'])
            else: pick=min(cand, key=lambda n:abs(n['y']-a['y']))
            best=(0, pick)
        if best: owner[id(best[1])]['artic'].append(name)
    for e in events: e['artic']=sorted(set(e['artic']))

    # ---- accidentals ----
    accs=[r for r in others if r['font']=='OpusStd' and r['ch'] in ACC]
    heads=[n for e in events for n in e['notes']]
    for n in heads: n['alter']=None
    # ONE ACCIDENTAL BELONGS TO ONE NOTEHEAD: the nearest one to its right on the same
    # line or space. A cautionary in brackets sits further out, hence the wide window.
    for a in accs:
        cand=[n for n in heads if abs(a['y']-n['y'])<1.2 and 0 < n['x']-a['x'] < 21]
        if cand:
            min(cand, key=lambda n:n['x'])['alter']=ACC[a['ch']]
    for e in events:
        for n in e['notes']: n['tie_start']=n['tie_stop']=False
    allheads=[n for e in events for n in e['notes']]
    for pa,pb in ties(p.fills, beams, allheads):
        pa['tie_start']=True; pb['tie_stop']=True
    return p, events, stems, beams, others, groups
