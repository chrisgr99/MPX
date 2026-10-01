# Guitar voice

A module that plays one MPX track, usually a Guitar Chart row, through an oscillator of the user's choosing, with the whole performance applied: bends, vibrato, slides, envelopes, dynamics, articulations, filtering and stereo placement. The user supplies only the sound source.

Name: mpxGuitarVoice.

Status: phases 1 to 3 built; phase 4 to do.

## Purpose

Playing a Guitar Chart track expressively through ordinary VCV modules takes mpxGuitarist or mpxOut, a polyphonic oscillator, a polyphonic envelope, a polyphonic filter, a polyphonic amplifier and a polyphonic-to-stereo mixer, with the control voltages patched between them correctly. This module replaces everything except the oscillator. The patch becomes two cables between this module and an oscillator, and a stereo pair to the mixer.

## Signal path

1. The MPX input takes one track. The performer library (src/Perform.hpp), the same one mpxGuitarist and the built-in band use, turns its notes into a schedule per voice.
2. The pitch output sends each voice's pitch, one polyphonic channel per voice, to the user's oscillator.
3. The oscillator's polyphonic audio comes back into the return input, channel for channel.
4. Each voice's audio goes through its own low-pass filter, then its own amplitude envelope and level.
5. Each voice is panned and the voices are mixed to the stereo output pair.

A voice is a string of a fretted part, so a six-string part uses six channels whether or not all six sound. A part with no strings, such as a piano or a voice, takes a channel for each note it holds, up to the performer's limit of twelve. The channel count of the pitch output follows the part, and is published as soon as the part's instrument arrives on the cable.

A mono return, one channel, is used for every voice. It is a fallback for an oscillator that is not polyphonic: such an oscillator plays one pitch, so only a single-note part sounds right through it.

## What the performance does

The performer decides when each note starts and stops, how loud it is, and what each articulation does. The module applies those decisions.

- **Pitch** carries bends, vibrato, slides, hammer-ons and pull-offs. It is the performer's pitch as played, in volts per octave.
- **A note start** triggers the voice's amplitude envelope and filter envelope at the note's level.
- **A note end** releases both envelopes. Let-ring notes end later, palm mutes and staccato notes sooner, because the performer has already moved the note's end.
- **A hammer-on, pull-off or legato slide** does not trigger the envelopes again. The pitch moves under an envelope that is already sounding, as on a string.
- **Level changes** during a note scale the voice's amplitude.
- **Timbre**, from dark for a palm mute or dead note to bright for a harmonic, moves the voice's filter cutoff.
- **Pan** comes from the track's pan in the file and from the note's own pan, and places the voice in the stereo mix.
- **Humanise** is fixed at the performer's normal amount, as in the built-in band.

## Controls

### Amplitude envelope

Applied to each voice's audio inside the module.

- **Attack**: how quickly a note reaches its level. Near zero is a hard pick; longer is a swell.
- **Decay**: how quickly a held note dies away, from a short muted plunk to a long sustain. A string decays towards silence while it is held; there is no sustain level.
- **Release**: how quickly a note stops once it ends.

### Filter

One low-pass filter per voice.

- **Filter**: a switch. Off, each voice's audio goes to its envelope as the oscillator made it.
- **Cutoff**: the base cutoff frequency.
- **Resonance**: emphasis at the cutoff.
- **Slope**: a switch between 12 and 24 dB per octave.
- **Key tracking**: how far the cutoff follows each voice's pitch. At full, the cutoff rises an octave for each octave of pitch, so a setting that suits the low strings does not muffle the high ones. On by default.

### Filter envelope

Moves each voice's cutoff over the course of its note.

- **Attack** and **Decay**: its shape. A short decay gives the bright attack and darkening tail of a plucked string. It falls over its decay whether the note is held or not.
- **Depth**: how far it moves the cutoff, in octaves.

The cutoff of a voice is the cutoff control, plus key tracking times the voice's pitch, plus the performer's timbre, plus the filter envelope times its depth, all measured in octaves.

## Inputs and outputs

- **MPX in**: one track.
- **Pitch out**: polyphonic, volts per octave, one channel per voice.
- **Return in**: polyphonic audio from the oscillator, one channel per voice; mono is accepted.
- **Left and right out**: the mixed voices.
- **Envelope out**: each voice's filter envelope as 0 to 10 V, polyphonic with the same channels as the pitch output, for driving other modules in time with the plucks.

All nine knobs apply to every voice; each voice runs its own copy of the envelopes, so the strings of a chord can be at different points in them. The panel is 14 HP, the knobs in three columns: the amplitude envelope, the filter, and the filter envelope.

## Engine

The envelopes, the filters and the mix are a library with no Rack in it, as the performer is, so they can be tested from the command line: a test renders notes through a fixed waveform and checks the envelope timings, the filter's response at both slopes, key tracking, and that a legato note does not retrigger the envelopes.

## Build phases

1. **Pitch and amplitude.** The engine library with the amplitude envelope, its command-line test, and the module with the MPX input, the pitch output, the return input, per-voice envelope and level, pan and the stereo mix. Attack, decay and release on a plain panel. At the end of this phase a Guitar Chart track plays through any polyphonic oscillator with the right notes, bends, dynamics and articulations.
2. **Filter.** The per-voice low-pass filter with cutoff, resonance, the 12 and 24 dB slope switch, key tracking and the performer's timbre, added to the engine, the test and the module.
3. **Filter envelope.** Its attack, decay and depth.
4. **Finish.** The panel laid out with the panel tools, the help text written as //? comments, the plugin.json entry, the module's name chosen, and a demonstration patch with a Guitar Chart and a stock oscillator.

Each phase ends with something that can be heard and judged in Rack before the next one starts.
