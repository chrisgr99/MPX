"""Where real melodies breathe, measured from MusicXML lead sheets.

  python3 test/subphrase.py held      rests and held notes both end a sub-phrase
  python3 test/subphrase.py rests     rests alone

Reads every .xml in /tmp/leads, which is where the lead sheets are unpacked from their .mxl
archives. A sub-phrase ends at a rest of an eighth note or longer, or, with "held", at a note of
at least a half note and two and a half times the median note length that runs straight into the
next note. Lead sheets very often write a breath that way, with no rest, so rests alone make whole
choruses into one group.

For each sub-phrase it records its length, where it starts and ends in the bar, whether its last
note is long, which bar of a four-bar unit it ends in, and what the harmony is doing at its end.
Lyric punctuation is used as independent evidence that the splitting finds real breaths.
"""
import glob, os, statistics as st, xml.etree.ElementTree as ET
from collections import Counter

STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
DOMINANT = {'dominant', 'dominant-ninth', 'dominant-11th', 'dominant-13th', 'major',
            'augmented-seventh', 'suspended-fourth'}
REST_BREAK = 0.5
import sys
RULE = sys.argv[1] if len(sys.argv) > 1 else 'rests'
PUNCT = [0, 0]
SPACING = []          # quarter notes: an eighth-note rest or longer ends a sub-phrase


def bucket(b):
    if b <= 0.75: return 'under 1 bar'
    if b <= 1.25: return 'about 1 bar'
    if b <= 1.75: return '1 to 2 bars'
    if b <= 2.25: return 'about 2 bars'
    if b <= 3.25: return 'about 3 bars'
    if b <= 4.25: return 'about 4 bars'
    return 'over 4 bars'


def parse(path):
    root = ET.parse(path).getroot()
    part = root.findall('part')[0]
    divisions = 1
    beats, beatType = 4, 4
    fifths, minor = 0, False
    t = 0.0                 # quarter notes from the start
    notes, chords, bars = [], [], []
    for m in part.findall('measure'):
        start = t
        cursor = t
        longest = t
        voice_seen = None
        for el in m:
            tag = el.tag
            if tag == 'attributes':
                d = el.findtext('divisions')
                if d: divisions = float(d)
                tm = el.find('time')
                if tm is not None and tm.findtext('beats'):
                    try:
                        beats = int(tm.findtext('beats').split('+')[0])
                        beatType = int(tm.findtext('beat-type'))
                    except ValueError:
                        pass
                k = el.find('key')
                if k is not None and k.findtext('fifths') is not None:
                    fifths = int(k.findtext('fifths'))
                    minor = (k.findtext('mode') or '') == 'minor'
            elif tag == 'harmony':
                rs = el.find('root')
                if rs is None: continue
                pc = (STEP[rs.findtext('root-step')] + int(float(rs.findtext('root-alter') or 0))) % 12
                off = el.find('offset')
                at = cursor + (float(off.text) / divisions if off is not None else 0.0)
                chords.append((at, pc, el.findtext('kind') or ''))
            elif tag == 'backup':
                cursor -= float(el.findtext('duration')) / divisions
            elif tag == 'forward':
                cursor += float(el.findtext('duration')) / divisions
            elif tag == 'note':
                if el.find('grace') is not None: continue
                dur = float(el.findtext('duration') or 0) / divisions
                voice = el.findtext('voice') or '1'
                if voice_seen is None: voice_seen = voice
                if el.find('chord') is not None:
                    continue       # a simultaneous note; the melody is the first
                if voice != voice_seen:
                    cursor += dur
                    longest = max(longest, cursor)
                    continue
                is_rest = el.find('rest') is not None
                tie_stop = any(ti.get('type') == 'stop' for ti in el.findall('tie'))
                lyric = el.findtext('lyric/text') or ''
                if not is_rest and tie_stop and notes and not notes[-1]['rest']:
                    notes[-1]['dur'] += dur
                    if lyric: notes[-1]['lyric'] = lyric
                else:
                    notes.append({'t': cursor, 'dur': dur, 'rest': is_rest, 'lyric': lyric})
                cursor += dur
                longest = max(longest, cursor)
        barLen = beats * 4.0 / beatType
        t = max(longest, start + (longest - start))
        if t <= start:
            t = start + barLen
        bars.append((start, t, beats, beatType))
    tonic = (fifths * 7) % 12
    if minor:
        tonic = (tonic + 9) % 12
    return notes, chords, bars, tonic


