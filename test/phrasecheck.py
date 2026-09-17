"""Checks the chart's phrasing rule against where real melodies breathe.

  python3 test/phrasecheck.py

A MIRROR, NOT THE RULE. The rule itself is chartPhrases in src/ChartLayout.cpp. This applies the
same rule to the chord symbols in the MusicXML lead sheets unpacked into /tmp/leads, then asks how
often a phrase end found that way falls where the melody actually breathes. If the two ever
disagree, the C++ is the truth and this file is wrong; the constants are printed so they can be
compared by eye.

THE BASELINE is every bar line. A phrase end that coincides with a breath no more often than an
arbitrary bar line does is not finding phrases at all.

Several variants of the acceptance rule are run side by side, so a threshold can be chosen by
evidence rather than by preference.
"""
import glob, importlib.util, os, statistics as st
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sp", os.path.join(HERE, "subphrase.py"))
sp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sp)

FALLBACK = 4
DOMINANT = {'major', 'dominant', 'dominant-ninth', 'dominant-11th', 'dominant-13th',
            'suspended-fourth', 'augmented-seventh'}
# Semitones above the tonic, as the degree and accidental the chart stores.
DEGREE = {0: (1, 0), 1: (2, -1), 2: (2, 0), 3: (3, -1), 4: (3, 0), 5: (4, 0), 6: (5, -1),
          7: (5, 0), 8: (6, -1), 9: (6, 0), 10: (7, -1), 11: (7, 0)}


def cadence(frm, to, held, bar_len, tonic):
    fd = DEGREE[(frm[1] - tonic) % 12]
    td = DEGREE[(to[1] - tonic) % 12]
    fdom = frm[2] in DOMINANT
    if td == (1, 0):
        if fd == (5, 0) and fdom: return 'authentic'
        if fd == (4, 0): return 'plagal'
        if fd == (7, -1) and fdom: return 'backdoor'
        if fd == (2, -1) and fdom: return 'tritone'
    if fd == (5, 0) and fdom and td in ((6, 0), (6, -1)):
        return 'deceptive'
    if td == (5, 0) and to[2] in DOMINANT and held >= bar_len - 1e-6:
        return 'half'
    return None


def section_starts(path, nbars):
    """Bars that open a section: a rehearsal mark, or the bar after a double bar line."""
    root = ET.parse(path).getroot()
    part = root.findall('part')[0]
    starts = {0}
    for i, m in enumerate(part.findall('measure')):
        if m.find('.//rehearsal') is not None:
            starts.add(i)
        for bl in m.findall('barline'):
            style = bl.findtext('bar-style') or ''
            if bl.get('location', 'right') == 'right' and style in ('light-light', 'light-heavy'):
                if i + 1 < nbars:
                    starts.add(i + 1)
    return sorted(starts)


def phrase_ends(path, bars, chords, tonic, variant):
    """Bar indices at which phrases end, by the given variant of the acceptance rule."""
    nb = len(bars)
    if not chords:
        return []
    # Changes, deduplicated.
    ch = []
    for c in sorted(chords):
        if ch and ch[-1][1] == c[1] and ch[-1][2] == c[2]:
            continue
        ch.append(c)
    total_end = bars[-1][1]

    def bar_of(t):
        return sp.bar_at(bars, t)[0]

    cands = []
    for i in range(1, len(ch)):
        held = (ch[i + 1][0] if i + 1 < len(ch) else total_end) - ch[i][0]
        b = bar_of(ch[i][0])
        bar_len = bars[b][2] * 4.0 / bars[b][3]
        k = cadence(ch[i - 1], ch[i], held, bar_len, tonic)
        if k is None or k == 'deceptive':
            continue
        end_bar = bar_of(ch[i][0] + held - 1e-3) + 1
        cands.append((b, end_bar, held >= bar_len - 1e-6, k))

    ends = []
    secs = section_starts(path, nb)
    for si, s in enumerate(secs):
        e = secs[si + 1] if si + 1 < len(secs) else nb
        last = s
        accepted = []
        for (ab, eb, held, k) in cands:
            if not (s <= ab < e):
                continue
            eb = min(eb, e)
            if eb <= last:
                continue
            closes = eb == e
            far = eb - last >= variant['min']
            ok = closes or far or (variant['held'] and held and eb - last >= variant['held_min'])
            if ok:
                accepted.append((eb, k)); last = eb
        if last < e:
            accepted.append((e, None))
        frm = s
        for (eb, k) in accepted:
            whole = k is None
            at = frm
            while True:
                rem = eb - at
                cut = rem > FALLBACK if whole else rem > 2 * FALLBACK
                if not cut or rem - FALLBACK < variant['min']:
                    break
                at += FALLBACK
                ends.append(at)
            ends.append(eb)
            frm = eb
    return sorted(set(ends))


