# mpxPattern — typed rhythms, and several lines from one melody

## What it is

A rhythm written as text. A row of characters says where the notes fall, a second row says how strong each is, a third says roughly how high it sits. Each row cycles at its own length, so rows of eleven, seven and five come back into step only occasionally, and the pattern is longer than anything typed into it.

It takes the place of mpxPhrase. Phrase generates a rhythm from the form; this is the same slot with the rhythm written by hand.

## Why it is not a note sequencer

The pattern chooses no pitches. mpxMelody chooses pitches, against the chart's harmony, as it already does. A pattern is therefore a phrase rather than a part: the same typed rows over a different chart give the same phrasing on different harmony, which is the thing worth keeping.

## The rows

**Hits.** `x` is a stroke and `.` is a rest. A digit from 2 to 9 is a ratchet: that many evenly spaced strokes inside the one slot, which is how a roll or a drag is typed rather than chosen. A bar line and a space are layout and are ignored, so the row can be written in bars and still mean what it looks like. Its length is the chart's phrase, laid out four bars to a line — see below.

**Strengths.** A digit from nought to nine, one per step, read round on its own length — counted across the whole timeline rather than restarting each phrase, which is what makes it phase. A missing or unreadable character is the loudest, nine, so a row left empty plays everything at full weight. It is the note's level, and it is also its weight in the line: a strong step is where a chord tone belongs and a weak one is where a passing note or an approach note belongs. Nought is a rest, so either row can thin the pattern.

**Contour.** A digit saying how high in the line's range that note aims, nought at the bottom and nine at the top; a full stop carries on from the last. This is what shapes a line without naming any notes, and it is what the paths over an image did in GXW. Like the hits, it is the length of the chart's phrase, so the shape comes round when the harmony does and is heard as belonging to it.

## The hits are as long as the phrase

The hits row is not an arbitrary length. It is the length of the chart's phrase, which is almost always a multiple of four bars, so the pattern comes round where the music does rather than drifting against it. Where a section of the chart is selected, the pattern is that section's length and recurs with it, which is how a repeated section keeps its own figure.

It is shown four bars to a line, with as many lines as the phrase needs. Four bars is what the eye reads as a unit and what the hand counts in, and a thirty-two bar chorus is therefore eight lines rather than one long row nobody can find their place in.

The strengths row is not constrained this way. It is one row of digits, cycling on its own length, and its phasing against the phrase is the point of it: seven strengths against a sixteen-bar phrase give a line that accents differently every time round without a single character being retyped.

The contour row follows the hits rather than the strengths: it is the phrase long. A shape that floated against the form would be heard as drifting rather than as a tune, and the whole point of a contour is that the same rise and fall arrives over the same chords each time round. The strengths alone are free, and that is what keeps a repeated phrase from being identical.

## What GXW did, and what is worth keeping

The same feature in GXW is `deriveNormalLooped` in `src/beatPoints.js`, with the typing in `src/inspectorFields.js` and the four-bar rows in `src/chartFollow.js`. What it does that this design should keep:

**Ratchets.** A digit in the hits row is that many strokes in the slot. It costs nothing and it is most of what makes a typed rhythm sound played.

**Bar lines as layout.** The typing field inserts a bar line every so many cells and strips them when the row is read, so a long row stays readable.

**A pattern per phrase, inherited forward.** Each phrase of the chart has its own row, and a phrase with nothing typed in it uses the last one that was typed. A whole chorus is therefore one row until a phrase needs to differ.

**The strengths counted globally.** The strength row is sampled on the running slot number, not on the position within the phrase, so its cycle floats across the form.

**The unfolded chart.** In chart-following mode the rows follow the played bar order — repeats and codas expanded — rather than the written one, so a repeated section gets the same figure each time it is played.

**Variation with a seed.** A deterministic per-cycle flip, so a pattern varies without becoming random: the same seed gives the same variation every time the piece is played.

What is deliberately not kept here is the image: in GXW the sampled colour under each beat chose the pitch, the sustain and whether the beat sounded at all. Here the pitch belongs to mpxMelody and the harmony, which is a different answer to the same question.

## Writing the contour by machine

A contour can be typed, and for the bars that matter it should be. For the rest, a generator fills the row with digits, which are then ordinary digits: editable, saved as text, and replaced only when it is asked to generate again. What is heard is what is written down.

The shapes worth having are the fractal one — long arcs with smaller incidents inside them, from a seed, with a roughness control — and the plain ones a tune actually uses: an arch over the phrase, a ramp, a wave of a chosen number of bars, a walk that strays by a step at a time, and a terrace of a few levels held for a bar or two each. With a seed and a range, auditioning contours takes a second or two, which is what sweeping a picture was for.

It generates a phrase's worth, since that is what the row is.

## What a character is worth

One setting: a bar, a beat, an eighth, a sixteenth, or a triplet eighth. The beat, the bar and the metre come from the cable, so there is no clock jack and no tempo control.

## Several lines, one melody module

Rack allows several cables into one input, and mpxMelody's rhythm input takes up to four. Each cable is a line.

This is the point of the design. Two melody modules write two tunes that share a key; one melody module with every line's step in front of it at the same moment can write parts. It can keep lines out of unison, keep them from crossing unless it means to, move one against another rather than with it, and leave room in one line where another has just moved. Separate generators cannot do any of that, because neither knows what the other is about to play.

Each line's character comes from its pattern module — its role (bass, inner or melody), its register, and its contour row. The pattern says when, how strong, how high and what kind of part; the melody decides the actual notes for all of the lines together.

What comes out is one MPX cable carrying every line, since a note on that cable is an independent event. One voice module plays the lot, or the lines are split if they want different sounds.

## What has to change elsewhere

**The reader must say which cable a step came from.** mpxMelody reads its upstreams interleaved and cannot tell one from another, so every line would be treated as one. BusReader hands back the upstream index with each event.

**mpxMelody gains the rules that keep lines apart.** No unisons between lines; a minimum distance between registers, which the patterns' own register settings mostly supply; contrary motion preferred where a choice is free; and a line that has just leapt leaves the next step to something else.

**mpxMelody takes its weights from the pattern.** A step's strength decides how strongly a chord tone is preferred over a passing note, which is what makes a typed pattern sound phrased rather than merely rhythmic.

## What it does not do

It chooses no pitches, it keeps no time of its own, and it knows nothing about the chord. Those belong to the melody, the chart and the voicing.

## The stages

1. The module, its rows and its panel: typing, cycling, and notes out with the strengths as levels. The hits laid out four bars to a line, sized from the chart's phrase.
2. The upstream index in the reader, so a line can be told from a line.
3. mpxMelody reading several rhythm cables as several lines, with its own settings per line taken from the pattern.
4. The rules that keep the lines apart.
5. The contour row feeding the melody's register choice.
