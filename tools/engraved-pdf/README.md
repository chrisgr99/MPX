# Reading a Sibelius PDF back into MusicXML

This is the tool. [docs/engraved-pdf.md](../../docs/engraved-pdf.md) is the method, written to be followed again for another engraver's PDF or for lifting marks into a MusicXML file you already have.

A PDF engraved in Sibelius is not a picture of a score. Each notehead, accidental, clef, rest, flag and tuplet number is a glyph from one of the Opus music fonts placed at an exact coordinate, and each staff line, stem, beam, barline and hairpin is an exact line or filled shape. Nothing has to be recognised from pixels. This reads those coordinates and writes MusicXML.

    python3 convert.py score.pdf out.musicxml

It needs `qpdf` on the path, which is used to uncompress the page streams, and nothing else. Written against the system Python; no packages beyond the standard library, except that `glyphs.py` wants Pillow and is only needed when identifying an unfamiliar glyph.

## What it reads directly

Pitch is the notehead's height above the bottom staff line, read through whichever clef is in force at that horizontal position, so mid-bar clef changes are handled. Accidentals attach to the nearest notehead to their right on the same line or space, which also picks up cautionaries printed in brackets.

Duration is the number of beams crossing the stem, or the number of hooks on its flag. Opus draws a sixteenth flag as one glyph carrying two hooks, not as two glyphs, so `build.HOOKS` maps each flag glyph to its hook count. Augmentation dots are a glyph in OpusSpecialStd; the staccato dot is a different glyph in OpusStd.

Also read: time signatures, barlines and system breaks, grace notes (by their smaller size), tuplet numbers, dynamics, worded directions, hairpins, articulations and ties.

## What has to be worked out

The page says nothing directly about when a note starts. Three things pin it down: notes printed at the same horizontal position sound together, horizontal order is time order, and the bar has to fill. Beamed groups fix the relative onsets of their own members, and sharing a horizontal position links one group to another, which leaves only a handful of free starting times per bar. Those are searched exhaustively. Where more than one placement satisfies every constraint, the one taken is the one needing the fewest voices and leaving the least silence. `solve3.py`.

A whole-bar rest is drawn in the middle of the bar, so its position says nothing and it is excluded from the horizontal linking and forced to the start.

## Traps that cost time

A chord containing a second puts one of its noteheads on the far side of the stem, so matching a notehead to a stem has to allow for the displacement or the chord loses a note.

A steeply sloping beam has a tall bounding box and a thin body. Thickness has to be measured across the parallelogram, not from the box, or long sloping beams are missed and their notes come out too long.

A tuplet number sits beyond the beam on the side the stems point. Measuring to the noteheads instead picks the wrong voice wherever two voices on one staff are beamed apart. Sibelius will sometimes let a number be dragged to the far side, so there is a fallback to the nearest group.

The distance cap on that search matters: without it, a group on another system of the same page can win.

## Not general

`generate.py` carries the one octave line in Schoenberg op. 23 no. 1 as the literal `OTTAVA`. Finding the dashed line and matching it to a staff was not worth generalising for a single occurrence; any other score with an octave line needs that constant changed or the detection written.

Cross-staff beams are not reproduced as beams. Each note carries the staff its notehead sits on, which is correct for pitch and for playback, but a renderer will beam within a staff rather than across the pair.

## Checking the result

`render.swift` draws any PDF to PNG at a given scale:

    swiftc -O render.swift -o render
    ./render score.pdf 4 pages/render

Converting the output with MuseScore and rendering both side by side is how this was checked bar by bar. Matching the original's system breaks, which `generate.py` emits, makes the two line up.
