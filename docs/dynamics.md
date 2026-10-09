# Dynamics

How loud a note is, from the mark on the page to the volts at an instrument's output.

## The ladder

**A marking names a number of decibels, and it names the same number in every score.** Forte means the same thing in a piece that never leaves mezzo-forte as in one that reaches fff; a piece written quietly is meant to be quieter. Nothing normalises a score against itself.

`MPX_RANGE_DB` in `NoteBus.hpp` is 18.5: the whole span from ppp to fff. **Level is a decibel scale** — a note sounds `MPX_RANGE_DB` times `(1 - level)` decibels below `MPX_FULL_VOLTS` — so the eight markings are the eight levels evenly spread from nought to one, one seventh of the range apart, and `mpxDecibels` and `mpxLoudness` are the one conversion every instrument should use.

| | ppp | pp | p | mp | mf | f | ff | fff |
|---|---|---|---|---|---|---|---|---|
| level | 0 | 1/7 | 2/7 | 3/7 | 4/7 | 5/7 | 6/7 | 1 |
| dB | -18.5 | -15.9 | -13.2 | -10.6 | -7.9 | -5.3 | -2.6 | 0 |
| volts | 0.95 | 1.29 | 1.75 | 2.37 | 3.21 | 4.35 | 5.90 | 8.00 |

`sf` sits between f and ff, `sfz` at ff, and `fp` strikes at f — the drop that follows is not read, so it is the f that is kept.

**Eighteen and a half decibels, and not the twenty-nine the recordings span on their own.** That is a piano in a quiet room, and a rack has nowhere near that much room — the loudest texture has to fit under ten volts, so every decibel of range is a decibel taken off the bottom. Settled by listening to `test/ladder.py`, which plays the ladder with no music in the way. The top is fixed by the ceiling, so the figure is really a choice about where ppp sits: a step of 2.64 dB from one marking to the next puts it just under a volt, which is as quiet as a piano can usefully be here.

**The first three attempts at this were judged against the wrong instrument.** The patch had mpxPiano and mpxScore's band both feeding the mixer with the piano muted, so what was being listened to was the SoundFont and not the samples at all. Everything the piano does — the density share, aiming at the figure, the register normalisation — was inaudible, and the ladder kept being widened and narrowed on the strength of it. **Check the thing being changed is the thing making the sound** before changing it again.

**`MPX_FULL_VOLTS` is eight, and that is a single note rather than the loudest thing the module makes.** A chord is louder than one note even after its dynamic has been shared out — about four and a half decibels at the thickest — and nothing may pass ten volts, where the audio interface stops.

Without the soft clip this would have to be six, since the thickest chord would otherwise go over. Those two volts are two and a half decibels that would have come off the bottom of the ladder instead.

A mark is a point and not a property of a note: it holds until the next one. A hairpin is a ramp whose destination is whatever mark comes after it, which may be bars away, so the loudness of every note is worked out after the whole part has been read. A ramp with nothing after it travels `HAIRPIN_REACH`, one seventh — one step of the ladder — and may not arrive below ppp.

A part with no mark anywhere sits at mf from the first bar to the last. Seventeen of the thirty-four scores in regular use are like that — every lead sheet, and the Bach prelude and fugue.

**A dynamic written as a word is a dynamic.** Composers write them in letters as often as in symbols — the Appassionata's slow movement opens *piano e dolce* and carries no dynamic element at all until bar 15, and the Waldstein's rondo opens *sempre pp* with none until bar 9. Read as a symbol only, both pieces played their openings at the mezzo-forte default and then appeared to drop when a mark finally restated what had been true from the first bar.

Matched **by whole words, never by substring**: *Allegro ma non troppo* and *appassionato* both contain the letters pp, and both are in the scores here. The text is split on letters and whole tokens are compared.

*Più* makes it a direction rather than a place. *sempre forte* is a level and stays; *sempre più forte* is an instruction to go on getting louder and is read as a crescendo in words.

Left out deliberately: *sotto voce* and *mezza voce* say how to play rather than how loud — veiled, under the breath — and the nearest level is a guess. A manner is not a dynamic even when it implies one.

