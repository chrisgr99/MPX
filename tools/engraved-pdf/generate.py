# -*- coding: utf-8 -*-
"""Schoenberg op.23 no.1, rebuilt as MusicXML from the engraved PDF."""
import re
import measures, solve3 as solve2, build, lib2, toxml
from solve import headx
from fractions import Fraction as F
from collections import defaultdict
import xml.sax.saxutils as sx

DIV=96
STEPS='CDEFGAB'
TYPES=[(F(4),'whole'),(F(2),'half'),(F(1),'quarter'),(F(1,2),'eighth'),
       (F(1,4),'16th'),(F(1,8),'32nd'),(F(1,16),'64th'),(F(1,32),'128th')]
ALTERNAME={1:'sharp',-1:'flat',0:'natural',2:'double-sharp',-2:'flat-flat'}

# the one 8va in the piece: page 3, above the treble staff of the second system
OTTAVA=dict(page=3, x0=66.0, x1=154.0, ylo=440.0, yhi=470.0)

def ntype(base):
    b=F(base).limit_denominator(128)
    return min(TYPES, key=lambda t: abs(float(t[0])-float(b)))[1]

def tick(q): 
    v=F(q).limit_denominator(96)*DIV
    return int(round(float(v)))

def pitchof(m, n):
    st=m['p'].near(n['y']); d=st.diat(n['y'], n['x'])
    return STEPS[d%7], d//7, st

def collect():
    ms=measures.build_measures()
    # the stub after the final barline
    while ms and not ms[-1]['events']: ms.pop()
    pagewords={pg: toxml.words_of(pg) for pg in range(1,6)}
    pagehair={}; pagedyn={}
    for pg in range(1,6):
        g,l,f=lib2.page(pg)
        pagehair[pg]=toxml.hairpins(l, None)
        runs=[r for r in g if r['font']=='OpusTextStd' and r['ch'] not in (0x33,0x36)]
        runs.sort(key=lambda r:(round(r['y'],1), r['x']))
        grp=[]
        for r in runs:
            if grp and abs(r['y']-grp[-1][-1]['y'])<0.5 and r['x']-grp[-1][-1]['x']<9:
                grp[-1].append(r)
            else: grp.append([r])
        pagedyn[pg]=[(g2[0]['x'], g2[0]['y'], ''.join(chr(r['ch']) for r in g2)) for g2 in grp]
    for m in ms:
        L=m['num']*4.0/m['den']
        for e in m['events']:
            if e['kind']=='rest' and e['base']=='bar': e['dur']=L
        ok,_=solve2.solve_bar(m['events'], L)
        m['ok']=ok; m['L']=F(L).limit_denominator(48)
    return ms, pagewords, pagehair, pagedyn

def in_span(m, x, ylo, yhi, page):
    return m['page']==page and ylo < m['top'].bottom < yhi

def voices_for(m, staff):
    """split one staff's events into voices, newest free voice wins"""
    es=[e for e in m['events'] if e['home']==staff and not e.get('grace')]
    es.sort(key=lambda e:(e['on'], -F(e['dur']).limit_denominator(48)))
    lanes=[]
    for e in es:
        placed=False
        for ln in lanes:
            if ln[-1]['on']+F(ln[-1]['dur']).limit_denominator(48) <= e['on']:
                ln.append(e); placed=True; break
        if not placed: lanes.append([e])
    return lanes

def grace_before(m, staff):
    out=defaultdict(list)
    gs=[e for e in m['events'] if e.get('grace') and e['home']==staff]
    for g in gs:
        after=[e for e in m['events'] if not e.get('grace') and e['home']==staff
               and headx(e) > headx(g)-0.5]
        if after:
            tgt=min(after, key=lambda e: headx(e))
            out[id(tgt)].append(g)
        else: out[None].append(g)
    return out

