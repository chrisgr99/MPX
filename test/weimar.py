"""Phrasing in 456 transcribed jazz solos, from the Weimar Jazz Database.

  python3 test/weimar.py [path to wjazzd.db]

The database is not in this repository. It is 42.5 MB, from
https://jazzomat.hfm-weimar.de/download/downloads/wjazzd.db, under the Open Database Licence, and
is expected at ~/Documents/MusicResearch/weimar/wjazzd.db unless a path is given.

WHAT A PHRASE IS HERE. The transcribers marked phrases in every solo: the units a player
performs in one gesture, separated by the places they stop. In an improvised solo that is the
breath group, so these figures are compared with mpxPhrase's sub-phrases, not with its
cadence-to-cadence phrases.

WHAT THEY ARE NOT. These are improvised horn and piano solos over jazz changes, not sung
melodies. They are busier than songs and their phrases carry more notes. Figures in beats and
bars are more likely to transfer to song-like lines than figures in notes.
"""
import os, sqlite3, statistics as st, sys
from collections import Counter, defaultdict

DB = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    '~/Documents/MusicResearch/weimar/wjazzd.db')


def pct(n, d):
    return 100.0 * n / d if d else 0.0


def show(title, counter, order=None, total=None):
    total = total if total is not None else sum(counter.values())
    print(f"\n{title}")
    keys = order if order else sorted(counter, key=lambda k: -counter[k])
    for k in keys:
        print(f"  {str(k):34s} {counter[k]:6d}  {pct(counter[k], total):5.1f}%")


