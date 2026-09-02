# mpxEuclid — design

A four-voice Euclidean sequencer whose notes leave on one MPX cable. It produces no pitch. Each note carries a velocity and a duration taken from values that change slowly and repeat.

## What problem it addresses

Four rhythmic streams played on the same percussion sound are heard as one instrument unless each stream has its own continuity. A velocity drawn at random for every hit is heard as a single source behaving randomly. A velocity that is correlated with the previous velocity of the same stream is heard as a separate source.

The module therefore gives each voice its own slowly changing velocity and duration, independent of the other voices.

## Structure

Four voices. Each has a step count, a pulse count, an offset and a clock divider. All four share one set of controls governing velocity and duration, and one output.

The voices are independent because their value sequences differ, not because their settings differ. A single set of shared controls is sufficient.

## The clock

One pulse advances one step. A voice set to sixteen steps completes its cycle in sixteen pulses.

The Clock input takes any signal that crosses one volt. When nothing is connected, an internal clock runs at the rate set by the Tempo knob, in beats per minute. The Tempo knob has no effect while a cable is connected to Clock. The panel does not state this; the parameter tooltip does.

Reset returns every voice to step zero and returns the value sequences to their start.

## The Euclidean rule

A voice plays on step *i* when *i* multiplied by the pulse count, modulo the step count, is less than the pulse count. This produces the same patterns as Bjorklund's algorithm without constructing them.

The offset is added to the step index before the test, modulo the step count, so it rotates the pattern within its cycle.

A pulse count of zero silences the voice. A pulse count equal to or greater than the step count plays every step.

## The clock divider

Each voice counts master pulses and acts on every pulse, every second pulse, every third or every fourth. A divided voice takes correspondingly longer to complete its cycle.

## The value sequences

Each voice holds two sequences of 256 values in the range zero to one, one for velocity and one for duration. Each is generated from a fixed seed, so a patch produces the same result when it is reopened.

A position advances through the sequence with the master clock. Values between two points are interpolated with a smoothstep curve.

**Drift** sets how many beats separate one point from the next, from a quarter of a beat to sixteen beats. A large value produces slower change.

**Repeat** sets how many beats pass before the position returns to the start, from one to sixty-four. The number of points used is the repeat length divided by the drift, rounded to a whole number and limited to 256.

The position is taken from the master pulse count rather than from each voice's divided count, so every voice returns to the start of its sequence at the same time.

Each voice and each lane has a different seed. Sharing one sequence between voices would make them change together, which is the condition this design exists to avoid.

## Velocity and duration

Velocity is the Level knob displaced by the velocity sequence, scaled by Move:

    velocity = clamp(level + move × (v − 0.5) × 2, 0, 1)

Duration is derived from a mixture of the duration sequence and the velocity, in the proportion set by Weight:

    shape = d × (1 − weight) + velocity × weight
    seconds = length × 4 ^ (shape − 0.5)

At a Weight of zero, duration and velocity change independently. At a Weight of one, duration follows velocity exactly, so a harder hit produces a longer note. The Length knob sets the middle of the range, from ten milliseconds to four seconds; the exponent gives a range of half to twice that value.

The result is clamped between five milliseconds and thirty seconds.

## What a note carries

Pitch is zero. Pan is zero. Bend range is two semitones and is unused, since no bend is sent.

Level and duration are as calculated above. A note-off is sent when the duration has elapsed. The duration is also present in the note-on, so the receiving module ends the note whether or not the note-off arrives.

## Parameters

| Control | Range | Default |
| --- | --- | --- |
| Steps | 1 to 32 | 16 |
| Pulses | 0 to 32 | 3, 4, 5, 6 by voice |
| Offset | 0 to 31 | 0 |
| Clock | 1, /2, /3, /4 | 1 |
| Level | 0 to 1 | 0.7 |
| Move | 0 to 1 | 0.5 |
| Length | 0.01 to 4 seconds | 0.15 |
| Weight | 0 to 1 | 0.5 |
| Drift | 0.25 to 16 beats per point | 2 |
| Repeat | 1 to 64 beats | 16 |
| Tempo | 30 to 300 bpm | 120 |

## Its place in the plugin

It is an MPX source. It claims a bus slot, writes note events to it, and answers the question every source answers: which bus a given output writes to.

That question is asked of a capability rather than of a class, so this module required no change at the receiving end. Before it existed the lookup tested for toMPX specifically, which made the design's claim that any module can be a source true only on paper.

## Known limitations

The pan, pressure and timbre lanes are unused. Pan in particular would strengthen the separation between voices, since a per-note position is carried by the cable already.

Values are sampled at the start of a note and do not continue during it. Nothing is sent on the pressure or timbre lanes while a note sounds.

Voice identity is not preserved at the receiving end. The unbundler allocates notes to whichever of its voices is free, so a note's channel does not identify which sequencer voice produced it. This does not affect the sound, because velocity and duration travel with the note; it would matter if the receiving patch processed its channels differently.

Timing is exact. There is no swing and no displacement of individual hits.

The Tempo knob is inert whenever a cable is connected to Clock, and the panel does not indicate this.
