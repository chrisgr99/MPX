# A physically modelled piano

mpxPiano plays the Salamander sample set. This is the other thing: the instrument itself, modelled, so that what it does under the hands is a consequence rather than a setting. Sampled pianos are convincing until you ask them something the samples were not recorded doing — a key released while the pedal is half down, a note struck again before its damper has landed, a chord that rings sympathetically with one held silently. All of those are ordinary piano playing and none of them is in a sample set.

**It models the whole instrument, not the notes being played.** All two hundred-odd strings exist all the time, damped or not, coupled through one bridge. Sympathetic resonance is then not a feature that was added: it happens. The sustain pedal works because the dampers lift and the strings are already there. The undamped top two octaves ring along by themselves. A string that is not moving costs nothing, and most of them are not moving most of the time.

**The target is Bach and Chopin**, which is a demanding pair. A fugue wants four voices that stay separate, repeated notes that speak, and dampers that actually stop the string. Chopin wants the pedal — including half-pedalling — the soft pedal, a wide dynamic range where loud is *brighter* and not merely louder, and the resonance of everything that is not being played.

## What carries over, and what does not

`GuitarString` is a digital waveguide: a delay line closed through a loss filter, with an allpass chain for stiffness and bridge coupling through `drive()` and `bridge()`. That is the right structure and it is also how piano strings are modelled. What it does not have is everything that makes a piano a piano.

**The hammer.** Felt is a nonlinear spring: the force it returns goes as its compression raised to a power of about two and a half. A hard blow compresses it further, which makes it stiffer, which *shortens* the contact to a millisecond or two and puts energy much higher up the spectrum. This is why a piano brightens when played harder rather than only getting louder, and no envelope or filter reproduces it. It has to be solved against the string sample by sample, because the string pushes back while they are in contact.

**Several strings a note, coupled.** One in the bass, two in the tenor, three in the treble, slightly mistuned and joined at the bridge. In phase they dump their energy into the bridge quickly; out of phase they hang on. That is the piano's double decay — a fast initial fall into a long aftersound — and it is a property of the coupling, not an envelope shape.

**Inharmonicity that varies across the compass.** A guitar has one stiffness number. A piano's partial stretch is least around the middle and rises toward both ends — steeply in the bass, and again at the top where the strings are very short. It is why pianos are tuned stretched, and getting it wrong makes the instrument sound like something else.

**The soundboard**, which is not a tone control but the radiator and the path by which one string reaches another.

**The dampers**, per note, felt, and absent from the top two octaves.

## The phases

### Phase 0 — the measuring bench

Before any of it sounds, the means of telling whether it is right. As with the path reading and the mechanism, a fault in this is a sound that is merely odd, and odd cannot be debugged by ear.

A command-line harness that drives one string or the whole instrument and reports: the frequencies of the first twenty partials and the stretch coefficient fitted to them; the time each partial takes to fall sixty decibels; hammer contact time against blow speed; the spectral centroid against blow speed; the beat rate between unison strings; and the total energy in the board.

Published figures for a real grand are the targets, gathered in this phase and written down here so that later work is measured against numbers rather than against memory.

### Phase 1 — one string, measured

The waveguide extended for piano use: enough dispersion for the bass, a loss filter whose decay varies with frequency as a real string's does, and a tuning that accounts for the delay the dispersion itself adds.

**Done when** the partial stretch of a modelled string matches the published coefficient for that note across the compass, and the decay times fall in the measured range for a grand.

### Phase 2 — the hammer

A felt hammer with a nonlinear characteristic, in contact with the string for as long as the physics says and no longer. Struck at a point along the string, which is what suppresses the partial whose node is near the strike point — the reason the seventh and ninth partials are weak on a real piano.

**Done when** contact time falls from about four milliseconds at a whisper to about one at full force, the spectral centroid rises with blow speed, and the partials near the strike point are missing.

### Phase 3 — the choir

One, two or three strings a note as the scale demands, mistuned by a fraction of a cent to a cent or so, coupled through a bridge of finite impedance.

**Done when** a single note shows the double decay, and the beat rate between unisons follows the detuning as it should.

### Phase 4 — the whole board

Eighty-eight notes present at once, all coupled, with a string whose energy is below hearing skipped entirely so that silence is free.

**Done when** the instrument runs at a sensible cost with every damper up, and striking one note measurably excites the strings a twelfth above it.

### Phase 5 — dampers and pedals

Felt dampers that take time to land and take time to lift, so a note struck again before its damper has arrived behaves as it does on a piano. The sustain pedal as a continuous quantity — half-pedalling is a damper resting lightly on a string, not a switch. The soft pedal as what it physically is: the action shifted so the hammer strikes fewer strings of the choir and on a less-used part of its felt, which changes the tone and not only the level. Sostenuto if it earns its place.

**Done when** a chord held silently and then struck elsewhere rings sympathetically, and a pedal moved part way gives a part-way result.

### Phase 6 — the soundboard

A radiation filter with its own modes, and a bridge impedance that makes the string-to-string coupling right rather than approximately right. This phase is where the instrument stops sounding like strings and starts sounding like a piano in a room.

### Phase 7 — playing it

Taking the cable's notes as a pianist's hands: the velocity becoming a hammer speed rather than a gain, the duration becoming a key release, the pedals coming from the cable, and the bend and timbre doing whatever they can honestly do on a piano.

**Done when** the Bach prelude plays with its sixteenths separate and a Chopin nocturne pedals convincingly.

### Phase 8 — the noises

The key reaching its bed, the damper landing, the hammer returning, the pedal mechanism. Quiet, and the difference between an instrument and a tone generator.

### Phase 9 — the panel

Stretch tuning as a curve rather than a number. Voicing — how hard the felt is, per register, which is what a technician actually adjusts. Enough to shape the instrument without turning it into a synthesiser.

## Known hard parts, named now

**The hammer–string loop.** The hammer's force depends on the string's position and the string's response depends on the hammer's force, in the same sample. Done naively it is unstable at high velocity. This is the part most likely to take several attempts.

**Tuning against dispersion.** The allpass chain that makes a string inharmonic also delays it, by an amount that depends on frequency, so a string tuned by its delay length alone comes out flat. The correction has to be worked out rather than guessed.

**Cost.** Two hundred waveguides with allpass chains is affordable only because most strings are still. The gating has to be cheap and must not click when a string wakes.

**The bass.** Wound strings are the most inharmonic, the most strongly double-decaying, and the ones where phantom partials are audible. They will be the last to sound right.

## Open

**Whether this replaces mpxPiano or stands beside it.** The sampler is good at what it is good at, and a modelled instrument is a different thing rather than a better one.

**What it is called.**