def emit():
    ms, pagewords, pagehair, pagedyn = collect()
    out=[]
    W=out.append
    W('<?xml version="1.0" encoding="UTF-8"?>')
    W('<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" '
      '"http://www.musicxml.org/dtds/partwise.dtd">')
    W('<score-partwise version="4.0">')
    W('  <work><work-title>Fünf Klavierstücke, Op. 23 — I. Sehr langsam</work-title></work>')
    W('  <identification><creator type="composer">Arnold Schoenberg</creator>')
    W('    <encoding><software>recovered from the engraved PDF</software>'
      '<encoding-description>pitches, rhythm and marks read from the vector '
      'glyph positions of a Sibelius-set score</encoding-description></encoding>')
    W('    <rights>Published 1923, Wilhelm Hansen — public domain in the USA</rights>')
    W('  </identification>')
    W('  <part-list><score-part id="P1"><part-name>Piano</part-name>')
    W('    <score-instrument id="P1-I1"><instrument-name>Piano</instrument-name></score-instrument>')
    W('    <midi-instrument id="P1-I1"><midi-channel>1</midi-channel><midi-program>1</midi-program></midi-instrument>')
    W('  </score-part></part-list>')
    W('  <part id="P1">')

    lastnum=lastden=None
    lastclef={0:None,1:None}
    wedgeopen=False
    ottopen=False
    donewords=set(); donedyn=set(); donehair=set()

    for mi,m in enumerate(ms):
        L=m['L']; x0,x1=m['x0'],m['x1']; pg=m['page']
        W('    <measure number="%d">'%(mi+1))
        if mi and (m['page'], m['top'].bottom) != (ms[mi-1]['page'], ms[mi-1]['top'].bottom):
            W('      <print new-system="yes"%s/>'
              %(' new-page="yes"' if m['page']!=ms[mi-1]['page'] else ''))
        attrs=[]
        if mi==0: attrs.append('        <divisions>%d</divisions>'%DIV)
        if mi==0: attrs.append('        <key><fifths>0</fifths></key>')
        if (m['num'],m['den'])!=(lastnum,lastden):
            attrs.append('        <time><beats>%d</beats><beat-type>%d</beat-type></time>'%(m['num'],m['den']))
            lastnum,lastden=m['num'],m['den']
        if mi==0: attrs.append('        <staves>2</staves>')
        for si,st in ((1,m['top']),(2,m['bot'])):
            c=st.clefAt(x0+0.5)
            if c!=lastclef[si-1]:
                attrs.append('        <clef number="%d"><sign>%s</sign><line>%d</line></clef>'
                             %(si, c, 2 if c=='G' else 4))
                lastclef[si-1]=c
        if attrs:
            W('      <attributes>'); [W(a) for a in attrs]; W('      </attributes>')
        if mi==0:
            W('      <direction placement="above"><direction-type><words font-style="italic">'
              'Sehr langsam (Very slowly)</words></direction-type>'
              '<sound tempo="40"/></direction>')

        # ---- directions, kept at the x they sit at in the engraving ------
        pend={1:[], 2:[]}
        for wx,wy,ws in pagewords[pg]:
            key=(pg,round(wx,1),round(wy,1))
            if key in donewords or not (x0<=wx<x1): continue
            if not (m['bot'].bottom-40 < wy < m['top'].top+40): continue
            donewords.add(key)
            if ws.startswith('Sehr langsam'): continue
            pend[1].append((wx, '      <direction placement="above"><direction-type>'
                '<words font-style="italic">%s</words></direction-type></direction>'%sx.escape(ws)))
        for dx,dy,dw in pagedyn[pg]:
            key=(pg,round(dx,1),round(dy,1))
            if key in donedyn or not (x0<=dx<x1): continue
            if not (m['bot'].bottom-40 < dy < m['top'].top+40): continue
            donedyn.add(key)
            s = 1 if dy > m['mid'] else 2
            pend[s].append((dx, '      <direction placement="below"><direction-type><dynamics>'
                '<%s/></dynamics></direction-type><staff>%d</staff></direction>'%(dw,s)))
        for hx0,hx1,hy,kind in pagehair[pg]:
            key=(pg,round(hx0,1),round(hy,1))
            if key in donehair or not (x0<=hx0<x1): continue
            if not (m['bot'].bottom-40 < hy < m['top'].top+40): continue
            donehair.add(key)
            s = 1 if hy > m['mid'] else 2
            wn = (len(donehair)%6)+1
            pend[s].append((hx0, '      <direction placement="below"><direction-type>'
                '<wedge type="%s" number="%d"/></direction-type><staff>%d</staff></direction>'
                %(kind,wn,s)))
            pend[s].append((min(hx1,x1-0.2), '      <direction placement="below"><direction-type>'
                '<wedge type="stop" number="%d"/></direction-type><staff>%d</staff></direction>'
                %(wn,s)))
        for s in pend: pend[s].sort(key=lambda t:t[0])

        # ---- notes --------------------------------------------------------
        alter_state={}
        vnum=0
        for staff in (0,1):
            lanes=voices_for(m, staff)
            graces=grace_before(m, staff)
            q=pend[staff+1] if lanes else []
            for li,lane in enumerate(lanes):
                vnum+=1
                if vnum>1: W('      <backup><duration>%d</duration></backup>'%tick(L))
                t=F(0)
                opened=set()
                for e in lane:
                    if li==0:
                        # a hairpin must span at least one note, or it draws as a dot
                        while q and q[0][0] <= headx(e)+0.5:
                            d=q[0][1]
                            mo=re.search(r'wedge type="(\w+)" number="(\d+)"', d)
                            if mo and mo.group(1)=='stop' and mo.group(2) in opened: break
                            W(q.pop(0)[1])
                            if mo and mo.group(1)!='stop': opened.add(mo.group(2))
                    opened=set()
                    if e['on']>t:
                        r=rest_xml(e['on']-t, staff+1, vnum)
                        if r: W(r)
                        t=e['on']
                    for gr in graces.get(id(e),[]):
                        for ln in note_xml(m, gr, staff+1, vnum, alter_state, grace=True,
                                           page=pg): W(ln)
                    for ln in note_xml(m, e, staff+1, vnum, alter_state, page=pg): W(ln)
                    t = e['on']+F(e['dur']).limit_denominator(48)
                if t < L:
                    r=rest_xml(L-t, staff+1, vnum)
                    if r: W(r)
                if li==0:
                    # a hairpin left over with no note under it would draw as a dot
                    tail=[d for _,d in q]
                    starts={re.search(r'number="(\d+)"', d).group(1)
                            for d in tail if 'wedge type=' in d and 'stop' not in d}
                    for d in tail:
                        mo=re.search(r'wedge type="\w+" number="(\d+)"', d)
                        if mo and mo.group(1) in starts: continue
                        W(d)
                    q[:] = []
            if not lanes:
                vnum+=1
                if vnum>1: W('      <backup><duration>%d</duration></backup>'%tick(L))
                for _,d in pend[staff+1]: W(d)
                W(rest_xml(L, staff+1, vnum))
        W('    </measure>')
    W('  </part>')
    W('</score-partwise>')
    return '\n'.join(out), ms