def main():
    con = sqlite3.connect(DB)
    solos = {r[0]: r for r in con.execute(
        "select melid, avgtempo, tempoclass, rhythmfeel, style, instrument, title, performer "
        "from solo_info")}
    notes = defaultdict(list)
    for r in con.execute("select melid, onset, duration, bar, beat, tatum, division, num, denom, "
                         "beatdur from melody order by melid, eventid"):
        notes[r[0]].append(dict(onset=r[1], dur=r[2], bar=r[3], beat=r[4], tatum=r[5],
                                division=r[6], num=r[7], denom=r[8], beatdur=r[9]))
    phrases = defaultdict(list)
    for r in con.execute("select melid, start, end from sections where type='PHRASE' "
                         "order by melid, start"):
        phrases[r[0]].append((r[1], r[2]))
    # Chord changes, by onset time in seconds.
    changes = defaultdict(list)
    for melid, rows in [(m, list(con.execute(
            "select onset, chord from beats where melid=? order by onset", (m,)))) for m in solos]:
        prev = None
        for onset, chord in rows:
            if chord and chord != prev:
                changes[melid].append(onset)
                prev = chord

    lengths_notes, lengths_beats, lengths_secs = [], [], []
    by_tempo = defaultdict(lambda: {'beats': [], 'secs': []})
    gap_onset_beats, gap_silence_beats, gap_silence_secs = [], [], []
    starts, ends = Counter(), Counter()
    last_long = 0
    change_at_end = change_any = total_notes_for_baseline = 0
    phrase_count = 0

    def beat_position(n):
        """Beats from the start of the bar, as a fraction."""
        return (n['beat'] - 1) + (n['tatum'] - 1) / max(1, n['division'])

    for melid, ps in phrases.items():
        ns = notes[melid]
        if not ns:
            continue
        tempoclass = solos[melid][2]
        ch = changes.get(melid, [])
        for i, (a, b) in enumerate(ps):
            if a < 0 or b >= len(ns) or b < a:
                continue
            phrase_count += 1
            first, last = ns[a], ns[b]
            beatdur = st.median([n['beatdur'] for n in ns[a:b + 1] if n['beatdur']]) or 0.5
            span_secs = (last['onset'] + last['dur']) - first['onset']
            lengths_notes.append(b - a + 1)
            lengths_beats.append(span_secs / beatdur)
            lengths_secs.append(span_secs)
            by_tempo[tempoclass]['beats'].append(span_secs / beatdur)
            by_tempo[tempoclass]['secs'].append(span_secs)

            # START: on the downbeat of a bar, on another beat, off the beat, or a pickup — a first
            # note in the second half of a bar whose phrase carries on into the next bar.
            pos = beat_position(first)
            num = first['num'] or 4
            whole = abs(pos - round(pos)) < 1e-6
            crosses = ns[min(b, a + 1)]['bar'] > first['bar'] if b > a else False
            if whole and round(pos) == 0:
                starts['on the downbeat'] += 1
            elif pos >= num / 2.0 and crosses:
                starts['pickup into the next bar'] += 1
            elif whole:
                starts['on another beat'] += 1
            else:
                starts['off the beat'] += 1

            lpos = beat_position(last)
            lnum = last['num'] or 4
            lwhole = abs(lpos - round(lpos)) < 1e-6
            if lwhole and round(lpos) == 0:
                ends['beat 1'] += 1
            elif lwhole and lnum % 2 == 0 and round(lpos) == lnum // 2:
                ends['mid-bar (beat 3 in 4/4)'] += 1
            elif lwhole:
                ends['another beat'] += 1
            else:
                ends['off the beat'] += 1

            if b - a >= 2:
                before = [n['dur'] for n in ns[a:b]]
                if last['dur'] > 1.49 * st.median(before):
                    last_long += 1

            # A chord change within a beat of the phrase's last note, against every note.
            t = last['onset']
            if any(abs(c - t) <= beatdur for c in ch):
                change_at_end += 1

            if i + 1 < len(ps):
                na = ps[i + 1][0]
                if 0 <= na < len(ns):
                    nxt = ns[na]
                    gap_onset_beats.append((nxt['onset'] - last['onset']) / beatdur)
                    silence = nxt['onset'] - (last['onset'] + last['dur'])
                    gap_silence_beats.append(max(0.0, silence) / beatdur)
                    gap_silence_secs.append(max(0.0, silence))

        for n in ns:
            total_notes_for_baseline += 1
            if any(abs(c - n['onset']) <= (n['beatdur'] or 0.5) for c in ch):
                change_any += 1

    print(f"{len(phrases)} solos, {phrase_count} phrases, "
          f"{sum(len(v) for v in notes.values())} notes")

    def bucket_beats(x):
        if x < 2: return 'under 2 beats'
        if x < 4: return '2 to 4 beats'
        if x < 8: return '1 to 2 bars (4-8 beats)'
        if x < 12: return '2 to 3 bars'
        if x < 16: return '3 to 4 bars'
        return '4 bars or more'
    order_b = ['under 2 beats', '2 to 4 beats', '1 to 2 bars (4-8 beats)', '2 to 3 bars',
               '3 to 4 bars', '4 bars or more']

    print(f"\nPHRASE LENGTH")
    print(f"  notes:   median {st.median(lengths_notes):.0f}, "
          f"middle half {sorted(lengths_notes)[len(lengths_notes)//4]} to "
          f"{sorted(lengths_notes)[3*len(lengths_notes)//4]}")
    print(f"  beats:   median {st.median(lengths_beats):.1f}")
    print(f"  seconds: median {st.median(lengths_secs):.1f}, "
          f"middle half {sorted(lengths_secs)[len(lengths_secs)//4]:.1f} to "
          f"{sorted(lengths_secs)[3*len(lengths_secs)//4]:.1f}")
    show("phrase length in beats", Counter(bucket_beats(x) for x in lengths_beats), order_b)

    print(f"\nPHRASE LENGTH BY TEMPO — if the limit were in bars, beats would stay level and "
          f"seconds would fall as tempo rises; if it were breath, seconds would stay level")
    for tc in ['SLOW', 'MEDIUM SLOW', 'MEDIUM', 'MEDIUM UP', 'UP']:
        if tc in by_tempo and by_tempo[tc]['beats']:
            d = by_tempo[tc]
            print(f"  {tc:12s} phrases {len(d['beats']):5d}   median {st.median(d['beats']):5.1f} beats"
                  f"   {st.median(d['secs']):4.1f} seconds")

    show("GAP BETWEEN PHRASES: from the last note starting to the next phrase's first note",
         Counter(bucket_beats(x) for x in gap_onset_beats), order_b)
    show("SILENCE BETWEEN PHRASES: from the last note ending to the next phrase's first note",
         Counter(bucket_beats(x) for x in gap_silence_beats), order_b)
    print(f"  silence median {st.median(gap_silence_beats):.1f} beats, "
          f"{st.median(gap_silence_secs):.2f} seconds")

    show("HOW A PHRASE STARTS", starts)
    show("WHERE ITS LAST NOTE FALLS", ends)
    print(f"\nlast note longer than the notes before it: {last_long} of {phrase_count} "
          f"({pct(last_long, phrase_count):.0f}%)")
    print(f"a chord change within a beat of the last note: {pct(change_at_end, phrase_count):.0f}%"
          f"   against any note: {pct(change_any, total_notes_for_baseline):.0f}%")


if __name__ == '__main__':
    main()
