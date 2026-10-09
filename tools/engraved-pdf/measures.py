# -*- coding: utf-8 -*-
import assemble, build, layout
from collections import defaultdict

def grace(e):
    return e['kind']=='note' and all(n['size']<15 for n in e['notes'])

def build_measures():
    out=[]
    num,den=4,8
    for pg in range(1,6):
        p, ev, stems, beams, others, groups = assemble.page_events(pg)
        tsd=[r for r in others if r['font']=='OpusStd' and 0x30<=r['ch']<=0x39 and r['size']>20]
        for top,bot in p.systems:
            mid=(bot.top+top.bottom)/2.0
            lo,hi = bot.bottom-34, top.top+34
            bl=layout.barlines(p, top, bot)
            ts=[]
            cand=sorted([r for r in tsd if lo<r['y']<hi], key=lambda r:(round(r['x']), -r['y']))
            i=0
            while i+1<len(cand):
                a,b=cand[i],cand[i+1]
                if abs(a['x']-b['x'])<3 and a['y']>b['y']:
                    ts.append((a['x'], a['ch']-0x30, b['ch']-0x30)); i+=2
                else: i+=1
            def staff_of(e):
                y=(sum(n['y'] for n in e['notes'])/len(e['notes'])) if e['notes'] else e['glyph']['y']
                return 0 if y>mid else 1
            es=[]
            for e in ev:
                y=(sum(n['y'] for n in e['notes'])/len(e['notes'])) if e['notes'] else e['glyph']['y']
                if lo<y<hi:
                    e['staff']=staff_of(e); e['grace']=grace(e); es.append(e)
            # cross-staff groups keep one home staff
            byg=defaultdict(list)
            for e in es:
                if e['group'] is not None: byg[e['group']].append(e)
            for g,mem in byg.items():
                if len(mem)<2: continue
                mem.sort(key=lambda e:e['x'])
                home=mem[0]['staff']
                for e in mem: e['home']=home
            for e in es: e.setdefault('home', e['staff'])
            for i in range(len(bl)-1):
                x0,x1=bl[i],bl[i+1]
                for tx,n,d in ts:
                    if x0-3 <= tx <= x1: num,den=n,d
                me=[e for e in es if x0<e['x']<x1]
                out.append(dict(page=pg, top=top, bot=bot, mid=mid, x0=x0, x1=x1,
                                num=num, den=den, events=sorted(me,key=lambda e:e['x']),
                                others=others, p=p))
    return out
