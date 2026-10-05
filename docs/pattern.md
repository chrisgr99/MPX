# mpxPattern — typed rhythms, eight at a time

## What it is

A rhythm written as text. A grid of characters says where the notes fall: a full stop is a rest, an `x` is a beat, a digit from two to nine is that beat struck that many times. Bar lines are drawn where the metre says and are never typed.

It takes the place of mpxPhrase. Phrase generates a rhythm from the form; this is the same slot with the rhythm written by hand.

## A character is one beat

Always. There is no note-value setting: a character is a beat, a bar line falls wherever the time signature puts one, and four bars make a row.

Typing a bar of sixteenths as sixteen characters is tedious, and a settable note value put bar lines in places that depended on a control rather than on the music. Faster rhythm comes from the cycle speeds instead — the same grid of beats played at two is in eighths, at four in sixteenths.

## Eight tabs, and a tab is a voice

Nearly everything on the panel belongs to one pattern, so one pattern's worth is shown and the tabs say whose. Each tab has its own light, which switches that pattern on and off; turning one on brings it to the front, turning one off leaves you where you were.

There are eight because `ChordVoicing` carries eight. The voicing on the cable is chosen once for the whole chain and held steady between chords, so tab three is voice three. With a voicing present a tab sounds its voice's pitch and can drive a synth directly. Without one it sends notes with no pitch, which is what mpxMelody's rhythm input takes.

Every note carries its tab number in `Event::part`, counted from one, so a receiver that is itself one of several knows which notes are its own. A module that does not care ignores it, which is every module written before this.

## The three pattern types

**Manual.** The rows are phrases, typed, played one after another. A row left empty plays the nearest typed row above it, so a whole chorus is one row until a phrase needs to differ. Measures sets how many bars a phrase spans, up to four; phrases sets how many of them there are, up to eight.

**Harmony.** The rows are the chart's own bars, four to a row, so a tune can be given a different rhythm in every bar of it. The grid takes its size from whatever the chart is playing — the whole chart, or the section or span of bars chosen on mpxChart, since the chart narrows its own timeline to what it plays. The cycle speeds field is inactive here: the beats are locked to the chart's bars, and a speed other than one would unlock the grid from the thing it mirrors.

**Euclidean.** The rows are generated from a count of active beats and a rotation, by the modulo formula — cell *i* fires when *ik* mod *n* is below *k*. That puts the first beat on cell nought, which is how a musician reads these: three in eight is the tresillo. The grid is read-only apart from ratcheting a beat that is already there.

## Typing

Typing **overwrites** the cell at the caret and never inserts. A character is a beat and the column it sits in is where in the bar it falls, so anything that pushed the rest of the row along would move every later beat off its place.

A rest is the full stop or the space bar. A beat is `x` or the comma, which sits beside the full stop so a rhythm can be tapped in with one hand. A digit from two to nine ratchets that beat into that many evenly spaced strokes, which is how a roll or a drag is typed rather than chosen.

Backspace and delete clear a cell to a rest. The arrow keys move the caret, up and down between rows.

Every keystroke is applied at once. Nothing has to be committed.

## The two short fields

**Beat strength.** A digit per beat, nought to nine, read round on its own length — counted across the whole pattern rather than restarting each row, which is what makes it phase. It is the note's level, and nought is a rest, so the strength can thin the pattern on its own. Seven strengths against a sixteen-bar pattern accent differently every time round without a character being retyped.

**Cycle speeds.** A list of numbers separated by spaces, decimals allowed, a negative meaning backwards. One entry covers one trip through the whole pattern, and the list cycles, so `1 2 -1` is a pass as written, one at double time, then one backwards. An empty list is one.

Neither field lines up with the grid. They cycle against it, so they are as long as they need to be and usually much shorter — which is why they sit below the box rather than in it.

Both commit on every keystroke, so a speed typed while the pattern plays is heard at once. A list half typed never commits: `1 -` holds the last good reading until it is a number again.

## The grid

One framed box. Each row is numbered behind a rule of its own, with a rule between rows and a bar line after every complete bar including the last.

It scrolls when it has more rows than it can show — by the wheel over the box, which the box takes so the rack does not move, or by the scroll bar down its right-hand side. While the pattern plays the box follows the sounding row, and that following pauses for a few seconds after the scroll bar or the wheel has been used, so reading somewhere else is not interrupted.

The sounding cell is marked with a white outline one character wide. The caret is the same box in the frame's own green.

## The face

Hack, which the plugin ships under the MIT licence, at twenty-four pixels. Rack's own monospaced face is a display face with thin strokes and little between a full stop and a comma, and this grid is nothing but full stops, crosses and digits read at a glance. Hack was drawn for small text on a screen.

Monospaced is the point rather than a preference: every row has to line up character for character with the rows above and below it, because the column a character sits in is where in the bar it falls.

The colours are the beat-point editor's in GXW, so a pattern moved between the two reads the same way: the box is a dark grey ground, the text white, an inherited row grey, the frame and the rules green. The fields inside the box have no fill and no border of their own — the frame and the rules supply all the structure.

## The panel sets its own width

The grid decides it. Four bars of six beats is twenty-eight characters, and at twenty-four pixels that is a hundred and forty-five millimetres of box, which is why the module is thirty-one HP.

Six-eight is the widest metre it is sized for. A longer bar than that still works; it simply shows fewer than four bars on a row.

## Several lines, one melody module

Rack allows several cables into one input, and mpxMelody's rhythm input takes several. Each cable is a line, and each tab within a cable is a part of that line.

This is the point of the design. Two melody modules write two tunes that share a key; one melody module with every line's beat in front of it at the same moment can write parts. It can keep lines out of unison, keep them from crossing unless it means to, move one against another rather than with it, and leave room in one line where another has just moved. Separate generators cannot do any of that, because neither knows what the other is about to play.

## Where it sits in the chain

The chart, then the voicing if there is one, then mpxPattern, then whatever plays the notes — mpxMelody, or mpxSprites and mpxPathSprites.

The rhythm says *when* and the thing downstream says *what*, so the rhythm has to be made before the sprite sees it. A sprite takes each note of its own tab, reads the picture under wherever it has reached along its curve, chooses a pitch by melodic rules against the harmony on the cable, and passes the note on with that pitch filled in.

## What it does not do

It keeps no time of its own and it knows nothing about the chord. The beat, the bar and the metre are on the cable, so there is no clock jack and no tempo control. There are no level or length controls either: what plays the notes decides those.

## What is still to do

**Harmony mode and changing metres.** The grid is sized from the chart's length in beats divided by the beats in a bar, which is right for a piece in one metre and wrong for one that changes. It needs the per-bar metre from the chart rather than a single number.

**Harmony mode and folded repeats.** GXW's chart-following shows the *written* bars and plays the *unfolded* order, so a repeated section gets the same figure each time it is played and the highlight cycles through the repeat group. This shows the played length instead, which is the same thing only for a chart with no repeats.

**Ratchets in Euclidean mode** are kept per row in the typed text, but a change to the count or the rotation moves the beats without moving the ratchets with them.
