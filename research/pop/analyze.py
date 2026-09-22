#!/usr/bin/env python3
"""Measures phrasing, rhythm and prosody in the pop chart transcriptions (see SCHEMA.md)."""
import glob, json, os, statistics as st, collections

HERE = os.path.dirname(os.path.abspath(__file__))
songs = []
for f in sorted(glob.glob(os.path.join(HERE, "*.json"))):
    try:
        songs.append(json.load(open(f)))
    except Exception as e:
        print("skip", os.path.basename(f), e)

def pct(a, b):
    return "%3.0f%%" % (100.0 * a / b) if b else "  -"

def med(v):
    return st.median(v) if v else float("nan")

def quart(v):
    if len(v) < 4:
        return "n/a"
    q = st.quantiles(v, n=4)
    return "%.2f / %.2f / %.2f" % (q[0], q[1], q[2])

def near(x, y, e=0.02):
    return abs(x - y) < e

bad_bars = []
phrases = []  # (song, phrase dict, absolute notes list)
for s in songs:
    bb = s["meter"][0] * 4.0 / s["meter"][1]
    s["_bb"] = bb
    for p in s["phrases"]:
        ns = [dict(n) for n in p["notes"]]
        if not ns:
            continue
        for n in ns:
            n["t"] = n["bar"] * bb + n["beat"]
            n["end"] = n["t"] + n["dur"]
        phrases.append((s, p, ns))
    # bar sanity: notes must not overlap
    allnotes = sorted((n for _, p2, ns in phrases if _ is s for n in ns), key=lambda n: n["t"])
    for a, b in zip(allnotes, allnotes[1:]):
        if a["end"] > b["t"] + 0.01:
            bad_bars.append((s["title"], a["bar"], round(a["end"] - b["t"], 2)))

N = sum(len(ns) for _, _, ns in phrases)
print("songs %d, phrases %d, notes %d" % (len(songs), len(phrases), N))
if bad_bars:
    print("overlapping notes (transcription errors?): %d, e.g. %s" % (len(bad_bars), bad_bars[:5]))

# ---------------------------------------------------------------- phrase lengths and rests
print("\nPHRASE LENGTH AND BREATH (beats, quartiles 25/50/75)")
sung = [ns[-1]["end"] - ns[0]["t"] for _, _, ns in phrases]
rest = [p["restAfter"] for _, p, _ in phrases if p.get("restAfter", 0) < 16]
notes = [len(ns) for _, _, ns in phrases]
print("  sung length      ", quart(sung))
print("  rest after       ", quart(rest))
print("  notes per phrase ", quart(notes))
span = [a + b for a, b in zip(sung, [p.get("restAfter", 0) for _, p, _ in phrases]) if b < 16]
ratio = [p.get("restAfter", 0) / (ns[-1]["end"] - ns[0]["t"] + p.get("restAfter", 0))
         for _, p, ns in phrases if p.get("restAfter", 0) < 16]
print("  sung+rest span   ", quart(span))
print("  rest share of span", quart(ratio))
hist = collections.Counter(min(16, round(x)) for x in span)
print("  span histogram (beats):", " ".join("%d:%d" % kv for kv in sorted(hist.items())))

# ---------------------------------------------------------------- where phrases start
print("\nWHERE A PHRASE STARTS (first note, position in bar)")
starts = collections.Counter()
for s, p, ns in phrases:
    bb = s["_bb"]
    b = ns[0]["beat"]
    # a pickup: starts in the last beat and a half of a bar and the phrase goes on into the next
    if b >= bb - 1.5 + 0.01 and len(ns) > 1 and ns[1]["bar"] > ns[0]["bar"] or (len(ns) > 1 and ns[-1]["bar"] > ns[0]["bar"] and b >= bb - 1.01 and b > 0):
        k = "pickup (last beat or so of the bar before)"
    elif near(b, 0):
        k = "downbeat"
    elif near(b % 1, 0):
        k = "later whole beat"
    else:
        k = "off the beat, inside the bar"
    starts[k] += 1
for k, v in starts.most_common():
    print("  %-45s %s" % (k, pct(v, len(phrases))))
pick = collections.Counter()
for s, p, ns in phrases:
    first_bar = ns[0]["bar"]
    lead = sum(1 for n in ns if n["bar"] == first_bar)
    if ns[-1]["bar"] > first_bar and ns[0]["beat"] > 0:
        pick[lead] += 1
