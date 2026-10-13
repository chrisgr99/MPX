#!/usr/bin/env python3
"""THE TABLE OF STARTS, learned from what rehearsal found. See docs/bowed-strings.md, phase 8.

The player can write every decision it rehearses (BowedPlayer::setLog; `make bowplayer ARGS="...
log=FILE"`): the situation, every way of starting or changing it tried, with how each came out, and
the one it played. This reads those logs and answers the question a table of starts depends on —
how often the same situation's best start is the same from one time to the next — and writes the
table: for each kind of situation, the start that came out well most often.

WHAT COUNTS AS COMING OUT WELL is what the player itself asks of a start (BowedPlayer.cpp): from the
string, that it speaks within three periods and SPEAKS, plays cleanly after (ROUGH_CLEAN), reaches
its level promptly (RISE_PROMPT) and starts without a click (CLICK_CLEAN); a bow change, that it is
back to one slip a period within 90 ms.

A SITUATION is the instrument and its mode, the kind of start, the string, the note in steps of
three semitones above the open string, the marking, and for a start from the string how strongly
the string is still ringing. Coarse on purpose: the finer the cells, the fewer times each is seen.

    python3 test/starttable.py build/decisions/*.jsonl [--table OUT.json] [--least N]

THE NUMBERS ARE UPPER BOUNDS for the table. A start is only seen where rehearsal's shortlist offered
it, so its success rate in a situation is over the times it was tried there, and the table is
finally judged by playing with it.
"""
import argparse
import collections
import glob
import json
import math
import sys

SPEAKS = 0.03
ROUGH_CLEAN = -35.0
RISE_PROMPT = -3.0
CLICK_CLEAN = 0.0
CHANGE_SETTLES = 0.09


def grade(t, kind, hz):
    """HOW WELL A START CAME OUT, nought to four, in the order the player prefers: it speaks in time,
    then it plays cleanly after, then it reaches its level promptly, then it starts without a click.
    Each counts only with all the ones before it; one that does not speak in time is graded under one
    by how soon it reaches its full level. A bow change is four when it is back to one slip a period
    within 90 ms and nought when it is not."""
    if kind == 'change':
        return 4 if t['periods'] < 999 and t['periods'] / hz <= CHANGE_SETTLES else 0
    quick = 0 <= t['periods'] <= 3 and t['late'] <= SPEAKS
    if not quick:
        # NOTHING SPOKE IN TIME, which in a passage is most starts from the string, since the note
        # before is still ringing: then, as the player chooses, by how soon it is at its full level,
        # if it comes to one slip a period within ten periods at all — under one, so that any start
        # that speaks in time grades above any that does not.
        if t['periods'] < 0 or t['periods'] > 10:
            return 0.0
        return max(0.0, 0.99 * (1.0 - t.get('full', 9.0) / 0.33))
    g = 1
    for ok in (t['rough'] <= ROUGH_CLEAN, t['rise'] >= RISE_PROMPT, t['click'] <= CLICK_CLEAN):
        if not ok:
            break
        g += 1
    return g


def came_out_well(t, kind, hz):
    if kind == 'change':
        return t['periods'] < 999 and t['periods'] / hz <= CHANGE_SETTLES
    quick = 0 <= t['periods'] <= 3 and t['late'] <= SPEAKS
    return quick and t['rough'] <= ROUGH_CLEAN and t['rise'] >= RISE_PROMPT and t['click'] <= CLICK_CLEAN


def option(t, kind):
    """Which start this is, as the table names it."""
    if kind == 'change':
        return ('change', round(t['press'], 2), round(t['down'], 3))
    if t['start'] == 'glide':
        return ('glide', round(t['ramp'], 2), round(t['floor'], 1))
    # A BITE: its place as a share of the planned one, its force over the note's, and its acceleration
    # as a share of what Guettler's limits allow, on a logarithmic grid about 8% a step.
    return ('bite', round(t['betaShare'], 2), round(t['bite'], 2), round(math.log(max(t['accelShare'], 1e-6)) * 12.0))


def ringing_class(d):
    r = d.get('ringingDb', -300.0)
    if r < -90.0:
        return 'silent'
    if r < -50.0:
        return 'faint'
    return 'ringing'


