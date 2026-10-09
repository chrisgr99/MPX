# -*- coding: utf-8 -*-
"""Work out when every note in a bar starts.

The durations come from the engraving and are taken as given. What has to be
found is where each beamed group, and each note or rest standing on its own,
begins. Three things pin it down: notes printed at the same horizontal position
sound together, horizontal order is time order, and every staff that has
anything in it fills the bar exactly. Where more than one placement satisfies
all of that, the one wanted is the one needing the fewest voices and leaving
the least silence."""
from collections import defaultdict
from fractions import Fraction as F
from solve import headx, chains

def isbar(e): return e['kind']=='rest' and e.get('base')=='bar'
def dur(e):   return F(e['dur']).limit_denominator(48)

def components(events, tol=1.6):
    ch=chains(events)
    n=len(ch)
    pos=[[(headx(e), off, e) for e,off in c] for c in ch]
    par=list(range(n)); rel=[F(0)]*n
    def root(i):
        if par[i]==i: return i, F(0)
        r,d=root(par[i]); par[i]=r; rel[i]=rel[i]+d; return r, rel[i]
    def link(i,j,delta):
        ri,di=root(i); rj,dj=root(j)
        if ri==rj: return
        par[rj]=ri; rel[rj]=di+delta-dj
    free=[all(isbar(e) for _,_,e in c) for c in pos]
    for i in range(n):
        if free[i]: continue
        for j in range(i+1,n):
            if free[j]: continue
            done=False
            for xi,oi,_ in pos[i]:
                for xj,oj,_ in pos[j]:
                    if abs(xi-xj)<=tol: link(i,j,oi-oj); done=True; break
                if done: break
    comps=defaultdict(list)
    for i in range(n):
        r,d=root(i); comps[r].append((i,d))
    out=[]
    for r,mem in comps.items():
        items=[]
        for i,d in mem:
            for x,o,e in pos[i]: items.append((x, d+o, e))
        items.sort(key=lambda it:(it[0], it[1]))
        lo=min(o for _,o,_ in items)
        out.append([(x, o-lo, e) for x,o,e in items])
    out.sort(key=lambda it: min(x for x,_,_ in it))
    return out

def lanes_needed(spans):
    """how many voices a set of (start,end) intervals needs"""
    ends=[]
    for s,e in sorted(spans):
        for i,v in enumerate(ends):
            if v<=s: ends[i]=e; break
        else: ends.append(e)
    return len(ends)

def score(comps, starts, L):
    """lower is better: voices first, then silence, then late entries"""
    bystaff=defaultdict(list)
    for j,c in enumerate(comps):
        for _,o,e in c:
            bystaff[e['home']].append((starts[j]+o, starts[j]+o+dur(e)))
    voices=0; gap=F(0)
    for st,spans in bystaff.items():
        voices += lanes_needed(spans)
        covered=F(0); cur=None
        for s,e in sorted(spans):
            if cur is None or s>cur[1]:
                if cur: covered += cur[1]-cur[0]
                cur=[s,e]
            else: cur[1]=max(cur[1], e)
        if cur: covered += cur[1]-cur[0]
        gap += L-covered
    return (voices, gap, sum(starts))

def solve_bar(events, L, grid=F(1,24), cap=4000):
    comps=components(events)
    if not comps: return True, []
    Lf=F(L).limit_denominator(48)
    N=len(comps)
    spans=[(min(x for x,_,_ in c), max(x for x,_,_ in c),
            max(o+dur(e) for _,o,e in c)) for c in comps]
    barish=[all(isbar(e) for _,_,e in c) for c in comps]
    staves=[set(e['home'] for _,_,e in c) for c in comps]
    present=set()
    for s in staves: present |= s
    starts=[]; found=[]; seen=[0]

    def ok_mono(k, t):
        if barish[k]: return t==F(0)
        lx,rx,_=spans[k]
        for j in range(k):
            if barish[j]: continue
            for x,o,e in comps[j]:
                tt=starts[j]+o
                if x < lx-1.6 and tt > t: return False
                if x > rx+1.6 and tt < t: return False
        return True

    def rec(k):
        if seen[0] > cap: return
        if k==N:
            seen[0]+=1
            if min(starts)!=0: return
            allends=[starts[j]+o+dur(e) for j,c in enumerate(comps) for _,o,e in c]
            if max(allends)!=Lf: return
            found.append((score(comps, starts, Lf), list(starts)))
            return
        lo,hi,ln=spans[k]
        cands=[]; t=F(0)
        while t+ln<=Lf:
            cands.append(t); t+=grid
        known={F(0)}
        for j in range(k):
            for _,o,e in comps[j]:
                known.add(starts[j]+o); known.add(starts[j]+o+dur(e))
        cands.sort(key=lambda t:(t not in known, t))
        for t in cands:
            if seen[0] > cap: return
            if not ok_mono(k,t): continue
            starts.append(t); rec(k+1); starts.pop()
    rec(0)
    if not found: return False, comps
    best=min(found, key=lambda f:f[0])[1]
    for j,c in enumerate(comps):
        for _,o,e in c: e['on']=best[j]+o
    return True, comps
