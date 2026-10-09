# Reading an engraved PDF

A PDF produced by a notation program is not a picture of a score. Every notehead, accidental, clef, rest, flag, dot and tuplet number is a glyph from a music font placed at an exact coordinate, and every staff line, stem, beam, barline, hairpin and slur is an exact line or filled shape. The information is all there at full precision. Nothing has to be recognised.

This matters because optical music recognition on a rendered image is unreliable on dense music, and the kind of score worth the trouble of recovering is usually dense. A vector PDF skips the recognition problem entirely and replaces it with a geometry problem, which is tractable.

This document is the method. The working implementation is `tools/engraved-pdf/`, written against Schoenberg's op. 23 no. 1 as published by Dalrymple Designs and set in Sibelius. The last section covers the variant that matters more often in practice: taking the marks out of a PDF and putting them into a MusicXML file you already have.

## First, decide whether this applies

Three checks, in order, before any other work.

Is the PDF vector or a scan. Search the raw bytes for `/Subtype /Image`; a scan is one large image per page and nothing else. If it is a scan, this method does not apply and optical recognition is the only route.

Which music font is embedded. Search for `/BaseFont` and read the names. Sibelius writes OpusStd, OpusTextStd and OpusSpecialStd. Finale writes Maestro. Dorico and recent MuseScore write Bravura or Leland. The geometry reasoning below is the same for all of them; the glyph table is not.

Is there a ToUnicode map. The font dictionary should carry `/ToUnicode` pointing at a CMap. Without it the glyph identifiers are opaque numbers with no stable meaning, and you have to identify every glyph by drawing it. With it you still have to learn what each character means in that font, but the mapping is stable across files from the same program.

If all three pass, expect pitch and accidental recovery to be exact, duration recovery to be close to exact, and timing to take most of the work.

## Getting at the content

PDF page content streams are compressed. `qpdf --qdf --object-streams=disable in.pdf out.pdf` rewrites the file with every stream uncompressed and every object on its own line, which makes the rest readable with ordinary text handling.

The drawing is often not in the page's own content stream. A page may do nothing but invoke a form XObject that holds the music, with the page stream carrying only a watermark. Follow `/Contents` to find the `Do` operator, then follow the named XObject through the page's `/Resources` to the form, and parse the form's stream.

## Parsing the content stream

A small interpreter is enough. You need the graphics state stack (`q` and `Q`), the current transformation matrix (`cm`), line width (`w`), path construction (`m`, `l`, `c`, `h`, `re`), painting (`S` for stroke, `f` and `f*` for fill), and the text operators.

Two things are easy to get wrong and both cost real time.

Text positioning uses more than the text matrix. `Tm` sets it, but `Td` moves relative to the start of the current line, and a single text object will often set the matrix once and then use `Td` repeatedly for the glyphs that follow. Ignoring `Td` puts a run of glyphs all at one coordinate, which silently merges things that are far apart on the page.

Within a `TJ` array, the numbers between strings are horizontal adjustments in thousandths of an em, and the glyphs themselves advance by their own widths from the font's `/W` array. In ordinary text these are small kerns you could ignore. In a music font they are large and structural, because the engraver uses them to place glyphs. Both have to be applied.

For curves, keeping only the endpoint of each `c` operator is enough. A slur becomes three points, which is all you need to find where it starts and ends; you are not reproducing its shape.

## Identifying the glyphs

The ToUnicode map gives you a character for each glyph, but the character is arbitrary. In Opus, lowercase n is a natural, hash is a sharp, question mark is a bass clef, and the o-e ligature is a black notehead. You cannot guess this and you should not try.

Draw them. The embedded font program is reachable through the font descriptor's `/FontFile2`. Parse its `loca` and `glyf` tables, convert the quadratic contours to polylines, and render each glyph used in the file into a labelled contact sheet. Then look at it once and write down the table. This takes under an hour and removes all guessing from everything that follows.

Draw the contours as outlines, not as filled polygons. A naive fill ignores winding rules, so a half notehead comes out looking identical to a black one and a double flag comes out looking like a single one. The distinguishing feature is often an inner contour, and filling hides it. Counting contours is itself diagnostic: in Opus the eighth flags have one contour and the sixteenth flags have two, because the gap between the two hooks is enclosed.