def situation(d):
    kind = d['kind']
    base = (d['instrument'], kind, d['string'], d['semitone'] // 3, int(round(d['level'] * 7)))
    if kind == 'change':
        return base
    return base + (ringing_class(d),)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--table', help='where to write the table, as JSON')
    ap.add_argument('--least', type=int, default=5, help='decisions a situation needs to be judged')
    ap.add_argument('--plain', help='where to write the table for the player, as plain lines (see StartTable)')
    a = ap.parse_args()

    decisions = []
    for pattern in a.logs:
        for path in glob.glob(pattern):
            with open(path) as f:
                for line in f:
                    line = line.strip()
                    if line:
                        try:
                            decisions.append(json.loads(line))
                        except ValueError:
                            pass        # A line cut short by a run still writing.
    if not decisions:
        print('no decisions')
        return 1

    # EVERY DECISION: whether any start tried came out well, and whether the one played did.
    by_kind = collections.Counter()
    any_well = collections.Counter()
    for d in decisions:
        kind = d['kind']
        by_kind[kind] += 1
        if any(came_out_well(t, kind, d['hz']) for t in d['tried']):
            any_well[kind] += 1
    print('%d decisions' % len(decisions))
    for kind in sorted(by_kind):
        print('  %-7s %6d, rehearsal found one that came out well in %.0f%%' % (kind, by_kind[kind],
              100.0 * any_well[kind] / by_kind[kind]))

    # EVERY DECISION, GRADED: the best grade rehearsal found, against what the start played had.
    for kind in sorted(by_kind):
        best = [int(max(grade(t, kind, d['hz']) for t in d['tried'])) for d in decisions if d['kind'] == kind]
        counts = collections.Counter(best)
        print('  %-7s best grade rehearsal found: %s' % (kind, '  '.join('%d: %.0f%%' % (g, 100.0 * counts[g] / len(best))
              for g in range(5))))

    # EACH SITUATION: each start's record there, over the times it was tried.
    cells = collections.defaultdict(list)
    for d in decisions:
        cells[situation(d)].append(d)
    table = {}
    judged = 0
    rehearsed_well = 0.0
    table_well = 0.0
    same_best = 0
    best_seen = 0
    # BY KIND: the grade rehearsal reached on each occasion, and the table's start's grade on the
    # occasions it was tried, summed; and how many occasions each.
    graded = collections.defaultdict(lambda: [0.0, 0.0, 0, 0.0, 0])
    for key, ds in cells.items():
        if len(ds) < a.least:
            continue
        kind = key[1]
        record = collections.defaultdict(lambda: [0, 0, 0.0])
        for d in ds:
            for t in d['tried']:
                o = option(t, kind)
                record[o][1] += 1
                record[o][2] += grade(t, kind, d['hz'])
                if came_out_well(t, kind, d['hz']):
                    record[o][0] += 1
        # THE START WITH THE BEST GRADE ON AVERAGE, among those tried at least a third of the time.
        enough = max(2, len(ds) // 3)
        candidates = [(g / n, w / n, n, o) for o, (w, n, g) in record.items() if n >= enough]
        if not candidates:
            continue
        mean_grade, rate, tried, best = max(candidates)
        g = graded[kind]
        for d in ds:
            g[0] += max(grade(t, kind, d['hz']) for t in d['tried'])
            g[2] += 1
            mine = [grade(t, kind, d['hz']) for t in d['tried'] if option(t, kind) == best]
            if mine:
                g[3] += mine[0]
                g[4] += 1
        judged += len(ds)
        rehearsed_well += sum(1 for d in ds if any(came_out_well(t, kind, d['hz']) for t in d['tried']))
        table_well += rate * len(ds)
        # HOW OFTEN EACH OCCASION'S OWN BEST WAS THE TABLE'S: the chaos, counted.
        for d in ds:
            wells = [option(t, kind) for t in d['tried'] if came_out_well(t, kind, d['hz'])]
            if wells:
                best_seen += 1
                if best in wells:
                    same_best += 1
        table['|'.join(str(k) for k in key)] = {'start': list(best), 'cameOutWell': round(rate, 3),
                                                 'meanGrade': round(mean_grade, 2), 'tried': tried,
                                                 'occasions': len(ds)}
    print('\n%d situations seen at least %d times, %d decisions between them' % (len(table), a.least, judged))
    if judged:
        print('  rehearsing, a start that came out well:          %.0f%%' % (100.0 * rehearsed_well / judged))
        print('  the table\'s one start, where it was tried:       %.0f%%' % (100.0 * table_well / judged))
    if best_seen:
        print('  occasions where the table\'s start was among those that came out well: %.0f%% of %d'
              % (100.0 * same_best / best_seen, best_seen))
    print('\n  mean grade, rehearsing against the table\'s one start where it was tried:')
    for kind in sorted(graded):
        g = graded[kind]
        if g[2] and g[4]:
            print('    %-7s rehearsing %.2f over %d occasions; the table %.2f over the %d it was tried' %
                  (kind, g[0] / g[2], g[2], g[3] / g[4], g[4]))
    if a.table:
        with open(a.table, 'w') as f:
            json.dump(table, f, indent=1, sort_keys=True)
        print('wrote %s' % a.table)
    if a.plain:
        # ONE LINE A SITUATION, as BowedTable.cpp reads it: the instrument, the kind, the string, the
        # step of three semitones, the marking, how strongly the string rings (a dash for a bow change),
        # then the start and its three numbers.
        with open(a.plain, 'w') as f:
            f.write('# The table of starts: see test/starttable.py and BowedTable.hpp.\n')
            for key in sorted(table):
                parts = key.split('|')
                if len(parts) == 5:
                    parts.append('-')
                start = table[key]['start']
                numbers = [str(x) for x in start[1:]] + ['0'] * (4 - len(start))
                f.write(' '.join(parts + [start[0]] + numbers[:3]) + '\n')
        print('wrote %s' % a.plain)
    return 0


if __name__ == '__main__':
    sys.exit(main())
