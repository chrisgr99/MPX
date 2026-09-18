"""Swing and triplets in 456 transcribed jazz solos, from the Weimar Jazz Database.

  python3 test/swing.py [path to wjazzd.db]

The database is not in this repository. It is 42.5 MB, from
https://jazzomat.hfm-weimar.de/download/downloads/wjazzd.db, under the Open Database Licence, and
is expected at ~/Documents/MusicResearch/weimar/wjazzd.db unless a path is given.

TWO QUESTIONS, both for mpxPhrase's feel.

HOW MUCH DO PLAYERS SWING, AND DOES IT DEPEND ON THE TEMPO. Measured as the beat-upbeat ratio:
within one beat divided in two, how long the first half lasts against the second. One is even,
two is a full triplet feel, and anything between is a partial swing. If the ratio falls as the
tempo rises, a swing control set once cannot serve every tempo and the module has to flatten it
as the beat gets quicker.

HOW OFTEN IS THE MIDDLE OF THE TRIPLET USED. On a swung line the eighth-note pair is the first
and third unit of a triplet, so a triplet figure is the line also sounding the middle unit that
swing skips. This counts how often that happens, and whether it happens in runs — which decides
whether the control is a per-note likelihood or a per-group one.
"""
import os, sqlite3, statistics as st, sys
from collections import Counter, defaultdict

DB = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    '~/Documents/MusicResearch/weimar/wjazzd.db')

# A pair whose ratio is outside this is a transcription artefact or a mis-assigned tatum rather
# than a way of playing: nobody swings at seven to one.
BUR_MIN, BUR_MAX = 0.4, 4.0


def pct(n, d):
    return 100.0 * n / d if d else 0.0


