# Playing an imported part

A Guitar Pro file says which notes are played and how: the string and the fret, hammer-ons and pull-offs, slides into and out of a note, let ring, palm mute, dead notes, ghost notes, accents, harmonics, the vibrato, the strum and the bend. [guitar-pro.md](guitar-pro.md) covers reading the file and putting all of that on the cable. This covers what happens to it afterwards.

## The realism is in the conversion, not in the samples

The Ultimate Guitar player was taken apart to find out where its quality comes from, since it plays these files better than anything else available. Its score engine is MuseScore's, compiled to WebAssembly; its synthesiser is FluidSynth; and its sounds are GeneralUser GS, an ordinary free General MIDI bank by S. Christian Collins. For one song it loads about 1.7 megabytes of it — an overdrive guitar, a steel guitar, a finger bass, a piano, an organ, a saxophone and a drum kit.

So the samples are unremarkable and the quality is not in them. It is in the step between the score and the synthesiser: per-string channels, pitch-bend curves, legato, strum spread, dynamics and let ring. That step is what this plugin reproduces, and it is a library rather than a module.

## The performer

[Perform.hpp](../src/Perform.hpp) takes notes with their articulations and produces twelve voices whose pitch, level, gate and brightness move over time, with a queue of the moments those things changed. No Rack in it and no synthesiser in it, so it builds into a command-line test; `make performtest` plays each articulation on its own with the humanising turned off, so an answer is a number rather than a range.

**A voice is a string.** That is why it is not a note-per-voice allocator: a bend moves one string and leaves its neighbours alone, a string is monophonic because a real one is, and let ring ends when that string is played again rather than after some length. A note with no string gets a voice of its own.

**Nothing is known about the future.** The performer plays the notes as they arrive, so a slide into a note cannot begin before the note does; it reaches its pitch a few tens of milliseconds after the onset instead. A score also announces its notes ahead on the cable, which the bowed strings need (see [bowed-strings.md](bowed-strings.md), phase 8), but the performer does not read them. A grace note needs no special handling, since it is already a note of its own with its own moment. The alternative is to delay everything by a fixed latency, which is worse than either.

**Every number is in a file.** Forty-two of them in `DreamerMPX/perform.txt`, as plain name and value lines with comments — how long a palm mute lasts, how deep a wide vibrato is, how fast the hand crosses the strings — so the character of the playing can be changed by ear without rebuilding anything. Either module reads it again from its menu. A name a later version adds is passed over rather than refusing the file. Not JSON: the library has to build without Rack, and a flat list of names reads more easily than braces.

## Four ways to hear it

The performer decides what is played. What makes the sound is a separate choice, and there are four, all reading the same schedule and therefore agreeing about what a hammer-on is.

**mpxFluidSynth** — the whole band through one FluidSynth and one bank. Six MPX inputs, one instrument each, a program each from the bank's own list of 287 sounds, and a channel per string so a bend on one string leaves the others alone. A drum kit is a sound like any other; what makes it a kit is that the notes are General MIDI drum numbers. See below for how the library is built.

**mpxGuitarist** — the same schedule as control voltage, a channel per string, so any voice in the rack can play a guitar part. The bend is inside the volts-per-octave output, so one cable into an oscillator plays the performance.

**mpxGuitarArticulation** — one part through an oscillator of your choosing, with the envelopes, filtering and stereo placement applied here rather than patched. See [guitar-articulation.md](guitar-articulation.md).

**mpxGuitar** — a physically modelled guitar, strings and body included, nothing patched between it and the mixer. See [guitar-model.md](guitar-model.md).

## FluidSynth, and how it is built

FluidSynth understands note-on, note-off, pitch bend, control changes and program changes, and nothing about a hammer-on or a palm mute. Every articulation is therefore translated before it arrives: a hammer-on sends no new note and glides the string's bend instead, a palm mute shortens the note and closes the filter through a control change, a strum spreads the onsets across the strings, a bend becomes a curve through its points with the channel's range set to twelve semitones.

It is built static into `dep` by `make dep`, pinned to 2.6.1, without audio drivers, MIDI drivers, libsndfile, LADSPA, readline, networking or threads of its own — none of which a module inside a host has any business owning. Without GLib either, which it stopped needing in 2.6; before that it would have been the hardest part of this to carry to Windows. It is LGPL and this plugin is GPL-3, so linking it statically is permitted, and its licence and source are named in the plugin's licence notes.

The bank is GeneralUser GS, kept in `DreamerMPX/banks` and read when the module is made, so the panel asks nothing it can answer itself. `make soundtest` loads a bank, plays a chord across four channels and bends one of them a whole tone, then measures where the pitch went — everything the module does to make a sound happens there too, where it can be looked at.

## What is not built

**Style profiles.** Articulation habits, strum patterns and chord voicings measured out of real transcriptions into a profile — how often a player hammers rather than picks, which positions they use, how hard they lean into a downstroke — to steer a part that is generated rather than imported. A profile describes how somebody plays and never what they play, so it carries no note sequences and reproduces no song.

**A standard MIDI file export.** The performance written out with the per-string channels kept, so a part can be opened in a DAW and its expression looked at. The cheapest way to check the conversion against another player.

**Variations.** Taking an imported part and moving it: re-voicing a chord, substituting a figure, thinning or thickening, keeping the phrasing and changing the notes. This is the thing all of it was for, and it wants designing on its own.

**A generative guitarist** — inventing a part from the harmony rather than playing one — was specified and is unlikely to be built. The reason for importing a song is that the material is already musical, and inventing one from a chord grid is the problem importing was taken up to avoid.

## What is yours and what is ours

An imported file is a transcription of someone's song. Importing one for your own listening is one thing; shipping any of it inside the plugin is another, and nothing imported is ever included with it.

## Where the parts came from

alphaTab's GPIF parser is the most complete public reading of the format's element names, bend and slide units and repeat handling. MuseScore's own `importexport/guitarpro` and its playback code are what the Ultimate Guitar player runs, and the best guide to how each articulation should turn into MIDI. PyGuitarPro reads gp3, gp4 and gp5 and was both the port source and the oracle the reader is checked against.
