#!/usr/bin/env python3
"""How the melody's rhythm follows the chords, in the pop transcriptions that have chords.

  python3 research/pop/harmony_rhythm.py

For each chord: how long it lasts and how many notes start under it; for each chord change: whether
a note starts on it, or just before it and is held across it (an anticipation), and how long that
note is against the notes around it; and over diminished and short passing chords, how the melody
behaves there.
"""
import glob, json, os, re, statistics as st, collections

HERE = os.path.dirname(os.path.abspath(__file__))

ROOTS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def parse_chord(sym):
    """Root pitch class, and the chord's pitch classes, from a plain chord symbol."""
    m = re.match(r"^([A-G])([#b]?)(.*)$", sym.strip())
    if not m:
        return None, set()
    root = (ROOTS[m.group(1)] + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0)) % 12
    q = m.group(3).split("/")[0]
    if q.startswith("dim7") or q.startswith("o7") or q.startswith("°7"):
        iv = [0, 3, 6, 9]
    elif q.startswith("dim") or q.startswith("o") or q.startswith("°"):
        iv = [0, 3, 6]
    elif q.startswith("m7b5") or q.startswith("ø"):
        iv = [0, 3, 6, 10]
    elif q.startswith("maj"):
        iv = [0, 4, 7, 11]
    elif q.startswith("m") and not q.startswith("maj"):
        iv = [0, 3, 7] + ([10] if "7" in q else [])
    elif q.startswith("sus"):
        iv = [0, 5, 7]
    elif q.startswith("5"):
        iv = [0, 7]
    elif q.startswith("6"):
        iv = [0, 4, 7, 9]
    elif q.startswith("7") or q.startswith("9") or q.startswith("13") or q.startswith("11"):
        iv = [0, 4, 7, 10]
    elif q.startswith("aug") or q.startswith("+"):
        iv = [0, 4, 8]
    else:
        iv = [0, 4, 7]
    return root, {(root + i) % 12 for i in iv}


def diminished(sym):
    q = re.sub(r"^[A-G][#b]?", "", sym).split("/")[0]
    return q.startswith(("dim", "o", "°"))


songs = []
for f in sorted(glob.glob(os.path.join(HERE, "*.json"))):
    d = json.load(open(f))
    if "chords" in d and d["chords"]:
        songs.append(d)
print("songs with chords:", len(songs))

per_chord = collections.defaultdict(list)     # chord length (beats, rounded) -> notes per beat
changes = on = antic = none_ = 0
change_note_len, other_len = [], []
dim_notes = dim_beats = 0
dim_len, dim_ct, dim_ct_held, dim_held = [], 0, 0, 0
short_notes = short_beats = 0
long_notes = long_beats = 0
into_change_step = into_change_n = 0

for d in songs:
    bb = d["meter"][0] * 4.0 / d["meter"][1]
    notes = sorted([(n["bar"] * bb + n["beat"], n["dur"], n["pitch"]) for p in d["phrases"] for n in p["notes"]])
    if not notes:
        continue
    first, last = notes[0][0], notes[-1][0] + notes[-1][1]
    ch = sorted([(c["bar"] * bb + c["beat"], c["symbol"]) for c in d["chords"]])
    # only the span the melody covers
    spans = []
    for i, (t, sym) in enumerate(ch):
        end = ch[i + 1][0] if i + 1 < len(ch) else last
        s, e = max(t, first), min(end, last)
        if e > s + 1e-6 and sym.upper() != "N.C.":
            spans.append((s, e, sym, t))
    for s, e, sym, t0 in spans:
        inside = [n for n in notes if s - 1e-6 <= n[0] < e - 1e-6]
        length = e - s
        per_chord[min(8, round(length))].append(len(inside) / length)
        root, tones = parse_chord(sym)
        if diminished(sym):
            dim_notes += len(inside); dim_beats += length
            for n in inside:
                dim_len.append(n[1])
                isct = n[2] % 12 in tones
                dim_ct += isct
                if n[1] >= 0.75:
                    dim_held += 1; dim_ct_held += isct
        if length <= 2.01:
            short_notes += len(inside); short_beats += length
        elif length >= 3.99:
            long_notes += len(inside); long_beats += length
    # chord changes inside the melody's span
    for i in range(1, len(ch)):
        t = ch[i][0]
        if not (first < t < last) or ch[i][1] == ch[i - 1][1]:
            continue
        changes += 1
        starts = [n for n in notes if abs(n[0] - t) < 0.13]
        held = [n for n in notes if n[0] < t - 0.13 and n[0] + n[1] > t + 0.13 and t - n[0] <= 1.01]
        if starts:
            on += 1
            change_note_len.append(starts[0][1])
            k = notes.index(starts[0])
            if k > 0:
                into_change_n += 1
                into_change_step += 1 <= abs(starts[0][2] - notes[k - 1][2]) <= 2
        elif held:
            antic += 1
        else:
            none_ += 1
    changeset = {c[0] for c in ch}
    for n in notes:
        if all(abs(n[0] - t) >= 0.13 for t in changeset):
            other_len.append(n[1])

q = lambda v: "%.2f/%.2f/%.2f" % tuple(st.quantiles(v, n=4)) if len(v) >= 4 else str(v)
print("\nNOTES PER BEAT BY HOW LONG THE CHORD LASTS (median)")
for k in sorted(per_chord):
    v = per_chord[k]
    print("  %d beats: %.2f notes a beat  (%d chords)" % (k, st.median(v), len(v)))
print("  chords of two beats or less: %.2f notes a beat; four beats or more: %.2f" % (
    short_notes / max(1e-9, short_beats), long_notes / max(1e-9, long_beats)))
print("\nAT A CHORD CHANGE (%d changes)" % changes)
print("  a note starts on it: %.0f%%   one before it is held across it: %.0f%%   neither: %.0f%%" % (
    100 * on / changes, 100 * antic / changes, 100 * none_ / changes))
print("  length of the note starting on a change %s against other notes %s (quartiles, beats)" % (
    q(change_note_len), q(other_len)))
print("  arrived at by step (1 or 2 semitones from the note before): %.0f%%" % (
    100 * into_change_step / max(1, into_change_n)))
if dim_beats:
    print("\nOVER DIMINISHED CHORDS: %.2f notes a beat over %.0f beats; note length %s; chord tones %.0f%%; held notes %d, chord tones among them %d" % (
        dim_notes / dim_beats, dim_beats, q(dim_len), 100 * dim_ct / max(1, dim_notes), dim_held, dim_ct_held))
