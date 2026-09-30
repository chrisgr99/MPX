# One module for a melody, up to four lines

mpxPhrase, mpxMelody and its voice expanders do one job between them: draw a line against the harmony on the cable. Split across three modules and two cables, that job takes about fifty knobs, several of which can be set somewhere no music goes, and a wrong cable or an expander nudged out of contact produces silence with nothing on any panel to say why.

This is the same job in one module: chart in, notes out, a handful of controls that stay inside what the style they belong to actually does.

The three modules stay. They are still the way to drive the note choice from a typed or Euclidean rhythm, they share the same libraries, and nothing is duplicated. If this proves better to use they can be deprecated later.

## The panel grows with the voices

Voices is a control on the shared part, one to four. Each voice past the first adds a band on the right of the same module, carrying that voice's own controls and its own MPX output. No expanders and no cables between them.

The module is built for four voices whether or not they are shown: Rack fixes a module's parameters and jacks when it is created, so all four exist from the start and the count decides how many bands are drawn. The width changes with it, and Rack is asked to place the module again, which pushes whatever is to the right along.

Reducing the count disconnects any cable on a band that goes away. A cable attached to a jack nobody can see is worse than one that is plainly gone.

## What is shared and what belongs to a voice

Shared, because the answer has to be the same for every line:

- the chart input;
- Melody style and Rhythm style, and the seed and cycle that decide which line comes out of them;
- the voicing: which chord tones are available, where they sit, how little they move between chords;
- how hard the voices keep out of each other's way, which only means anything once there are two.

Per band, because these are what make one line different from another:

- register and span — where the line sits and how far it ranges;
- smoothness and chord lock — steps against leaps, chord tones against passing notes;
- motif and contour — how surely a restated rhythm brings its pitches back, and the shape across a group;
- articulation and accent;
- the voice's role, and its MPX out.

## Two styles, not one

The phrasing styles and the voice styles are already separate tables — `phraseStyle` and `voiceStyle` — indexed by the same number today only because one knob on the chart chose both. They are two controls here: Rhythm style and Melody style. Pop phrasing with a jazzier line, or a jazz ballad's breathing under a plain singable line, are musical choices worth having.

Both default to Follow chart. The chart's own Style then sets the character of every player in the patch in one move, as it does now, and either can be set against the chart when that is what is wanted. The seed already works this way: Lock seed reads Follows chart or Locked.

Within a style, each macro knob moves between values the style measures, so no position of any control is outside what that music does. The styles are the ones in Phrasing.cpp and MelodyVoice.cpp: Pop 1, 2 and 3, Jazz 1, 2 and 3, and Song.

## No rhythm input

The note choice leans on what each event carries: whether it falls on a strong beat, how far through its group it is, and whether it restates an earlier figure — that last is what brings a motif back. A phrasing generator produces all of it because it is building phrases. An evenly spread pattern or one typed as text is not phrasing anything, so a line drawn against it has no phrase to be shaped by and no figure to restate, and it comes out as correct notes in no particular order.

So the module phrases for itself and there is no rhythm input. One could be added later without changing anything else.

## The rhythm goes out

A Rhythm out on the shared part sends the phrase's events without their pitch: the level, the length, the contour, whether the event is a pickup, how far through its group it falls, and what it restates.

That is what a module elaborating on the rhythm needs — a drum part or a comp playing to where the lines start and end and where the breaths are, rather than to a metronome. The pitched outputs carry the same information, so a follower could read one of those and ignore the pitch; the separate jack means a melody does not have to be heard in order to be followed.

The elaborating module itself is a later phase.

## The voicing

The module places the chord itself.

When a voicing is already on the cable — an mpxVoicing upstream, feeding this and a comp — it takes that one instead, exactly as mpxArp does. A patch with two players wants one set of chord decisions, or the two disagree about which notes the chord has.

## Tuning by ear

The rule in [live-tuning.md](live-tuning.md) applies here, and this module is the reason it is straightforward: the rhythm and the notes are decided in one place, so a knob change rebuilds one phrase and carries on from the current beat, with one splice point rather than two modules agreeing about one across a cable.

## The name

`mpxLine` — a line drawn against the harmony, which is what mpxMelody's own description calls it, and it does not read as a second melody module.
