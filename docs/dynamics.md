# Dynamics

How loud a note is, from the mark on the page to the volts at an instrument's output.

## The ladder

**A marking names a number of decibels, and it names the same number in every score.** Forte means the same thing in a piece that never leaves mezzo-forte as in one that reaches fff; a piece written quietly is meant to be quieter. Nothing normalises a score against itself.

`MPX_RANGE_DB` in `NoteBus.hpp` is 22: the whole span from ppp to fff. **Level is a decibel scale** — a note sounds 22 times `(1 - level)` decibels below `MPX_FULL_VOLTS` — so the eight markings are the eight levels evenly spread from nought to one, one seventh of the range apart, and `mpxDecibels` and `mpxLoudness` are the one conversion every instrument should use.

| | ppp | pp | p | mp | mf | f | ff | fff |
|---|---|---|---|---|---|---|---|---|
| level | 0 | 1/7 | 2/7 | 3/7 | 4/7 | 5/7 | 6/7 | 1 |
| dB | -22.0 | -18.9 | -15.7 | -12.6 | -9.4 | -6.3 | -3.1 | 0 |

`sf` sits between f and ff, `sfz` at ff, and `fp` strikes at f — the drop that follows is not read, so it is the f that is kept.

Twenty-two decibels and not the twenty-nine the recordings span on their own, which is a piano in a quiet room and leaves pianissimo close to inaudible in a rack. **mf and fff are where they were before this table**, deliberately: the eighteen scores carrying no dynamics at all sit at mf and should not move, and the top of an instrument should not move either.

A mark is a point and not a property of a note: it holds until the next one. A hairpin is a ramp whose destination is whatever mark comes after it, which may be bars away, so the loudness of every note is worked out after the whole part has been read. A ramp with nothing after it travels `HAIRPIN_REACH`, one seventh — one step of the ladder — and may not arrive below ppp.

A part with no mark anywhere sits at mf from the first bar to the last. Seventeen of the thirty-four scores in regular use are like that — every lead sheet, and the Bach prelude and fugue.

**The `sound dynamics` attribute is ignored.** Every score that carries written marks also carries these, essentially one per mark, so they add a dependency on whatever playback settings the engraver happened to have and no new information.

## What a level is worth

**`MPX_FULL_VOLTS`, in `NoteBus.hpp`: a single note at level 1, with the module's volume control where it starts, peaks at five volts.** That is Rack's nominal full scale for audio, so the loudest single note an instrument plays reaches the top of the scale, and a chord uses the headroom above it.

This is a statement about every instrument on the cable, not about one of them. The same number has to be the same loudness wherever it is read, or handing a part to a different instrument changes the music rather than the colour. Two modules playing the same part were once twenty decibels apart, which nobody hears as a fault — it reads as one of them being the wrong instrument for the music.

`test/leveltest.cpp` plays one note at level 1 through each instrument and fails when any is more than three decibels from the standard, that being about the smallest difference heard as loudness rather than as character.

As measured, all six now within the three decibels it allows:

| | peak | from the standard |
|---|---|---|
| mpxGuitar, steel | 5.01 V | 0.0 dB |
| mpxPiano | 5.00 V | 0.0 dB |
| mpxGuitar, nylon | 4.93 V | -0.1 dB |
| the band, steel guitar | 4.88 V | -0.2 dB |
| the band, grand piano | 4.01 V | -1.9 dB |
| the band, nylon guitar | 6.40 V | +2.1 dB |

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

Measured across eight registers from A1 to F sharp 7, forte now spans **0.0 dB**. The bench checks it.

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

| | wanted | piano | guitar | band's piano |
|---|---|---|---|---|
| ppp | -22.0 | -22.0 | -22.0 | -23.5 |
| pp | -18.9 | -18.9 | -18.9 | -19.6 |
| p | -15.7 | -15.7 | -15.7 | -15.9 |
| mp | -12.6 | -12.6 | -12.6 | -12.6 |
| mf | -9.4 | -9.4 | -9.4 | -9.3 |
| f | -6.3 | -6.3 | -6.3 | -6.3 |
| ff | -3.1 | -3.1 | -3.1 | -3.1 |
| fff | 0 | 0 | 0 | 0 |

Worst deviation 1.5 dB, at the band's quiet end.

**A level is a loudness and a velocity is not**, which is the trap here and it cost the quiet end of every score. ppp is nought on a decibel scale; handed to a string as a pluck strength it is no pluck at all, and handed to a synthesiser as a velocity it is silence. Both now convert through `mpxLoudness`.

The band needs one more step, because a velocity is not a loudness either. The SoundFont specification says amplitude follows the square of the velocity; swept through GeneralUser-GS the exponent is nearer **1.57**, and taking the specification at its word left pianissimo four decibels louder than asked. The bench prints the sweep the constant came from, so it can be refitted if the bank changes.

## The limiter

mpxPiano's output used to be hard-clamped at twelve volts, which is not a limiter: it flattens the top of the waveform, and that is distortion arriving exactly on the loudest chord of the piece and on nothing else. mpxGuitar had no limit at all and left it to whatever was downstream.

Both now limit at **ten volts**, twice Rack's nominal level for audio. A millisecond to take hold, a sixth of a second to let go, both channels by the same amount so a loud note on one side cannot pull the image across. The piano keeps its clamp behind the limiter, because nothing with an attack time is instantaneous and the front of a transient still gets through.

**It is a safety net and not a tone control.** The loudest attack in any score in use reaches about six volts. What it is for is the texture that no single marking asked for: on the piano, notes piling up under a held pedal; on the guitar, a full strum, which even with the chord's dynamic shared out reaches eleven volts against a single string's five.

## Still to do

**Notes accumulating under the pedal are not counted.** Only notes struck together are, which is the easy case and most of the fault. A texture building up under a held pedal still adds, and it is what the limiter exists to catch.

**The share is counted per instrument, not per score.** Each module sees only its own cable, so in a quartet the four parts do not know about each other. That is the right answer for balance — the written dynamics decide it — but it means a tutti chord spread across four modules is not shared out as one chord.

**A loudness bench over the whole library**, reporting LUFS rather than peak, so the figures above are watched rather than remembered. The measurements here were made outside the repository, which is the wrong place for them.
