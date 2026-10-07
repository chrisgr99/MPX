# Bowed strings

A module that plays one MPX part on a physically modelled bowed string instrument: violin, viola, cello or double bass. The string, the bow, the body and the bowing are all inside it, as they are in mpxGuitar.

Names: not fixed. One module while the engine is unproven, then one slug per instrument.

Status: designed, nothing built.

## Purpose

mpxGuitar models a plucked string. A bowed string is a different problem and a harder one, and it is the gap in the plugin's instruments: everything here either strikes a string, plucks one or plays a sample.

It is also the instrument family where the research supports a design best. The acoustics of the bowed string have been measured for a century and a half, and the results that matter are specifications rather than descriptions — they say which combinations of bow force, bow position and bow speed produce a musical note and which do not. That makes the model testable before anybody listens to it, which is how the piano benches earned their keep.

## Why this is a better prospect than the modelled piano was

The piano model was deprecated because it did not reach a usable sound. Its design and the measurements behind it are on the `piano-model` branch, in `docs/piano-model.md`; nothing of it is on main. The failure mode there was unforgiving: a piano that is slightly wrong sounds like a synthesiser imitating a piano.

A bowed string is more tolerant. Helmholtz motion — one kink travelling around the string, giving a sawtooth of velocity at the bow — is a strong attractor, and a string falls into it from a wide range of conditions. A rough bowed model still sounds bowed.

The difficulty moves rather than disappearing. A plucked or struck string is an initial-condition problem: set it going and the instrument does the rest. A bowed string is driven continuously through a strongly nonlinear loop, and the player is in that loop for the whole of every note. The hard part will be the bowing, not the string.

## The string

A waveguide, as in mpxGuitar and the deprecated piano: a delay line a period long, a loss filter, and an allpass chain for dispersion. The bow is a nonlinear junction partway along it, so the string is two delay lines — bridge to bow, bow to nut — with the friction solved where they meet.

Torsional motion is real on a bowed string and affects the friction, because the string twists as well as moves sideways under the bow. It is a second waveguide at a much higher wave speed. Not in the first version; noted because it is a known reason for a model to sound thin in the attack.

Stiffness matters less than on a piano and is still present. The allpass chain covers it.

## The bow

Friction between rosin and string, solved once per sample at the bow's contact point.

The classical model makes friction a function of the sliding speed alone: a curve with a peak at zero relative velocity, falling as the string slips faster. It is simple, it produces Helmholtz motion, and it is wrong. Measurements of rosin show the friction is thermal — the rosin layer heats under the bow and softens, so the force depends on the temperature history of the contact, not on the instantaneous speed. The velocity curve gets steady tone approximately right and transients wrong, and transients are where a bowed instrument is recognised.

Build the velocity curve first because it is a known quantity and the rest of the model can be exercised against it. Treat the thermal model as the planned replacement rather than as an improvement that may never come.

The bow is not a point. The hair covers about a centimetre of string, which is why some effects need several contact points. Out of scope for the first version.

## What makes it playable, and how that is tested

Two published diagrams define the model's acceptance tests. Both are pass or fail per combination of controls, which means a bench can sweep them and report a region rather than a number.

**Schelleng's diagram** maps bow force against the bow's distance from the bridge. Below a minimum force the string never sticks and the sound is thin and unstable; above a maximum it never slips and the sound is a crunch. Between them is Helmholtz motion. Both limits move with the sounding point, which is why a player bowing near the bridge must press harder. The limits scale with the string's wave impedance, so the same rule gives the four instruments their different playable ranges.

**Guettler's diagram** does the same for attacks: bow acceleration against bow force, with a wedge-shaped region where the note starts cleanly and areas either side where it starts with multiple slips or with a scratch.

A model that reproduces both diagrams is playable whatever it sounds like. A model that does not is unplayable however good a single note sounds. The benches come before the sound.

## The body

A modal bank and a radiation filter, as in the deprecated piano's soundboard, where that approach was measured and worked. The violin family's modes are documented in detail: the air resonance, the corpus bending modes, and the bridge's own resonance around three kilohertz, which shapes the whole timbre.

The bridge is not a boundary condition here but a filter with a strong resonance of its own, and it is the one component that most distinguishes the instruments.

## The player

The bowing is a performance, and the plugin already separates performance from instrument — see `players.md` and `guitar-player.md`. The same division applies: the score says what is written, the player decides what the bow does, the instrument decides what that sounds like.

### A slur is a bow stroke

For a pianist a slur is a phrasing instruction. For a string player it is physical: those notes are played in one bow. That makes a score far more specific about bowing than it looks.

Measured on the Fauré piano quartet, first movement, which has violin, viola and cello parts:

| marking | count |
|---|---|
| slurs | 2,430 |
| hairpins | 570 |
| dynamics | 337 |
| tremolo | 87 |
| accents | 63 |
| staccato | 32 |
| up-bow and down-bow | 0 |
| string numbers, fingerings, harmonics | 0 |
| pizz and arco | 12 each, as words |

