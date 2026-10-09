# -*- coding: utf-8 -*-
import assemble, build
from collections import defaultdict

def barlines(p, top, bot):
    xs=[]
    for a,b,w in p.strokes:
        if abs(a[0]-b[0])>0.8: continue
        lo,hi=min(a[1],b[1]),max(a[1],b[1])
        if lo <= bot.bottom+1 and hi >= top.top-1 and w>0.55:
            xs.append(round(a[0],1))
    return sorted(set(xs))

def timesigs(p, others, top, bot):
    out=[]
    for st in (top,bot):
        digs=[r for r in others if r['font']=='OpusStd' and r['ch'] in range(0x30,0x3A)
              and r['size']>22 and st.bottom-4 < r['y'] < st.top+4]
        digs.sort(key=lambda r:(round(r['x'],0), -r['y']))
        i=0
        while i+1 < len(digs):
            a,b=digs[i],digs[i+1]
            if abs(a['x']-b['x'])<3 and a['y']>b['y']:
                out.append((a['x'], a['ch']-0x30, b['ch']-0x30)); i+=2
            else: i+=1
    return sorted(set(out))

def systems_of(pg):
    p, ev, stems, beams, others, groups = assemble.page_events(pg)
    out=[]
    for top,bot in p.systems:
        mid=(bot.top+top.bottom)/2.0
        bl=barlines(p, top, bot)
        def own(e):
            y = (sum(n['y'] for n in e['notes'])/len(e['notes'])) if e['notes'] else e['glyph']['y']
            return bot.bottom-30 < y < top.top+30
        es=[e for e in ev if own(e)]
        out.append(dict(page=pg, top=top, bot=bot, mid=mid, bars=bl, events=es,
                        ts=timesigs(p, others, top, bot), others=others, p=p))
    return out

def allsystems():
    out=[]
    for pg in range(1,6): out += systems_of(pg)
    return out
