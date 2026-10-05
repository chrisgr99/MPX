# mpxPathSprites

A sibling of mpxSprites. The same picture, read the same way, but the sprites do not drift under a field: each one runs along a line or a circle drawn on the picture, round and round, and plays where a rhythm says it should.

## What it is for

mpxSprites is weather. A field pushes the sprites about, they find their own paths, and what comes out is a texture that can be steered but not placed. That is its character and it is worth having.

A path is the opposite. The sprite goes where it is put, round the same loop every time, so the material recurs — and recurrence is what makes a passage sound composed rather than endless. The picture still supplies everything the notes are made of; what changes is that the reading order is fixed and repeatable instead of emergent.

**Recurrence is now the whole of what this module adds.** mpxSprites already takes its rhythm from mpxPattern, reads the picture where it has got to and chooses the pitch. What it cannot do is come back to the same place.

## A separate module

One module doing both would be too complex. The two share the picture library, the sources and the pitch choice, but a sprite that drifts and a sprite that runs a drawn path want different controls and different drawing on the picture, and a panel carrying both would carry neither well.

## How it is patched

The chart, then mpxPattern, then mpxPathSprites, on one cable. mpxPattern forwards the harmony, the transport, the voicing and the pedals, so the path module receives everything the chart published as well as everything the pattern added.

## Eight paths

Eight, matching mpxPattern's eight tabs and the chord voicing's eight voices, so every tab has somewhere to go. Each sprite is a tab carrying its own path and its own settings, and it plays the beats of the tab of the same number — which it knows from `Event::part` on the note, as mpxSprites does.

## The path

Each sprite has one path, and it is either a **line** or a **circle**. Which it is, and a circle's diameter, are set on the panel. Where it sits is set by dragging on the picture.

**A circle** is placed by dragging its centre. It starts at the middle of the picture when the sprite is switched on. Its zero is at three o'clock and it runs counter-clockwise, which is the convention in the app this comes from and is worth keeping so that a figure transferred between them behaves the same way.

**A line** arrives at unit length in a default position. Dragging either end moves that end; dragging the middle moves the whole line without turning it.

The split follows from the shapes. A circle has one size, which is a number and belongs on the panel. A line has two ends, which are places and belong under the pointer.

## What the picture gives

The readings under the sprite give a note's pitch, its level and its length, as they do in mpxSprites — and from the same seventeen sources, which are not only colour: brightness, two hue axes, saturation, edge and oddness; height, across and distance out; speed, heading east-west and north-south, corner and push; nearness, which sprite, and chance.

**Drop** silences a beat whose reading falls below a threshold. A rhythm played against a picture should not sound every step regardless of what is underneath it; dropping the weak ones is what makes a path over a varied picture phrase itself.

## Where the notes fall

The rhythm comes from mpxPattern. A sprite does not decide when to play; it decides what a note sounds like when the pattern calls for one.

**The unit is a looped section, not a bar count.** A chart is too long to hear repetition across: played whole, a path's return is minutes apart and nothing is recognised. So a section of eight to twelve bars is looped — by its section letter on mpxChart, or by dragging across its bars — and the chart narrows its own timeline to what it is playing, so the cycle length on the cable is the loop's length. Nothing new is needed for this.

**One trip around a path is `Repeats` passes of that looped section**, typically four to six, each pass taking an equal share of the path. The same rhythm therefore falls on different colours each time the section comes round, and the variation is supplied by the picture rather than invented. Nothing random is needed and nothing random is wanted: the same patch plays the same music.

## Moving in musical time

**The sprite moves smoothly and continuously, and its position is worked out from where the music has reached** — the pass number, the beat within the section, and the section's length. All three are on the cable: the pass number is `Harmony::epoch`, which counts how many times the music has gone back to the top and changes on a rewind and on the chart looping.

So after `Repeats` passes the sprite has gone exactly once round, whatever rhythm was played over it. Rests and dense bars make no difference to where it is. A note reads wherever the sprite happens to be at that instant.

This is what makes the recurrence exact: the sprite is at the same point of the path on the same beat of the same pass, every time.

**It follows that a cycle speed of two sends two notes in the time of one and both read nearly the same place**, so they are nearly the same pitch. The speeds double the rhythm rather than the melody. That is accepted: recurrence is the module's reason for being, and it is bought at that price.