**The `sound dynamics` attribute is ignored.** Every score that carries written marks also carries these, essentially one per mark, so they add a dependency on whatever playback settings the engraver happened to have and no new information.

## What a level is worth

**`MPX_FULL_VOLTS`, in `NoteBus.hpp`: a single note at level 1, with the module's volume control where it starts, peaks at eight volts.** Not ten, which is where the interface stops — a chord is louder than one note, and the last volt and a half is a curve rather than a wall.

This is a statement about every instrument on the cable, not about one of them. The same number has to be the same loudness wherever it is read, or handing a part to a different instrument changes the music rather than the colour. Two modules playing the same part were once twenty decibels apart, which nobody hears as a fault — it reads as one of them being the wrong instrument for the music.

`test/leveltest.cpp` plays one note at level 1 through each instrument and fails when any is more than three decibels from the standard, that being about the smallest difference heard as loudness rather than as character.

As measured, all six now within the three decibels it allows:

| | peak | from the standard |
|---|---|---|
| mpxGuitar, steel | 6.00 V | 0.0 dB |
| mpxPiano | 5.00 V | 0.0 dB |
| mpxGuitar, nylon | 5.91 V | -0.1 dB |
| the band, steel guitar | 5.85 V | -0.2 dB |
| the band, grand piano | 4.81 V | -1.9 dB |
| the band, nylon guitar | 7.68 V | +2.1 dB |

mpxPiano is exact because it aims at the figure rather than arriving at one. The band's spread of four decibels is the SoundFont's own: its programs were recorded at whatever level their samples happened to have, and one scale factor cannot take that out.

mpxPiano's voices live inside a Rack Module and cannot be run from a bench, so its note is computed the way the module computes it, from the same recordings through the same formulas. Those constants are repeated in the bench and have to be changed together with the module's.

## The DYNAMICS knob

Twenty-two decibels is still a wide range for a rack, and some pieces want less. The knob narrows it: the setting is the fraction of `MPX_RANGE_DB` used, and it moves the floor and never the ceiling, so nothing ever gets quieter than it was.

| | level | 1.00 | 0.85 | 0.70 | 0.55 | 0.40 |
|---|---|---|---|---|---|---|
| ppp | 0 | -22.0 | -18.7 | -15.4 | -12.1 | -8.8 |
| pp | 1/7 | -18.9 | -16.0 | -13.2 | -10.4 | -7.5 |
| p | 2/7 | -15.7 | -13.4 | -11.0 | -8.6 | -6.3 |
| mp | 3/7 | -12.6 | -10.7 | -8.8 | -6.9 | -5.0 |
| mf | 4/7 | -9.4 | -8.0 | -6.6 | -5.2 | -3.8 |
| f | 5/7 | -6.3 | -5.3 | -4.4 | -3.5 | -2.5 |
| ff | 6/7 | -3.1 | -2.7 | -2.2 | -1.7 | -1.3 |
| fff | 1 | 0 | 0 | 0 | 0 | 0 |
| **whole range** | | **22.0** | **18.7** | **15.4** | **12.1** | **8.8 dB** |

**It does not touch the velocity layer**, so a pianissimo keeps the tone of a pianissimo and only becomes easier to hear. Which recording is chosen is a question about colour; how loud it then plays is a separate gain. Nothing jumps and nothing reorders.

## Aiming at the number

The piano does not let a note land where the recording leaves it. It picks the velocity layer from the marking, for tone, and then divides that recording's own measured peak back out and multiplies by what the marking is worth. `aimedAt` in mpxPiano, one line, and it needs `PianoRegion::peak`, measured as each recording is read.

**That is what makes the register stop mattering.** The Salamander set was recorded note by note and never normalised. The same marking varied by **7.5 dB across the keyboard**, with 3.8 dB of it between A3 and middle C — neighbours in the same set, where middle C is simply a louder performance. A piece whose loud writing happened to sit around F sharp 4 came out four decibels under one whose loud writing sat on middle C, both marked forte.

