#!/usr/bin/env python3
"""Rebuild MusicXML from a PDF that was engraved in Sibelius.

A Sibelius PDF is not a picture of a score. Every notehead, accidental, clef and
rest is a glyph from the Opus music font placed at an exact coordinate, and every
stem, beam and barline is an exact line or filled shape. So nothing has to be
recognised: the pitch is the notehead's height above the bottom staff line read
through whichever clef is in force at that horizontal position, and the duration
is the number of beams crossing the stem, or the number of hooks on its flag.

What does have to be worked out is when each note starts, because the page says
nothing about that directly. Three things pin it down: notes printed at the same
horizontal position sound together, horizontal order is time order, and the bar
has to fill. Where more than one placement satisfies all three, the one taken is
the one needing the fewest voices and leaving the least silence. See solve3.py.

    python3 convert.py score.pdf out.musicxml

The PDF has to carry the Opus fonts (OpusStd, OpusTextStd, OpusSpecialStd) with
a ToUnicode map, which is what Sibelius writes. A scanned PDF will not work, and
neither will one set in another program, because the glyph meanings differ.

LEFT TO THE CALLER: octave lines. generate.py carries the one 8va in Schoenberg
op. 23 no. 1 as a literal, because finding the dashed line and matching it to a
staff was not worth generalising for a single occurrence. Anything else with an
8va needs that constant changed.
"""
import os, subprocess, sys, shutil, tempfile

def main():
    if len(sys.argv) < 2:
        print(__doc__); return 1
    pdf = os.path.abspath(sys.argv[1])
    out = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else 'out.musicxml'
    here = os.path.dirname(os.path.abspath(__file__))
    work = tempfile.mkdtemp(prefix='engraved-')
    qdf = os.path.join(work, 'qdf.pdf')
    # qpdf's QDF mode uncompresses every stream, which is what the parser reads
    subprocess.check_call(['qpdf', '--qdf', '--object-streams=disable', pdf, qdf])
    env = dict(os.environ, MPX_QDF=qdf, PYTHONPATH=here)
    subprocess.check_call([sys.executable, os.path.join(here, 'generate.py'), out],
                          cwd=work, env=env)
    print('wrote', out)
    shutil.rmtree(work, ignore_errors=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())