## Reading the staves

Staff lines are long horizontal strokes of identical width spanning the system. Collect the distinct vertical positions of strokes longer than some fraction of the page width, sort them, and take them five at a time; a group whose total span is small is a staff. For piano music, pair the staves top to bottom into systems.

Barlines are vertical strokes heavier than a stem that span from the top staff of a system to the bottom staff. They give you the measures. Stems are vertical strokes of the lighter weight.

A clef glyph's origin sits on the line it names: the G of a treble clef on the second line from the bottom, the F of a bass clef on the fourth. Collect every clef on a staff with its horizontal position, and the clef in force at any point is the last one at or before it. This handles mid-bar clef changes, which are common in piano music and which make pitch wrong everywhere downstream if you ignore them.

Pitch is then arithmetic. The staff line gap is a quarter of the total staff span; a step is half the gap. Count steps from the bottom line to the notehead, add the diatonic index the clef gives the bottom line, and you have the note. Ledger lines need no special handling because they are not consulted.

Accidentals sit immediately to the left of their notehead at the same height. Match each accidental to the nearest notehead to its right at the same height rather than searching outward from each notehead, so that one accidental claims one note. The window has to be wide enough for a cautionary accidental printed in brackets, which sits further out than a plain one.

## Reading the durations

The notehead glyph gives the base: black, half or whole. From there:

A stem with beams crossing it is one over two to the power of the beam count: one beam an eighth, two a sixteenth, three a thirty-second.

A stem with a flag is the same, counting hooks rather than glyphs. This is the trap. Opus draws a sixteenth flag as a single glyph carrying two hooks, so counting flag glyphs gives you eighths where sixteenths were written, and every bar containing one comes out too long. Build a table of hooks per flag glyph from the contact sheet.

Augmentation dots multiply by one and a half, or one and three quarters for two. The augmentation dot and the staccato dot are different glyphs, at least in Opus, where the augmentation dot lives in the special font and the staccato dot in the main one. If they share a glyph in another font, separate them by position: an augmentation dot is to the right of the notehead at the same height or offset by half a space, a staccato dot is above or below it, centred.

A beam is a thin parallelogram, and thin is measured across it. Taking the height of its bounding box works for a level beam and fails for a sloping one, because a beam falling across a staff has a tall box and a thin body. Take the four corners, pair them into a left side and a right side, and measure the vertical distance within each pair. Reject anything where the two measurements disagree, which removes slurs and other filled shapes.

Grace notes are drawn at a smaller point size than ordinary notes. A size threshold separates them reliably and they must be excluded from the bar's arithmetic.

Tuplet numbers are digits in the text font of the music family. Attaching them to the right group is harder than it looks and is covered below.

## Working out when notes start

The page says nothing directly about onsets. This is the part that takes the work, and it is the part worth reusing, because the reasoning has nothing to do with PDFs.

What you have is a set of notes and rests, each with a duration and a horizontal position, and some of them grouped under shared beams. What you need is a time for each.

Three constraints pin it down.

Notes printed at the same horizontal position sound together. This is the strongest one, and it is what links a group on one staff to a group on the other.

Horizontal order is time order. A note printed to the left of another does not start after it.

The bar fills. The latest end in the bar equals the length the time signature gives.

Build the atoms first. A beamed group is one atom: its members' onsets relative to each other are fixed by their own durations, so the whole group has a single unknown starting time. A note or rest not under a beam is an atom of one. Then merge atoms that share a horizontal position, because that fixes their relative times too; after merging, each connected component has one unknown.

That usually leaves a handful of unknowns per bar, few enough to search exhaustively over a grid fine enough to carry the smallest tuplet in the piece. One twenty-fourth of a quarter note is enough for triplets and sixteenths together.

More than one placement will often satisfy every constraint. Do not take the first. Score each solution by the number of voices it implies on each staff and the amount of silence it leaves, and take the lowest. Without this you get bars where four notes all land on the first beat because nothing forbade it — which happens whenever one staff has a long note covering the bar and the other staff's arithmetic is therefore unconstrained.

Two details that are not obvious.

A whole-bar rest is drawn in the middle of the bar. Its position carries no information, so exclude it from the horizontal linking and from the ordering constraint, and force it to the start.