**And what that first went wrong on.** The peak is the wrong quantity to divide out. It is a few milliseconds of hammer, where the ear weighs a note over a seventh of a second or so, and the two disagree by a factor of two and a half in the bass against nearly seven in the top octave — a treble note is almost all attack. Dividing the peak out made every attack transient the same height from the bottom of the keyboard to the top, and left what is actually heard running **8.7 dB downhill towards the treble**. A melody climbing the keyboard faded as it went, with an identical click under every note of it. That is a good part of the mechanical quality the piano was reported to have, and the correction put it there rather than finding it.

The bench could not see any of this, which is the more useful lesson. `pianoNote` in `leveltest` divided the recording's peak out and multiplied it straight back in, so the register spread it reported was **an identity, not a measurement** — it would have printed 0.0 dB whatever the samples did. It now reads the recording and reports both measures.

**Nor can the loudness simply be divided out instead.** Made equally loud, the top octave's crest factor sends its peaks where the bass's cannot follow: a single fff note there would reach seventeen volts against the eight a single note is allowed, which is not a transient to be rounded off but a note half as loud again as the instrument's whole range.

**So the error is shared**, by `CREST_BLEND`: each recording is divided by its peak to the power one minus the blend, times what is heard to the power of the blend, anchored on `CREST_REFERENCE` so that moving it tilts the keyboard without moving the instrument. `leveltest` prints the whole trade so the figure can be moved on evidence:

| blend | heard spans | peak spans | worst single fff peak | |
|---|---|---|---|---|
| 0.00 | 8.7 dB | 0.0 dB | 8.0 V | every attack alike, the treble quiet |
| 0.25 | 6.5 | 2.2 | 9.7 | |
| 0.50 | 4.3 | 4.3 | 11.7 | where it is set |
| 0.75 | 2.2 | 6.5 | 14.1 | |
| 1.00 | 0.0 | 8.7 | 17.0 | equally loud, and far over the ceiling |

At a half, middle C stays within a quarter of a decibel of where it was, so nothing about the instrument's overall level moves — only the slope across the keyboard, which halves.

It also replaced the set's velocity tracking of seventy-three per cent, taken from its SFZ. That is a reasonable guess at how a piano behaves, but it is not a target: it could say how a note would scale, never what it would come out at.

## How much is sounding: the density share

**A dynamic mark is a loudness, not a hammer speed per finger.** A pianist playing a five-note chord at forte plays the chord forte; they do not play five notes each as loud as a single forte note.

Nothing used to say so, and notes simply added. N of them at one level sum to twenty log N — sixteen decibels for six notes, before the written dynamic is considered at all. Measured over the thirty-six scores in use, the amplitude of an attack spanned twenty-one and a half decibels between one score and another, and it followed how many notes were sounding rather than anything written: a lead sheet, one note at a time, sat at minus four, while Layla, six notes to an attack, sat at plus twelve.

**`mpxShare` in `NoteBus.hpp` shares one dynamic out among the notes struck together**, by the count raised to the power `MPX_DENSITY_POWER`, which is 0.35. Every instrument uses it: mpxPiano, mpxGuitar and the band.

**It is counted where the cable is read and nowhere later.** A strum is spread across milliseconds by the performer, so by the time the strings speak there is no chord left to count — the share rides from the cable to the strike on the note itself. A chord is still bigger than a single note, as it should be, but by an amount a player could have chosen rather than one that follows from how many fingers the chord happens to need. Against a single note at the same marking, with `voicing` already applied:

| notes | 2 | 4 | 6 | 8 | 12 | 16 |
|---|---|---|---|---|---|---|
| without it | +2.8 | +5.7 | +7.5 | +9.3 | +11.9 | +13.0 dB |
| at 0.35 | +0.7 | +1.5 | +2.1 | +3.0 | +4.4 | +4.6 dB |

A single note is left alone, so this pulls dense textures down to meet sparse ones rather than making anything quieter than it was. One half flattens it altogether — every chord within a decibel of a single note — which loses the weight a big chord ought to have.