print("  notes before the first bar line, when it starts inside a bar and crosses one:",
      " ".join("%d:%d" % kv for kv in sorted(pick.items())))

# ---------------------------------------------------------------- where and how phrases end
print("\nHOW A PHRASE ENDS")
last_longest = last_len = 0
last_d = []
endpos = collections.Counter()
for s, p, ns in phrases:
    bb = s["_bb"]
    L = ns[-1]
    last_d.append(L["dur"])
    if len(ns) > 1 and L["dur"] >= max(n["dur"] for n in ns[:-1]) - 0.01:
        last_longest += 1
    b = L["beat"]
    if near(b, 0):
        endpos["onset on the downbeat"] += 1
    elif near(b % 2, 0):
        endpos["onset on the half-bar beat"] += 1
    elif near(b % 1, 0):
        endpos["onset on a weak beat"] += 1
    elif L.get("tie") or (L["t"] + L["dur"]) > (int(L["t"]) + 1):
        endpos["onset off the beat, held across it (anticipation)"] += 1
    else:
        endpos["onset off the beat, short"] += 1
multi = sum(1 for _, _, ns in phrases if len(ns) > 1)
print("  last note is the longest:", pct(last_longest, multi))
print("  last note length        ", quart(last_d))
for k, v in endpos.most_common():
    print("  %-50s %s" % (k, pct(v, len(phrases))))

# ---------------------------------------------------------------- rhythm inside phrases
print("\nRHYTHM")
grid = collections.Counter()
ant = 0
allns = [n for _, _, ns in phrases for n in ns]
for n in allns:
    f = n["beat"] % 1
    if near(f, 0):
        grid["on a beat"] += 1
    elif near(f, 0.5):
        grid["eighth off-beat"] += 1
    elif near(f, 0.25) or near(f, 0.75):
        grid["sixteenth"] += 1
    else:
        grid["triplet or other"] += 1
    if f > 0.01 and n["t"] + n["dur"] > int(n["t"]) + 1 + 0.01:
        ant += 1
for k, v in grid.most_common():
    print("  onset %-20s %s" % (k, pct(v, len(allns))))
print("  off-beat onset held across the next beat (anticipation): %s of notes" % pct(ant, len(allns)))
ioi = []
for _, _, ns in phrases:
    ioi += [round(b["t"] - a["t"], 2) for a, b in zip(ns, ns[1:])]
h = collections.Counter(ioi)
print("  time between onsets:", " ".join("%g:%s" % (k, pct(v, len(ioi))) for k, v in sorted(h.items()) if v / len(ioi) > 0.02))

# ---------------------------------------------------------------- repetition of rhythm
print("\nRHYTHMIC REPETITION (each phrase against the phrase before it in the same song)")
def shape(ns, bb):
    """Onsets only, counted from the phrase's first bar line: a pickup's notes fall below nought.
    Durations are left out because a held last note and a short one with a rest after it are the
    same rhythm to the ear."""
    b0 = ns[0]["bar"] + (1 if ns[0]["beat"] >= bb - 1.5 + 0.01 and len(ns) > 1 and ns[1]["bar"] > ns[0]["bar"] else 0)
    return set(round(n["t"] - b0 * bb, 2) for n in ns)
def jac(A, B):
    return len(A & B) / len(A | B)
sims = []
for (s1, p1, a), (s2, p2, b) in zip(phrases, phrases[1:]):
    if s1 is s2:
        sims.append(jac(shape(a, s1["_bb"]), shape(b, s2["_bb"])))
print("  onset overlap with the phrase before (Jaccard) quartiles:", quart(sims))
for cut in (0.5, 0.8):
    print("  overlap above %.1f with the phrase before: %s" % (cut, pct(sum(1 for j in sims if j > cut), len(sims))))
anyrep = {0.5: 0, 0.8: 0}; tot = 0
for i, (s, p, ns) in enumerate(phrases):
    prev = [shape(q, s["_bb"]) for (s2, _, q) in phrases[:i] if s2 is s]
    if not prev:
        continue
    tot += 1
    A = shape(ns, s["_bb"])
    best = max(jac(A, B) for B in prev)
    for cut in anyrep:
        anyrep[cut] += best > cut
for cut, v in anyrep.items():
    print("  overlap above %.1f with SOME earlier phrase in the song: %s" % (cut, pct(v, tot)))
# two phrases back: the AABA / question-answer shape
two = [jac(shape(a, s1["_bb"]), shape(c, s3["_bb"])) for (s1, _, a), (s2, _, b), (s3, _, c)
       in zip(phrases, phrases[1:], phrases[2:]) if s1 is s3]
