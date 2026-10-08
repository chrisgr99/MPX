"""A SCALE OVER THE WHOLE KEYBOARD AT EVERY MARKING, written out as MusicXML.

    python3 test/ladder.py

WHY THERE IS A FILE FOR THIS. Every measurement of the dynamics so far has been a measurement of
CONSISTENCY: that forte is the same number of decibels in one score as in another, and the same
at one end of the keyboard as at the other. All of that was true and none of it answers whether
the numbers are the right ones, because there is no piece of music in which p can be heard on its
own with nothing else going on.

So this is the ladder with the music taken away: C major from C1 to C8 and back, one note at a
time, at ppp and then pp and so on to fff, with a bar of silence between one marking and the next.
Single notes, so nothing is shared out between the notes of a chord and nothing is held back under
a melody — what is heard is the marking and the register and nothing else.

TWO SETTINGS TO PUT RIGHT BEFORE LISTENING, or the test measures them instead:

    Melody balance   Off      a scale descends faster than the melody is forgotten, so the way
                              down would be held back as accompaniment
    DYNAMICS         full     it narrows the range, which is the thing being judged

Writes to ~/Downloads/dynamic-ladder-test.musicxml.
"""
import os
DIV = 2                      # divisions per quarter; an eighth is 1
MARKS = ["ppp","pp","p","mp","mf","f","ff","fff"]
STEP  = [("C",0),("D",0),("E",0),("F",0),("G",0),("A",0),("B",0)]

# C major from C1 to C8 and back, one note per scale degree.
def scale():
    up=[]
    for octave in range(1,8):
        for i,(s,a) in enumerate(STEP):
            up.append((s,a,octave))
    up.append(("C",0,8))
    return up + up[-2::-1]

def note_xml(step, alter, octave, staff, dur):
    return (f'      <note>\n'
            f'        <pitch><step>{step}</step><octave>{octave}</octave></pitch>\n'
            f'        <duration>{dur}</duration>\n'
            f'        <voice>1</voice>\n'
            f'        <type>eighth</type>\n'
            f'        <staff>{staff}</staff>\n'
            f'        </note>\n')

def rest_xml(dur, kind, staff=1):
    return (f'      <note>\n        <rest/>\n        <duration>{dur}</duration>\n'
            f'        <voice>1</voice>\n        <type>{kind}</type>\n'
            f'        <staff>{staff}</staff>\n        </note>\n')

out=[]
out.append('<?xml version="1.0" encoding="UTF-8"?>\n')
out.append('<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN"'
           ' "http://www.musicxml.org/dtds/partwise.dtd">\n')
out.append('<score-partwise version="3.1">\n')
out.append('  <work><work-title>Dynamic ladder test</work-title></work>\n')
out.append('  <identification><encoding><software>DreamerMPX</software>'
           '<encoding-description>A scale over the whole keyboard at each marking, for testing'
           ' loudness without a piece of music in the way.</encoding-description></encoding>'
           '</identification>\n')
out.append('  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part>'
           '</part-list>\n')
out.append('  <part id="P1">\n')

bar=1
notes=scale()
for m,mark in enumerate(MARKS):
    i=0
    first=True
    while i < len(notes):
        out.append(f'    <measure number="{bar}">\n')
        if first:
            out.append('      <attributes>\n')
            if bar==1:
                out.append(f'        <divisions>{DIV}</divisions>\n')
                out.append('        <key><fifths>0</fifths></key>\n')
                out.append('        <time><beats>4</beats><beat-type>4</beat-type></time>\n')
                out.append('        <staves>2</staves>\n')
                out.append('        <clef number="1"><sign>G</sign><line>2</line></clef>\n')
                out.append('        <clef number="2"><sign>F</sign><line>4</line></clef>\n')
            out.append('        </attributes>\n')
            out.append('      <direction placement="below">\n'
                       '        <direction-type><dynamics>'
                       f'<{mark}/></dynamics></direction-type>\n'
                       '        <staff>1</staff>\n        </direction>\n')
            first=False
        for _ in range(8):
            if i >= len(notes):
                out.append(rest_xml(1,"eighth"))
                continue
            s,a,o = notes[i]; i+=1
            midi = (o+1)*12 + {"C":0,"D":2,"E":4,"F":5,"G":7,"A":9,"B":11}[s]
            out.append(note_xml(s,a,o, 1 if midi>=60 else 2, 1))
        out.append('      </measure>\n')
        bar+=1
    # a bar of silence between one marking and the next
    out.append(f'    <measure number="{bar}">\n')
    out.append(rest_xml(8,"whole"))
    out.append('      </measure>\n')
    bar+=1

out.append('    </part>\n</score-partwise>\n')
path=os.path.expanduser("~/Downloads/dynamic-ladder-test.musicxml")
open(path,"w").write("".join(out))
print("wrote", path)
print("bars:", bar-1, " markings:", len(MARKS), " notes per marking:", len(notes))