**The share is a gain on the string's amplitude, not a smaller level**, and that distinction is the whole of why it is carried all the way down to `noteOn`. Level chooses a velocity layer, and the set's layers are far apart: the ladder spans twenty-nine decibels over a level ratio under seven, so amplitude goes roughly as level to the power of one and three quarters. Dividing the level instead divides the amplitude by very much more, and a six-note chord at forte then comes out quieter than a single forte note — the opposite of the fault being cured. Applied as a gain, the arithmetic is the arithmetic intended, and the velocity layer still follows what was written.

It also keeps the share out of the two other things computed from those levels: which layer each note is played from, and the order the notes speak in, which comes from the gaps between them.

**Measured in volts through the real recordings, for a given written dynamic, across every score carrying enough of them:**

| | before | after |
|---|---|---|
| spread of notes written forte | 6.2 dB | 3.5 dB |
| spread of notes written piano | 8.1 dB | 4.7 dB |

## Every instrument against the table

Measured as dB below each instrument's own fff, which is what the bench prints:

| | wanted | piano | guitar steel | guitar nylon | band's piano |
|---|---|---|---|---|---|
| ppp | -18.5 | -18.5 | -18.5 | -18.5 | -19.0 |
| pp | -15.9 | -15.9 | -15.9 | -15.9 | -15.9 |
| p | -13.2 | -13.2 | -13.2 | -13.2 | -13.0 |
| mp | -10.6 | -10.6 | -10.6 | -10.6 | -10.7 |
| mf | -7.9 | -7.9 | -7.9 | -7.9 | -7.8 |
| f | -5.3 | -5.3 | -5.3 | -5.3 | -5.2 |
| ff | -2.6 | -2.6 | -2.6 | -2.6 | -2.5 |
| fff | 0 | 0 | 0 | 0 | 0 |

Worst deviation 0.5 dB, at the band's quiet end.

**A level is a loudness and a velocity is not**, which is the trap here and it cost the quiet end of every score. ppp is nought on a decibel scale; handed to a string as a pluck strength it is no pluck at all, and handed to a synthesiser as a velocity it is silence. Both now convert through `mpxLoudness`.

The band needs one more step, because a velocity is not a loudness either. The SoundFont specification says amplitude follows the square of the velocity; swept through GeneralUser-GS the exponent is nearer **1.57**, and taking the specification at its word left pianissimo four decibels louder than asked. The bench prints the sweep the constant came from, so it can be refitted if the bank changes.

## What the band cannot do, and why that is accepted

The band plays a quiet string part far too softly, and the cause is not the table. Measured on the Fauré piano quartet, second movement, which is 48 per cent pp and never passes ff:

Every level arrives correctly. A violin at ppp peaks at 0.94 V, which is where the piano's ppp sits. Energy above 500 Hz tracks full-band energy within 0.1 dB at every level, so the patch is not going dull at low velocity either.

What is wrong is how long the programs take to speak. Time for a held note to reach 3 dB under its peak: grand piano 3 to 14 ms, flute 55 to 67 ms, violin 274 ms, cello 366 to 464 ms. The median note in that movement is 125 ms, 86 per cent of its notes are shorter than the violin's attack and 96 per cent shorter than the cello's.

So a note of the length the music actually has, at pp, reaches: piano 0.890 V, flute 1.577 V, violin 0.673 V, cello 0.566 V, string ensemble 0.565 V. The strings lose 8 to 9.5 dB that the keyboard and the wind do not.

It cannot be corrected from outside the bank. CC 73, attack time, does nothing in this FluidSynth build — identical to three decimals at every setting. Setting GEN_VOLENVATTACK directly does work for the string ensemble, taking a 125 ms note from 0.93 V to 2.34 V against a held 2.73, and does nothing at all for violin, cello, flute or piano, because in those programs the slow onset is in the recorded sample rather than in the envelope.

**This is left alone deliberately.** The band is for checking that a score reads and plays, not for performance, and the fixes available are all worse than the fault: raising the level of short notes would paper over an articulation the samples cannot produce, and holding them longer would turn détaché into legato. A sampled bank of this size does not have a quiet short bowed note in it. It is the reason for `bowed-strings.md`.

Two things follow for the benches. `leveltest` measures held notes at one key on three programs, so it cannot see any of this; a note of a realistic length, on a sustained instrument, is the measurement that was missing. And a loudness measure rather than a peak would have shown it sooner, which is the LUFS bench still listed below.

