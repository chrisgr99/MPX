#!/usr/bin/env python3
"""THE TROMBONE'S TUNING SCORES, for setting each note's start and tone by ear on mpxTrombone (docs/brass.md,
phase 7).

    python3 tools/trombone-tuning-score.py

Writes research/brass/tuning/Trombone tuning, pp.musicxml, and the same at mf and ff: two bars of four
tongued quarter notes on every pitch from E2 to C5, the pitches the Iowa recordings hold, at 80 crotchets a
minute, each pitch's name a rehearsal mark over its first bar, a section of its own in mpxScore, so that a loop
of its two bars can be set there.
"""
import os

LOWEST, HIGHEST, TEMPO = 40, 72, 80
# The spellings trombonists read: flats, but F sharp and C sharp.
SPELLING = [("C", 0), ("C", 1), ("D", 0), ("E", -1), ("E", 0), ("F", 0), ("F", 1), ("G", 0), ("A", -1), ("A", 0), ("B", -1), ("B", 0)]
NAMES = ["C", "C sharp", "D", "E flat", "E", "F", "F sharp", "G", "A flat", "A", "B flat", "B"]


def score(dynamic):
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<score-partwise version="3.1">',
           f'  <work><work-title>Trombone tuning, {dynamic}</work-title></work>',
           '  <part-list><score-part id="P1"><part-name>Trombone</part-name></score-part></part-list>',
           '  <part id="P1">']
    bar = 1
    for midi in range(LOWEST, HIGHEST + 1):
        step, alter = SPELLING[midi % 12]
        octave = midi // 12 - 1
        name = f"{NAMES[midi % 12]} {octave}"
        for half in range(2):
            out.append(f'    <measure number="{bar}">')
            if bar == 1:
                out.append('      <attributes><divisions>1</divisions><key><fifths>0</fifths></key>'
                           '<time><beats>4</beats><beat-type>4</beat-type></time><clef><sign>F</sign><line>4</line></clef></attributes>')
                out.append(f'      <direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit>'
                           f'<per-minute>{TEMPO}</per-minute></metronome></direction-type><sound tempo="{TEMPO}"/></direction>')
                out.append(f'      <direction placement="below"><direction-type><dynamics><{dynamic}/></dynamics></direction-type></direction>')
            if half == 0:
                # As a rehearsal mark, which mpxScore takes as a section's name: as words, "F 2" was read as forte.
                out.append(f'      <direction placement="above"><direction-type><rehearsal>{name}</rehearsal></direction-type></direction>')
            for _ in range(4):
                alter_xml = f'<alter>{alter}</alter>' if alter else ''
                out.append(f'      <note><pitch><step>{step}</step>{alter_xml}<octave>{octave}</octave></pitch>'
                           '<duration>1</duration><type>quarter</type></note>')
            out.append('    </measure>')
            bar += 1
    out += ['  </part>', '</score-partwise>', '']
    return "\n".join(out)


here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "research", "brass", "tuning")
os.makedirs(here, exist_ok=True)
for dynamic in ("pp", "mf", "ff"):
    path = os.path.join(here, f"Trombone tuning, {dynamic}.musicxml")
    with open(path, "w") as f:
        f.write(score(dynamic))
    print("wrote", os.path.normpath(path))
