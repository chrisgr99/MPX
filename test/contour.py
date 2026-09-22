"""Which way the melody goes, measured against which way real melodies go.

  python3 test/contour.py                     the patch in Downloads, over every example
  PATCH=path/to/patch.vcv python3 test/contour.py

WHAT IT MEASURES: how often the line turns round, how often it goes straight back to the note it
has just left (a trill, not a melody), how long it keeps going one way, and how wide it ranges in a
phrase. The melody's notes come from `make phrasesim`, which runs the same code the modules do.

WHAT IT COMPARES WITH: the same figures in 456 transcribed jazz solos, from the Weimar Jazz
Database, if it is on disk at ~/Documents/MusicResearch/weimar/wjazzd.db. When the melody first
had no sense of direction it turned round on 60 per cent of its moves and went straight back on
39; the solos do so on 39 and 11. See the contour comment in src/Melodic.cpp.
"""
import os, re, sqlite3, statistics as st, subprocess, sys
from collections import defaultdict

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAMES = {"C": 0, "C sharp": 1, "D": 2, "E flat": 3, "E": 4, "F": 5, "F sharp": 6, "G": 7,
         "A flat": 8, "G sharp": 8, "A": 9, "B flat": 10, "B": 11}


def midi(tok):
    m = re.match(r"(.+?)(-?\d+)$", tok)
    return NAMES[m.group(1)] + 12 * (int(m.group(2)) + 1)


def measure(phrases):
    moves = turns = returns = steps = leaps = repeats = 0
    runs, ranges = [], []
    for ps in phrases:
        if len(ps) < 3:
            continue
        ranges.append(max(ps) - min(ps))
        ivs = [b - a for a, b in zip(ps, ps[1:])]
        for d in ivs:
            moves += 1
            if d == 0:
                repeats += 1
            elif abs(d) <= 2:
                steps += 1
            else:
                leaps += 1
        nz = [d for d in ivs if d]
        turns += sum(1 for a, b in zip(nz, nz[1:]) if (a > 0) != (b > 0))
        returns += sum(1 for a, b, c in zip(ps, ps[1:], ps[2:]) if a == c and a != b)
        run = best = 1
        for a, b in zip(nz, nz[1:]):
            run = run + 1 if (a > 0) == (b > 0) else 1
            best = max(best, run)
        runs.append(best)
    return dict(moves=moves, steps=100 * steps // moves, leaps=100 * leaps // moves,
                turns=100 * turns // moves, returns=100 * returns // moves,
                run=st.median(runs), range=st.median(ranges))


def ours():
    extra = ["PATCH=" + os.environ["PATCH"]] if "PATCH" in os.environ else []
    phrases = []
    for ex in range(1, 8):
        out = subprocess.run(["make", "-s", "-C", HERE, "phrasesim", "EXAMPLE=%d" % ex, "NOTES=1"]
                             + extra, capture_output=True, text=True).stdout
        for phrase in out.split("\nPhrase ")[1:]:
            phrases.append([midi(m) for m in re.findall(r"^\s+(.+?\d) on bar", phrase, re.M)])
    return measure(phrases)


def solos():
    path = os.path.expanduser("~/Documents/MusicResearch/weimar/wjazzd.db")
    if not os.path.exists(path):
        return None
    con = sqlite3.connect(path)
    spans = defaultdict(list)
    for melid, a, b in con.execute("select melid, start, end from sections where type='PHRASE'"):
        spans[melid].append((a, b))
    notes = defaultdict(list)
    for melid, p in con.execute("select melid, pitch from melody order by melid, eventid"):
        notes[melid].append(int(round(p)))
    return measure([notes[m][a:b + 1] for m, ss in spans.items() for a, b in ss])


def show(name, r):
    print("%s: steps %d%%, leaps %d%%; turns round on %d%% of moves, straight back on %d%%; "
          "runs of %.0f notes one way; a range of %.0f semitones a phrase"
          % (name, r["steps"], r["leaps"], r["turns"], r["returns"], r["run"], r["range"]))


show("the examples", ours())
s = solos()
if s:
    show("456 jazz solos", s)