## The limiter, and what is left for the soft clip

**They were the wrong way round.** The limiter held at twelve volts and the soft clip bent from eight and a half, so everything between the two went through the waveshaper with no gain reduction at all. A waveshaper is distortion; a limiter is a change of gain. The curve should catch the millisecond the limiter cannot reach, not do the work.

**And the note beside it was wrong about the size of the problem.** It said the loudest attack in the scores in use reaches about six volts, so the limiter never acted on an ordinary chord. That came from multiplying one note, which is not how a chord sounds — five recordings do not line their peaks up sample for sample. Summing the actual recordings, a five-note chord at fff peaks at:

| | one note | chord, every hammer at once | chord, spread as a player does |
|---|---|---|---|
| A1 | 8.4 V | 13.8 | 12.3 |
| middle C | 7.9 | 10.2 | 14.0 |
| F sharp 6 | 9.8 | 20.7 | 19.0 |

So it acts on every loud chord there is, and it is not only the safety net for a pedalled pile-up it was written as. `LIMIT_VOLTS` is now `MPX_CLIP_FROM`: the gain comes down where the curve begins, and the curve sees only the front of a transient.

**What cannot be had.** A single note at eight volts and an unclipped five-note chord do not fit together under a ten-volt ceiling — the chord would need the single note down at about three, which puts ppp back where it was inaudible. Loud chords are limited, and the question was only whether that is done by moving the gain or by bending the waveform.

## The soft clip, and what is left for the limiter

**Nothing may pass ten volts**, where the audio interface stops. The question is what to do with the peaks that would.

**A limiter is the wrong tool for most of them.** It pulls the whole signal down for a sixth of a second when a peak arrives, so one loud chord ducks the sustain of everything around it. What goes over on a piano is the hammer attack, a couple of milliseconds long: round that and nobody hears it, while ducking the sustain behind it is plainly audible.

So the last volt and a half is a curve. `mpxSoftClip` leaves everything below 8.5 V untouched — a single note at fff is eight volts and never reaches it — and bends anything above towards ten without arriving. A tangent, so it meets the straight part with the same slope and does not announce itself at the knee.

| in | 8.0 | 8.5 | 9.0 | 10.0 | 11.4 | 13.3 | 16.0 V |
|---|---|---|---|---|---|---|---|
| out | 8.00 | 8.50 | 8.98 | 9.64 | 9.94 | 10.00 | 10.00 V |
| | 0 | 0 | -0.02 | -0.32 | -1.19 | -2.48 | -4.08 dB |

The thickest chord anybody writes arrives near thirteen volts and leaves at ten — two and a half decibels of rounding on the loudest moment of a piece and nothing anywhere else.

**What is left for a limiter is a plateau rather than a peak**: a texture sitting over the ceiling for whole bars while the pedal holds everything on, which is the case a clipper handles badly. mpxPiano holds that at twelve volts and mpxGuitar at fourteen, high enough never to touch an ordinary loud chord.

## Still to do

**Notes accumulating under the pedal are not counted.** Only notes struck together are, which is the easy case and most of the fault. A texture building up under a held pedal still adds, and it is what the limiter exists to catch.

**The share is counted per instrument, not per score.** Each module sees only its own cable, so in a quartet the four parts do not know about each other. That is the right answer for balance — the written dynamics decide it — but it means a tutti chord spread across four modules is not shared out as one chord.


**mpxGuitar's densest texture does not fit the way the piano's does.** Six open strings at full come to seven decibels over a single string, against the piano's four and a half for its thickest chord, so a full strum arrives near eighteen volts and the clip rounds it by five. That is heavy. Either the guitar wants a lower figure per note than the shared standard, or a strum wants its dynamic shared out more strongly than a piano chord — one gesture of one pick is not six fingers. Worth deciding before anybody plays loud strummed music through it.

**A loudness bench over the whole library**, reporting LUFS rather than peak, so the figures above are watched rather than remembered. The measurements here were made outside the repository, which is the wrong place for them.