def main():
    variants = [
        {'name': 'held tonic bypasses the 2-bar minimum', 'min': 2, 'held': True, 'held_min': 1},
        {'name': 'AS BUILT: closes section, or 2+ bars', 'min': 2, 'held': False, 'held_min': 0},
        {'name': '3-bar minimum', 'min': 3, 'held': False, 'held_min': 0},
        {'name': '4-bar minimum', 'min': 4, 'held': False, 'held_min': 0},
    ]
    files = sorted(glob.glob('/tmp/leads/*.xml'))
    # Only lead sheets with chord symbols and a melody.
    data = []
    for f in files:
        notes, chords, bars, tonic = sp.parse(f)
        if len([c for c in chords]) < 8:
            continue
        sounding = [n for n in notes if not n['rest']]
        if len(sounding) < 20:
            continue
        med = st.median([n['dur'] for n in sounding])
        # EACH BREATH WITH ITS SIZE: the length of the rest after the note, or for a held note
        # running into the next, how long it was held. A phrase end should carry a bigger breath
        # than an ordinary bar line, and that is a much sharper test than whether any breath at
        # all is nearby, because melodies breathe near most bar lines.
        breath_ends = []
        for i, n in enumerate(notes):
            if n['rest']:
                continue
            nxt = notes[i + 1] if i + 1 < len(notes) else None
            if nxt is None:
                breath_ends.append((n['t'] + n['dur'], 4.0))
            elif nxt['rest'] and nxt['dur'] >= 0.5:
                rest = 0.0
                j = i + 1
                while j < len(notes) and notes[j]['rest']:
                    rest += notes[j]['dur']; j += 1
                breath_ends.append((n['t'] + n['dur'], rest + n['dur']))
            elif not nxt['rest'] and n['dur'] >= 2.0 and n['dur'] >= 2.5 * med:
                breath_ends.append((n['t'] + n['dur'], n['dur']))
        data.append((f, notes, chords, bars, tonic, breath_ends))

    def size_at(bars, bar_index, breath_ends):
        """The biggest breath ending within a beat either side of this bar line, or nought."""
        if bar_index <= 0 or bar_index > len(bars):
            return 0.0
        t = bars[bar_index - 1][1]
        beat = 4.0 / bars[bar_index - 1][3]
        near = [sz for (e, sz) in breath_ends if t - beat - 1e-6 <= e <= t + beat + 1e-6]
        return max(near) if near else 0.0

    base = []
    for (f, notes, chords, bars, tonic, be) in data:
        for bi in range(1, len(bars) + 1):
            base.append(size_at(bars, bi, be))
    def summary(xs):
        hit = sum(1 for x in xs if x > 0)
        big = sum(1 for x in xs if x >= 2.0)
        return (f"a breath within a beat {100*hit/max(1,len(xs)):3.0f}%   "
                f"a breath of a half note or more {100*big/max(1,len(xs)):3.0f}%   "
                f"mean size {st.mean(xs) if xs else 0:.2f} beats")
    print(f"{len(data)} lead sheets with chord symbols and a melody; fallback {FALLBACK} bars\n")
    print(f"every bar line ({len(base)}):")
    print(f"  {summary(base)}\n")

    for v in variants:
        at_ends = []
        lengths = []
        for (f, notes, chords, bars, tonic, be) in data:
            ends = phrase_ends(f, bars, chords, tonic, v)
            prev = 0
            for e in ends:
                lengths.append(e - prev)
                prev = e
                at_ends.append(size_at(bars, e, be))
        one = sum(1 for L in lengths if L == 1)
        print(f"{v['name']} ({len(at_ends)} ends, median {st.median(lengths):.0f} bars, "
              f"one-bar {100*one/max(1,len(lengths)):.0f}%):")
        print(f"  {summary(at_ends)}")


if __name__ == '__main__':
    main()
