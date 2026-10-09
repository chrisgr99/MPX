# -*- coding: utf-8 -*-
from collections import defaultdict
from fractions import Fraction as F

def headx(e):
    if e['kind']=='rest': return e['glyph']['x']
    return min(n['x'] for n in e['notes'])

def chains(events):
    """beam groups (and singletons) with internal offsets"""
    byg=defaultdict(list)
    singles=[]
    for e in events:
        if e.get('grace'): continue
        if e['kind']=='note' and e['group'] is not None: byg[e['group']].append(e)
        else: singles.append(e)
    out=[]
    for g,mem in byg.items():
        mem.sort(key=headx)
        off=F(0); items=[]
        for e in mem:
            items.append((e, off)); off += F(e['dur']).limit_denominator(48)
        out.append(items)
    for e in singles: out.append([(e, F(0))])
    return out

def find(p,a):
    while p[a]!=a: p[a]=p[p[a]]; a=p[a]
    return a

def solve(events, L, tol=1.6):
    ch=chains(events)
    if not ch: return True, 0.0
    n=len(ch)
    par=list(range(n)); rel={i:F(0) for i in range(n)}   # rel = start of chain i relative to root
    # positions
    pos=[[(headx(e), off, e) for e,off in c] for c in ch]
    def root(i):
        if par[i]==i: return i, F(0)
        r,d = root(par[i]); par[i]=r; rel[i]=rel[i]+d
        return r, rel[i]
    def link(i,j,delta):
        """start_j = start_i + delta"""
        ri,di = root(i); rj,dj = root(j)
        if ri==rj: return
        par[rj]=ri; rel[rj] = di + delta - dj
    for i in range(n):
        for j in range(i+1,n):
            for xi,oi,_ in pos[i]:
                for xj,oj,_ in pos[j]:
                    if abs(xi-xj) <= tol:
                        link(i,j, oi-oj); break
                else: continue
                break
    comps=defaultdict(list)
    for i in range(n):
        r,d = root(i); comps[r].append((i,d))
    # anchor: component containing the leftmost chain starts at 0
    solved={}
    order=sorted(comps.items(), key=lambda kv: min(min(x for x,_,_ in pos[i]) for i,_ in kv[1]))
    anchors=[]; placed=[]
    for ci,(r,mem) in enumerate(order):
        base=None
        if ci==0: base=F(0) - min(d+o for i,d in mem for _,o,_ in pos[i])
        else:
            lx = min(x for i,_ in mem for x,_,_ in pos[i])
            rx = max(x for i,_ in mem for x,_,_ in pos[i])
            first = min((d+o) for i,d in mem for _,o,_ in pos[i])
            best=None
            for ax,at in anchors:
                if abs(ax-lx)<=tol: best=at; break
            if best is None:
                cands={F(0)}
                for e2 in placed:
                    cands.add(e2['on']); cands.add(e2['on']+F(e2['dur']).limit_denominator(48))
                before=[e2['on'] for e2 in placed if headx(e2) < lx-tol]
                after=[e2['on'] for e2 in placed if headx(e2) > rx+tol]
                lowbar = max(before) if before else None
                highbar = min(after) if after else None
                ok=[c for c in sorted(cands)
                    if (lowbar is None or c > lowbar)
                    and (highbar is None or c <= highbar)]
                if not ok:
                    ok=[c for c in sorted(cands) if (lowbar is None or c > lowbar)]
                best = ok[0] if ok else F(0)
            base = best - first
        for i,d in mem:
            for x,o,e in pos[i]:
                t = base + d + o
                e['on']=t; solved[id(e)]=t
                anchors.append((x,t)); placed.append(e)
        anchors.sort()
    ends=[e['on']+F(e['dur']).limit_denominator(48) for c in ch for e,_ in c]
    starts=[e['on'] for c in ch for e,_ in c]
    total=max(ends) if ends else F(0)
    ok = (min(starts)==0) and (total==F(L).limit_denominator(48))
    return ok, float(total)