Requiring each staff to fill is too strong. Engravers leave voices unfilled, and a second voice that stops halfway through a bar with no rest printed is not an error you should try to repair. Require the bar as a whole to fill and let the cost function discourage silence.

Once onsets are known, voices follow by interval packing: sort by start, and put each event in the lowest-numbered voice whose previous event has already ended. Fill the gaps with rests, and split each gap into durations that are real note values, or the renderer draws a question mark where the rest should be.

## Attaching the marks

Dynamics, words, hairpins, articulations, tuplet numbers and ties all have to be attached to something, and the attachment rules are where the subtle errors live.

A dynamic is a run of glyphs in the text font of the music family, adjacent horizontally. Join them using the real glyph advances; guessing at the width splits `mp` into two marks.

A hairpin is two strokes meeting at a point, with their far ends at the same horizontal position and separated vertically, the meeting point level with the midpoint between them. It opens rightward for a crescendo and leftward for a diminuendo. Reject very short ones.

An articulation is centred horizontally on its notehead but placed beyond the stem, which can be a long way from the note. The horizontal alignment identifies it; the vertical distance does not, and a vertical tolerance tight enough to be safe will miss most of them. Match on horizontal position alone, then among the candidates prefer the highest note if the mark is above them all and the lowest if it is below.

A tie is a short, nearly level curve whose two ends are each at a notehead, with both noteheads at the same pitch. A slur is the same kind of shape without the pitch agreement. Distinguishing them matters for playback.

A tuplet number sits beyond the beam, on the side the stems point. Measuring from the number to the noteheads instead picks the wrong voice wherever two voices on one staff are beamed in opposite directions, and the wrong voice then gets the wrong durations and the bar stops reconciling. Measure to the beam. Allow a fallback to the nearest group regardless of side, because a number that has been dragged by hand can end up on the wrong side. Cap the search distance, or a group on another system of the same page will win.

## Checking the result

Three checks, and the second is the one that earns its keep.

Count noteheads in and notes out, per bar. This catches anything the writer drops: a voice that never gets emitted, a grace note with nothing to attach to, a chord that loses a member.

Require every bar to reconcile before believing anything. A bar whose durations do not add up to its time signature is telling you that a duration is wrong, and chasing those is how you find the systematic faults. Each of the traps described above was found this way and not by reading the code. When the count of failing bars drops in steps as you fix them, the remaining failures are pointing at real defects.

Render both and compare bar by bar. Emit the original's system breaks into the output so the two line up on the page; then a side-by-side image of each system is directly readable. This is what catches errors that reconcile perfectly and are still wrong, such as a tuplet attached to the wrong voice.

## Taking marks into an existing MusicXML

The common case is not a score you have no file for. It is a score you do have, from a library or a publisher, where the notes are right and the expression is missing or thin, and a PDF that has the dynamics, hairpins and pedalling printed on it.

That job is easier than a full conversion, because it does not need the hard part. You need the glyph positions, the staff and barline geometry, and the pitches — all of which come straight out of the page. You do not need the durations to be right, and you do not need the onset solver at all.

Align by notes, not by time. Read the PDF's measures and, within each, the sequence of pitches in horizontal order. Read the same from the existing MusicXML. Match the two measure sequences; they should agree bar for bar, and where they do, you have a correspondence between each notehead on the page and each note in the file. Then every mark on the page attaches to whichever note it sits nearest, and that note is identifiable in the file.

Checking the alignment is nearly free, because the pitch sequences are a strong signature. A bar where they disagree is a bar where the PDF edition and your file differ, and should be reported rather than guessed at. Pickup bars, repeats written out in one source and not the other, and a differing number of bars are the things that break the correspondence, and all of them show up as a sequence mismatch.

Insert into the existing document rather than regenerating it. Parse the MusicXML, add direction and notation elements at the right places, and write it back. Everything you did not touch stays exactly as it was, which means the result is safe to use even where the extraction was incomplete. This is the same approach the score annotation work settled on, for the same reason.

A hairpin needs a start and a stop, and the stop has to fall on a later note than the start or it draws as a dot. Where a hairpin runs past the end of a bar, carry the stop into the following measure rather than clamping it.