print("  overlap above 0.5 with the phrase two back: %s" % pct(sum(1 for j in two if j > 0.5), len(two)))

# ---------------------------------------------------------------- pitch
print("\nPITCH")
iv = []
for _, _, ns in phrases:
    iv += [b["pitch"] - a["pitch"] for a, b in zip(ns, ns[1:]) if "pitch" in a and "pitch" in b]
ha = collections.Counter(min(12, abs(x)) for x in iv)
print("  interval sizes:", " ".join("%d:%s" % (k, pct(v, len(iv))) for k, v in sorted(ha.items())))
runs = []
for _, _, ns in phrases:
    r = 1
    for a, b in zip(ns, ns[1:]):
        if a["pitch"] == b["pitch"]:
            r += 1
        else:
            runs.append(r); r = 1
    runs.append(r)
print("  unison share of intervals: %s" % pct(sum(1 for x in iv if x == 0), len(iv)))
def max_run(ns):
    best = r = 1
    for a, b in zip(ns, ns[1:]):
        r = r + 1 if a["pitch"] == b["pitch"] else 1
        best = max(best, r)
    return best
print("  phrases with 3+ repeated notes in a row: %s" % pct(sum(1 for _, _, ns in phrases if max_run(ns) >= 3), len(phrases)))
rng = [max(n["pitch"] for n in ns) - min(n["pitch"] for n in ns) for _, _, ns in phrases]
print("  phrase range (semitones)", quart(rng))

# ---------------------------------------------------------------- prosody
print("\nPROSODY")
def onbeat(n, bb):
    return near(n["beat"] % 1, 0)
def strongbeat(n, bb):
    return near(n["beat"], 0) or (bb == 4 and near(n["beat"], 2))
cls = collections.defaultdict(list)
for s, p, ns in phrases:
    bb = s["_bb"]
    for i, n in enumerate(ns):
        key = n.get("syl", "?")
        cls[key].append((n, bb, i == len(ns) - 1))
for key in ["S", "u", "m"]:
    v = cls.get(key, [])
    if not v:
        continue
    print("  %s: %4d notes, on a beat %s, on beat 1 or 3 %s, median length %.2f, held across a beat %s" % (
        {"S": "stressed  ", "u": "unstressed", "m": "melisma   "}[key], len(v),
        pct(sum(onbeat(n, bb) for n, bb, _ in v), len(v)),
        pct(sum(strongbeat(n, bb) for n, bb, _ in v), len(v)),
        med([n["dur"] for n, _, _ in v]),
        pct(sum(1 for n, bb, _ in v if n["beat"] % 1 > 0.01 and n["t"] + n["dur"] > int(n["t"]) + 1.01), len(v))))
wc = collections.defaultdict(list)
for n in allns:
    wc[(n.get("word"), n.get("syl"))].append(n["dur"])
for k in [("c", "S"), ("c", "u"), ("f", "u"), ("f", "S")]:
    if wc.get(k):
        print("  %s word, %s syllable: median length %.2f (n=%d)" % (
            "content " if k[0] == "c" else "function", "stressed  " if k[1] == "S" else "unstressed", med(wc[k]), len(wc[k])))
pl = collections.defaultdict(list)
for n in allns:
    pl[n.get("punct", "")].append(n["dur"])
print("  syllable before punctuation: median length",
      ", ".join("'%s' %.2f (n=%d)" % (k or "none", med(v), len(v)) for k, v in pl.items()))
# where the phrase's stressed syllables fall relative to the downbeat of each bar
sb = collections.Counter()
for s, p, ns in phrases:
    for n in ns:
        if n.get("syl") == "S":
            sb[round(n["beat"] * 4) / 4] += 1
tot = sum(sb.values())
print("  stressed syllable onsets by position in bar:", " ".join("%g:%s" % (k, pct(v, tot)) for k, v in sorted(sb.items()) if v / tot > 0.02))
# the last stressed syllable of a phrase: is it the last note?
lastS = sum(1 for _, _, ns in phrases if ns[-1].get("syl") == "S" or (ns[-1].get("syl") == "m"))
print("  phrase ends on a stressed syllable (or its melisma): %s" % pct(lastS, len(phrases)))
wend_ends = sum(1 for _, _, ns in phrases if ns[-1].get("wend"))
print("  phrase ends at the end of a word: %s" % pct(wend_ends, len(phrases)))