## Direction, from the speeds on the cable

**A tab's direction of travel comes from mpxPattern's cycle speeds**, a negative speed running the sprite backwards along its path.

**So mpxPattern publishes its tabs' current speeds on the cable**, a block of eight numbers beside the harmony — the same shape as the chord voicing — updated as each tab advances through its list. The path module integrates its own tab's speed, which it needs continuously rather than only when a note arrives, because it is moving between notes.

**With no pattern connected**, a line sweeps to its far end, returns to its start and sweeps the same way again. It never reverses: every sweep is in the same order, so the material recurs identically. A circle simply comes round.

**Pitch always comes from the chord sounding now**, at the chart's position — never from where the sprite has reached along its trip. Otherwise a tab at double speed would be playing the harmony of a bar the band left some time ago.

## The picture library

The same library mpxSprites uses, which already exists in `Picture.hpp`: the twenty most recent pictures with thumbnails, a chooser five across by four down, and a copy of every picture's bytes kept in a folder the plugin owns so that a remembered picture cannot go missing. Those copies are shared by every module here.

The two modules are unlikely to be used at once, so each chooses its own picture — but choosing one adds it to the library for both.

## What mpxChart has to add

**An automatic turnaround.** When a looped section's last chord does not lead back to its first, playback replaces the last bar with a turnaround — a dominant, or a two-five, of the first chord. The written chart is untouched; this is a property of looping it.

It matters more here than anywhere else, because this module depends on looping a short section many times, so a loop that lands badly is heard over and over. It is in scope for this work.

## A simpler module first: paths as control voltages

The whole of the framework below — choosing a picture, drawing a line or a circle, moving a sprite along it, reading what is underneath — is worth having before a single note is played. So it is built first in a module that makes no notes at all.

**Each sprite puts out a control voltage**: whatever it is reading now, from the same seventeen sources, as a voltage. One per sprite to begin with. No beats, no harmony, no pitch — a sprite is a reading head and the picture is the data.

**It is a modulation source and, at speed, an oscillator.** Run a circle round once a bar and it is an LFO whose shape is drawn rather than chosen from a list. Run it at two hundred a second and the picture's values become a waveform — a wavetable oscillator whose table is a circular slice of an image, and whose timbre is changed by dragging the circle somewhere else.

**That changes where the motion comes from.** The note module moves in musical time, worked out from the chart's position, which is exactly what makes its recurrence exact. A voltage source cannot: at audio rate there is no chart fast enough to count it. So the CV module carries its own **rate** control, free-running in cycles per second, with locking to the chart's beat as one setting among them rather than the only behaviour.

**Known problem, not solved here.** A picture squared off to 1024 points read at two hundred revolutions a second gives about two hundred and forty samples a revolution, which undersamples the table and aliases. The fix is to average along the path over the distance covered in one sample rather than point-sampling it, which is a filter whose width follows the rate. It is noted so that it is not discovered as a surprise.

## The order of work

**One. The shared framework**, as a library rather than a module: path geometry — a line as two ends, a circle as a centre and a diameter — position along a path as a fraction, and the drawing and hit-testing that go with dragging one on a picture. `Picture.hpp` is reused unchanged; the seventeen sources are currently inside mpxSprites and are lifted out into a header both modules use.

**Two. The CV module.** The picture and its chooser, eight tabs, a path each, the kind and diameter on the panel and the position under the pointer, a source per sprite, a rate per sprite, and the voltages out. Useful on its own, and it proves the framework before any of the musical machinery is written.

**Three. mpxPattern publishes its tab speeds** on the cable, a block of eight beside the harmony, which the note module needs for direction and which nothing needs before then.

**Four. mpxChart's automatic turnaround**, so a looped section lands properly — worth having before the note module rather than after, because that module loops a short section many times and a bad landing is heard over and over.

**Five. The notes.** Beats from mpxPattern by tab number, the pitch from the chord sounding now, drop, repeats, and position in musical time. Whether this is a separate module or a mode of the CV module is open, and is better decided once the CV module exists and its panel is known.

## Where the original is

The application this comes from has the paths in `src/curveGeometry.js`, the cycle speeds in `src/simulation.js` as `cycleSpeeds` and `computeCyclePhaseFromGlobalTime`, the step positions in `src/beatPoints.js`, and the drop and strength handling in `simulation.js`.