PIECES=[]
for _v,_n in TYPES:
    PIECES.append((_v,_n,0)); PIECES.append((_v*F(3,2),_n,1)); PIECES.append((_v*F(7,4),_n,2))
PIECES.sort(key=lambda p:-p[0])

def split_rest(dur):
    """a gap has to come out as rests that are real note values, or a renderer
    draws a question mark where the rest should be"""
    left=F(dur); out=[]
    while left > 0:
        for v,n,d in PIECES:
            if v <= left + F(1,1000):
                out.append((v,n,d)); left -= v; break
        else:
            break
    return out

def rest_xml(dur, staff, voice):
    lines=[]
    for v,n,d in split_rest(dur):
        lines.append('      <note><rest/><duration>%d</duration><voice>%d</voice>'
                     '<type>%s</type>%s<staff>%d</staff></note>'
                     %(tick(v), voice, n, '<dot/>'*d, staff))
    return '\n'.join(lines) if lines else ''


def note_xml(m, e, staff, voice, alter_state, grace=False, page=0):
    lines=[]
    if e['kind']=='rest':
        lines.append(rest_xml(e['dur'], staff, voice)); return lines
    dur=F(e['dur']).limit_denominator(48)
    base=F(e['base']).limit_denominator(48)
    if e['dots']: base = base / (F(2)-F(1,2)**e['dots'])
    first=True
    for n in sorted(e['notes'], key=lambda n:-n['y']):
        step,octv,st = pitchof(m, n)
        # a note beamed across to the other staff keeps its own staff
        nstaff = 1 if st is m['top'] else 2
        if (OTTAVA['page']==page and OTTAVA['x0'] <= n['x'] <= OTTAVA['x1']
                and OTTAVA['ylo'] < m['top'].bottom < OTTAVA['yhi'] and nstaff==1):
            octv += 1
        key=(step,octv)
        acc=n.get('alter')
        if acc is None: alter = alter_state.get(key, 0)
        else: alter = acc; alter_state[key]=acc
        lines.append('      <note>')
        if grace: lines.append('        <grace slash="yes"/>')
        if not first: lines.append('        <chord/>')
        lines.append('        <pitch><step>%s</step>%s<octave>%d</octave></pitch>'
                     %(step, ('<alter>%d</alter>'%alter) if alter else '', octv))
        if not grace:
            lines.append('        <duration>%d</duration>'%tick(dur))
        if n.get('tie_stop'):  lines.append('        <tie type="stop"/>')
        if n.get('tie_start'): lines.append('        <tie type="start"/>')
        lines.append('        <voice>%d</voice>'%voice)
        lines.append('        <type>%s</type>'%ntype(base))
        for _ in range(e['dots']): lines.append('        <dot/>')
        if acc is not None:
            lines.append('        <accidental>%s</accidental>'%ALTERNAME[acc])
        if e['tuplet'] and not grace:
            a,b=e['tuplet']
            lines.append('        <time-modification><actual-notes>%d</actual-notes>'
                         '<normal-notes>%d</normal-notes></time-modification>'%(a,b))
        lines.append('        <staff>%d</staff>'%nstaff)
        tied=[]
        if n.get('tie_stop'):  tied.append('          <tied type="stop"/>')
        if n.get('tie_start'): tied.append('          <tied type="start"/>')
        if tied and not (first and e['artic']):
            lines.append('        <notations>')
            for tl in tied: lines.append(tl)
            lines.append('        </notations>')
        if first and e['artic']:
            arts=[a for a in e['artic'] if a!='fermata']
            lines.append('        <notations>')
            for tl in tied: lines.append(tl)
            if 'fermata' in e['artic']: lines.append('          <fermata/>')
            if arts:
                lines.append('          <articulations>')
                for a in arts: lines.append('            <%s/>'%a)
                lines.append('          </articulations>')
            lines.append('        </notations>')
        lines.append('      </note>')
        first=False
    return lines

if __name__=='__main__':
    import sys
    out = sys.argv[1] if len(sys.argv)>1 else 'out.musicxml'
    xml, ms = emit()
    open(out,'w').write(xml)
    print('measures', len(ms), 'bytes', len(xml))