def quartiles(xs):
    xs = sorted(xs)
    return xs[len(xs) // 4], st.median(xs), xs[3 * len(xs) // 4]


def main():
    con = sqlite3.connect(DB)
    solos = {r[0]: dict(tempo=r[1], tempoclass=r[2], feel=r[3], style=r[4], instrument=r[5])
             for r in con.execute("select melid, avgtempo, tempoclass, rhythmfeel, style, "
                                  "instrument from solo_info")}

    # Beat onsets, so a beat's two halves can be measured against the beat that follows.
    beats = defaultdict(dict)
    for melid, bar, beat, onset in con.execute(
            "select melid, bar, beat, onset from beats order by melid, onset"):
        beats[melid][(bar, beat)] = onset

    notes = defaultdict(list)
    for r in con.execute("select melid, onset, duration, bar, beat, tatum, division, beatdur "
                         "from melody order by melid, eventid"):
        notes[r[0]].append(dict(onset=r[1], dur=r[2], bar=r[3], beat=r[4], tatum=r[5],
                                division=r[6], beatdur=r[7]))

    burs = []
    levels = defaultdict(list)
    soloMedians = []          # one row per solo: tempo, feel, and its median ratio per level
    by_feel = defaultdict(list)

    div_counts = Counter()
    beats_sounded = 0
    beats_with_middle = 0
    beats_in_three = 0
    triplet_runs = Counter()
    solos_with_triplets = 0
    triplet_note_share = []

    for melid, ns in notes.items():
        if melid not in solos:
            continue
        info = solos[melid]
        bmap = beats[melid]
        perSolo = defaultdict(list)

        # ---- SWING, AT WHATEVER LEVEL THE PLAYER IS SWINGING ---------------------------
        #
        # A beat divided in two is swung at the eighth-note level: first note, second note, next
        # beat. A beat divided in four is swung at the sixteenth level, and gives two ratios, one
        # for each half of the beat. Measuring only the first level and calling the answer "swing"
        # is what made slow tempos look even: at sixty-five beats a minute almost no beat is
        # divided in two, so the few that are were nearly all straight.
        byBeat = defaultdict(list)
        for n in ns:
            byBeat[(n['bar'], n['beat'])].append(n)
        for key, group in byBeat.items():
            group.sort(key=lambda n: n['tatum'])
            bar, beat = key
            nxt = bmap.get((bar, beat + 1)) or bmap.get((bar + 1, 1))
            if nxt is None:
                continue
            div = group[0]['division']
            tatums = [n['tatum'] for n in group]
            pairs = []
            if div == 2 and tatums == [1, 2]:
                pairs.append(('eighths', group[0], group[1], nxt))
            elif div == 4 and tatums == [1, 2, 3, 4]:
                pairs.append(('sixteenths', group[0], group[1], group[2]['onset']))
                pairs.append(('sixteenths', group[2], group[3], nxt))
            for level, a, b, endOnset in pairs:
                first = b['onset'] - a['onset']
                second = endOnset - b['onset']
                if first <= 0 or second <= 0:
                    continue
                bur = first / second
                if not (BUR_MIN <= bur <= BUR_MAX):
                    continue
                perSolo[level].append(bur)
                burs.append(bur)
                levels[level].append(bur)

        # ---- TRIPLETS: the middle unit -------------------------------------------------------
        triplet_beats = set()
        for n in ns:
            div_counts[n['division']] += 1
            if n['division'] == 3:
                if n['tatum'] == 2:
                    triplet_beats.add((n['bar'], n['beat']))
        beats_sounded += len(byBeat)
        for key, group in byBeat.items():
            if any(n['division'] == 3 for n in group):
                beats_in_three += 1
        beats_with_middle += len(triplet_beats)
        trip_notes = sum(1 for n in ns if n['division'] == 3)
        if trip_notes:
            solos_with_triplets += 1
        triplet_note_share.append(pct(trip_notes, len(ns)))

        # RUNS: consecutive beats using the middle unit. A control that is a per-note likelihood
        # would scatter them; if real ones come in runs, it belongs to the group instead.
        ordered = sorted(triplet_beats)
        run = 0
        prev = None
        for bar, beat in ordered:
            if prev and ((bar, beat - 1) == prev or (beat == 1 and bar - 1 == prev[0])):
                run += 1
            else:
                if run:
                    triplet_runs[min(run, 6)] += 1
                run = 1
            prev = (bar, beat)
        if run:
            triplet_runs[min(run, 6)] += 1

        # ONE ROW PER SOLO. A solo with two thousand notes and one with fifty say the same amount
        # about how much players swing, so the figures below are medians of solo medians.
        row = dict(tempo=info['tempo'], feel=info['feel'], instrument=info['instrument'])
        for level, rs in perSolo.items():
            if len(rs) >= 20:
                row[level] = st.median(rs)
        if 'eighths' in row or 'sixteenths' in row:
            soloMedians.append(row)
        for level, rs in perSolo.items():
            if len(rs) >= 20 and info['feel']:
                by_feel[(info['feel'], level)].extend(rs)

    print(f"{len(notes)} solos, {sum(len(v) for v in notes.values())} notes")
    print(f"{len(levels['eighths'])} swung eighth pairs, "
          f"{len(levels['sixteenths'])} swung sixteenth pairs, "
          f"{len(soloMedians)} solos with at least twenty of either")

    print(f"\nSWING RATIO — 1.0 is even, 2.0 is a full triplet feel. One figure per solo, so a "
          f"long solo does not outvote fifty short ones")
    for level in ['eighths', 'sixteenths']:
        rows = [r[level] for r in soloMedians if level in r]
        if not rows:
            continue
        l, m, h = quartiles(rows)
        print(f"  at the {level[:-1]}-note level: solos {len(rows):4d}   median {m:.2f}   "
              f"middle half {l:.2f} to {h:.2f}")

    print(f"\nBY TEMPO, AT THE LEVEL BEING SWUNG — the ratio a player uses at that tempo")
    print(f"  {'beats a minute':16s} {'solos':>6s}  {'eighths':>9s}  {'sixteenths':>11s}")
    buckets = defaultdict(lambda: defaultdict(list))
    for r in soloMedians:
        if not r['tempo']:
            continue
        k = min(int(r['tempo'] // 40) * 40, 320)
        for level in ['eighths', 'sixteenths']:
            if level in r:
                buckets[k][level].append(r[level])
    for k in sorted(buckets):
        e = buckets[k]['eighths']
        x = buckets[k]['sixteenths']
        if len(e) + len(x) < 5:
            continue
        es = f"{st.median(e):.2f} ({len(e)})" if e else "         "
        xs = f"{st.median(x):.2f} ({len(x)})" if x else ""
        print(f"  {k:3d} to {k + 39:3d}      {len(e) + len(x):6d}  {es:>9s}  {xs:>11s}")

    print(f"\nBY THE FEEL THE TRANSCRIBERS NAMED, at the eighth-note level")
    for (feel, level), rs in sorted(by_feel.items(), key=lambda kv: -len(kv[1])):
        if level != 'eighths' or len(rs) < 200:
            continue
        print(f"  {str(feel):14s} pairs {len(rs):6d}   ratio median {st.median(rs):.2f}")

    print(f"\nTRIPLETS")
    total_notes = sum(div_counts.values())
    for d in sorted(div_counts, key=lambda d: -div_counts[d])[:6]:
        print(f"  beat divided in {d:2d}: {div_counts[d]:7d} notes  "
              f"{pct(div_counts[d], total_notes):5.1f}%")
    print(f"  beats carrying a note at all: {beats_sounded}")
    print(f"  beats carrying a triplet figure: {beats_in_three} "
          f"({pct(beats_in_three, beats_sounded):.0f}% of beats played)")
    print(f"  of those, beats sounding the MIDDLE unit: {beats_with_middle} "
          f"({pct(beats_with_middle, beats_in_three):.0f}% of them, "
          f"{pct(beats_with_middle, beats_sounded):.1f}% of beats played)")
    print(f"  the rest are the swung pair written as a triplet: first and third unit only")
    print(f"  solos using at least one: {solos_with_triplets} of {len(notes)}")
    l, m, h = quartiles(triplet_note_share)
    print(f"  share of a solo's notes on a triplet division: median {m:.1f}%, "
          f"middle half {l:.1f}% to {h:.1f}%")

    print(f"\nDO THEY COME IN RUNS — consecutive beats using the middle unit")
    runs_total = sum(triplet_runs.values())
    for k in sorted(triplet_runs):
        label = f"{k} beats" if k < 6 else "6 beats or more"
        print(f"  {label:16s} {triplet_runs[k]:6d}  {pct(triplet_runs[k], runs_total):5.1f}%")


if __name__ == '__main__':
    main()