MusicXML can carry up-bow, down-bow, string number, harmonic and pizzicato as elements. This engraving, which is typical of what is available, carries none of them. It carries the slurs, and the slurs are the bowing.

`pizz` and `arco` arrive as words rather than as elements, which is the same reading problem as the worded crescendos in `guitar-pro.md` and is solved the same way.

### The bow budget

The control model follows from the slur. A bow has a finite length, so within one slur the player has a fixed amount of it to spend across the notes under that slur. Bow speed is then how much bow remains divided by how much time remains, and force follows from the dynamic and the sounding point, bounded by Schelleng's limits.

Bow direction is not written and is inferred. The convention is a down-bow on a strong beat, with the slur grouping deciding where alternation resumes.

### What the cable already carries

MPX has per-note pressure and timbre lanes that the piano ignores, because nothing on a piano responds to either once a key is down. A bowed instrument is the opposite: it wants continuous control throughout every note. Pressure maps onto bow force and timbre onto the sounding point.

## Sample rate

A waveguide's delay line is one period long, so the string's pitch decides how much resolution the model has:

| | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|
| cello C2, lowest | 734 | 1,468 | 2,936 |
| cello A3, top string | 218 | 436 | 873 |
| violin G3, lowest | 245 | 490 | 980 |
| violin E5, top string | 73 | 146 | 291 |
| violin E7, high position | 18 | 36 | 73 |

Tuning is not the problem — fractional delay by Lagrange interpolation handles that, as it did in the piano. Two other things fail first.

The loss filter needs a band to work in, and at eighteen samples a period there is almost none.

And the slip can only be timed to the nearest sample. Sticking and slipping once per cycle is what produces the waveform, so the timing of that one event is the waveform. One sample is 1.4 per cent of the cycle on the violin's top string at 48 kHz and 5.5 per cent in high positions.

**The module oversamples internally rather than requiring the engine to run faster.** It costs processing only where the problem is, instead of speeding up every module in a quartet patch to fix one of them; the factor can follow the pitch being played, so a low cello note costs nothing and a high violin note gets four or eight times, which a fixed engine rate cannot do; and it works for somebody who does not know the problem exists. Running Rack at 96 kHz lets the module use a lower factor and so makes it cheaper, which belongs in the help text as an optimisation and not as a requirement.

The schedule — how much oversampling at which pitch — comes from sweeping Schelleng's diagram at one, two, four and eight times and finding where the playable region starts to deform. Not from the table above, which only says where to expect trouble.

## One engine, four instruments

The physics is identical for violin, viola and cello. What differs is constants: string length, tuning, mass per unit length, wave impedance, body modes, and the force limits, which scale with impedance.

The double bass is the exception. It is tuned in fourths — E1 A1 D2 G2 — where the others are in fifths, its bow and technique differ, and its impedance is far higher. Same engine, its own parameter set.

| | tuning |
|---|---|
| violin | G3 D4 A4 E5 |
| viola | C3 G3 D4 A4 |
| cello | C2 G2 D3 A3 |
| double bass | E1 A1 D2 G2 |

Build one module first. Add the others through the pattern `mpxGuitarChart` and `mpxGuitarChart6` already use: one engine in a header, a short source file per slug setting its constants. Four slugs cost four short files and give four entries in the module browser, which matters because somebody looking for a cello searches for a cello.

Slugs are permanent, so do not mint four of them for an engine whose shape may still change.

**Start with the cello.** At 48 kHz its periods run from 218 to 734 samples, where the violin's top string gives 73 and its high positions 18. Get the physics right where it is easiest to observe, then scale up and find out what the violin additionally needs. The Fauré quartet provides a cello part to test against.

The physical constants are parameters, not four hardcoded sets, so that a five-string bass or an unusual viola is possible.

## What is not being taken from elsewhere

**Artifastring** is a rigorous modal-synthesis library for violin, viola and cello. It is GPLv3 and this plugin is MIT, so it cannot be linked without relicensing everything. Ruled out on licence, not on quality.

**Faust's physical modelling library** includes a bow table and bowed-string models, useful as a cross-check on structure.

**STK's `Bowed`** is the classic simple implementation under a permissive licence. It uses the velocity-curve friction model, so it is a reference for shape and not for physics.

The literature is where the value is, because Schelleng and Guettler give testable targets and no codebase does.

## Tests

Benches before the module, as the piano model's were:

- Schelleng sweep: for a grid of bow force and sounding point, does the string reach Helmholtz motion, and how long does it take.
- Guettler sweep: for a grid of bow acceleration and force, does the attack start cleanly, with multiple slips, or with a scratch.
- Oversampling: both sweeps at one, two, four and eight times, to decide the schedule.
- Tuning and decay, as for the other string models.

## Still to come

Torsional waves. A finite-width bow. The thermal friction model. Sympathetic resonance between the four strings, which the piano model showed is cheap once the strings share a bridge. Pizzicato, which is a different excitation of the same string and nearly free. Mutes, harmonics, and the sounding-point extremes of sul tasto and sul ponticello, all of which are parameter moves rather than new mechanisms.