def bar_at(bars, t):
    for i, (s, e, b, bt) in enumerate(bars):
        if s - 1e-6 <= t < e - 1e-6:
            return i, s, e, b, bt
    s, e, b, bt = bars[-1]
    return len(bars) - 1, s, e, b, bt


def chord_at(chords, t):
    cur = None
    prev = None
    for c in chords:
        if c[0] <= t + 1e-6:
            prev, cur = cur, c
        else:
            break
    return prev, cur


def degree(pc, tonic):
    return (pc - tonic) % 12


def main():
    files = sorted(glob.glob('/tmp/leads/*.xml'))
    groups = []
    per_file = []
    cadence_arrivals = 0
    cadences_ending_group = 0
    for f in files:
        notes, chords, bars, tonic = parse(f)
        chords.sort()
        sounding = [n for n in notes if not n['rest']]
        if len(sounding) < 20:
            continue
        # Is the first bar a pickup? It is if it is shorter than a full bar.
        s0, e0, b0, bt0 = bars[0]
        full0 = b0 * 4.0 / bt0
        pickup_bar = (e0 - s0) < full0 - 1e-6
        # Split into groups. RULE picks the boundary test: rests alone, or rests and held notes.
        # Lead sheets very often write a breath as a long held note running straight into the
        # next phrase, with no rest, so rests alone make a whole chorus one group.
        med = st.median([n['dur'] for n in sounding])
        gs, cur = [], []
        for i, n in enumerate(notes):
            if n['rest']:
                if n['dur'] >= REST_BREAK - 1e-6 and cur:
                    gs.append(cur); cur = []
                continue
            cur.append(n)
            if RULE == 'held':
                nxt = notes[i + 1] if i + 1 < len(notes) else None
                if nxt is not None and not nxt['rest'] and n['dur'] >= 2.0 - 1e-6 \
                        and n['dur'] >= 2.5 * med:
                    gs.append(cur); cur = []
        if cur: gs.append(cur)
        name = os.path.basename(f)[:34]
        lengths = []
        for g in gs:
            first, last = g[0], g[-1]
            start_t, end_t = first['t'], last['t'] + last['dur']
            bi, bs, be, beats, bt = bar_at(bars, start_t)
            barLen = beats * 4.0 / bt
            beatLen = 4.0 / bt
            onset_in_bar = (start_t - bs) / beatLen          # beats into the bar
            li, ls, le, lbeats, lbt = bar_at(bars, last['t'])
            lbeatLen = 4.0 / lbt
            last_in_bar = (last['t'] - ls) / lbeatLen
            # Start: on the downbeat, a pickup into the next bar, or after the downbeat.
            if abs(onset_in_bar) < 1e-3:
                start = 'on the downbeat'
            elif onset_in_bar >= beats / 2.0 and end_t > be + 1e-6:
                start = 'pickup'
            elif onset_in_bar < beats / 2.0:
                start = 'after the downbeat'
            else:
                start = 'late in a bar, not crossing'
            # End: the metric weight of the last note's onset.
            whole = abs(last_in_bar - round(last_in_bar)) < 1e-3
            if whole and int(round(last_in_bar)) % lbeats == 0:
                end = 'beat 1'
            elif whole and lbeats % 2 == 0 and int(round(last_in_bar)) == lbeats // 2:
                end = 'mid-bar (beat 3 in 4/4)'
            elif whole:
                end = 'another beat'
            else:
                end = 'off the beat'
            durs = [n['dur'] for n in g]
            last_long = len(g) >= 3 and last['dur'] > st.median(durs[:-1]) * 1.49
            # Hypermeter: which bar of a 4-bar unit, counting from the first full bar.
            first_full = 1 if pickup_bar else 0
            ei = bar_at(bars, end_t - 1e-3)[0]
            hyper = (ei - first_full) % 4 + 1 if ei >= first_full else 0
            # Harmony at the end.
            prev, curc = chord_at(chords, last['t'])
            harm = 'no chord'
            if curc is not None:
                d = degree(curc[1], tonic)
                pd = degree(prev[1], tonic) if prev else None
                recent = last['t'] - curc[0] <= barLen + 1e-6
                if d == 0 and recent and pd == 7:
                    harm = 'I, just after V'
                elif d == 0 and recent and pd in (5, 10, 1):
                    harm = 'I, just after IV, bVII or bII'
                elif d == 0:
                    harm = 'I, held'
                elif d == 7:
                    harm = 'V (half-cadence shape)'
                else:
                    harm = 'another chord'
            change_near = any(abs(c[0] - last['t']) <= beatLen + 1e-6 for c in chords)
            punct = bool(last['lyric']) and last['lyric'].rstrip()[-1:] in ',.;:!?'
            length_bars = (end_t - start_t) / barLen
            groups.append(dict(file=name, bars=length_bars, start=start, end=end,
                               last_long=last_long, hyper=hyper, harm=harm,
                               change_near=change_near, punct=punct,
                               has_lyric=bool(last['lyric']), notes=len(g),
                               start_t=start_t, barLen=barLen))
            lengths.append(length_bars)
        # LYRIC PUNCTUATION AS INDEPENDENT EVIDENCE: of every sung word ending in punctuation,
        # how many fall at the end of a group this rule found. That is recall; the earlier figure,
        # punctuation among group ends, is precision.
        ends_set = set(id(x[-1]) for x in gs)
        for x in notes:
            if not x['rest'] and x['lyric'] and x['lyric'].rstrip()[-1:] in ',.;:!?':
                PUNCT[0] += 1
                PUNCT[1] += 1 if id(x) in ends_set else 0
        # Start-to-start spacing, in bars: how often a new sub-phrase begins.
        for a, b in zip(gs, gs[1:]):
            bl = bar_at(bars, a[0]['t'])[3] * 4.0 / bar_at(bars, a[0]['t'])[4]
            SPACING.append((b[0]['t'] - a[0]['t']) / bl)
        starts = [g['t'] for g in [x[0] for x in gs]]
        per_file.append((name, len(gs), st.median(lengths) if lengths else 0))
        # How often a closing cadence arrival is followed, within a bar, by the end of a group.
        ends = [x[-1]['t'] + x[-1]['dur'] for x in gs]
        for i in range(1, len(chords)):
            d = degree(chords[i][1], tonic)
            pd = degree(chords[i - 1][1], tonic)
            if d == 0 and pd in (7, 5, 10, 1):
                cadence_arrivals += 1
                barLen = bar_at(bars, chords[i][0])[3] * 4.0 / bar_at(bars, chords[i][0])[4]
                if any(0 <= e - chords[i][0] <= barLen + 1e-6 for e in ends):
                    cadences_ending_group += 1

    n = len(groups)
    print(f"{len([p for p in per_file])} melodies, {n} sub-phrases\n")
    print("per melody: sub-phrases, median length in bars")
    for name, count, med in per_file:
        print(f"  {name:36s} {count:3d}   {med:.2f}")

    def dist(title, key, fmt=lambda k: k):
        c = Counter(key(g) for g in groups)
        print(f"\n{title}")
        for k, v in sorted(c.items(), key=lambda kv: -kv[1]):
            print(f"  {fmt(k):34s} {v:4d}  {100*v/n:5.1f}%")

    dist("length of a sub-phrase, first note to end of last", lambda g: bucket(g['bars']))
    dist("how a sub-phrase starts", lambda g: g['start'])
    dist("where its last note falls", lambda g: g['end'])
    dist("which bar of a four-bar unit it ends in", lambda g: g['hyper'],
         lambda k: f"bar {k}" if k else "the pickup bar")
    dist("the harmony at its end", lambda g: g['harm'])
    long_last = sum(g['last_long'] for g in groups)
    print(f"\nlast note longer than the rest of the group: {long_last} of {n} ({100*long_last/n:.0f}%)")
    near = sum(g['change_near'] for g in groups)
    print(f"a chord change within a beat of its last note: {near} of {n} ({100*near/n:.0f}%)")
    ly = [g for g in groups if g['has_lyric']]
    if ly:
        p = sum(g['punct'] for g in ly)
        print(f"lyric at its last note ends in punctuation: {p} of {len(ly)} ({100*p/len(ly):.0f}%)")
    if PUNCT[0]:
        print(f"punctuated lyric words that end a sub-phrase: {PUNCT[1]} of {PUNCT[0]} "
              f"({100*PUNCT[1]/PUNCT[0]:.0f}%)")
    sp = Counter(bucket(x) for x in SPACING)
    print("\nfrom the start of one sub-phrase to the start of the next")
    for k, v in sorted(sp.items(), key=lambda kv: -kv[1]):
        print(f"  {k:34s} {v:4d}  {100*v/len(SPACING):5.1f}%")
    if cadence_arrivals:
        print(f"closing cadences with a sub-phrase ending within a bar after: "
              f"{cadences_ending_group} of {cadence_arrivals} "
              f"({100*cadences_ending_group/cadence_arrivals:.0f}%)")


if __name__ == '__main__':
    main()
