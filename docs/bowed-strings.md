# Bowed strings

A module that plays one MPX part on a physically modelled bowed string instrument: violin, viola, cello or double bass. The string, the bow, the body and the bowing are all inside it, as they are in mpxGuitar.

Names: not fixed. One module while the engine is unproven, then one slug per instrument.

Status: phases 1, 2, 4, 5, 6 and 7 built — the string, the bow, the pitch range with its oversampling schedule, the body, the instrument with four strings on one bridge, and the player, settled by ear on the violin and the cello; phase 8, the module, built as mpxViolin and not yet played in Rack. Phase 3's benches stand as they were left: with the friction curve held notes play where Schelleng's diagram says a cello does and notes do not start the way Guettler's says they should, and the player is what starts them. `make bowtest`, `make bowplay`, `make bowinstrument`, `make bowplayer`; the sources checked for all of it are in `research/bowed-strings/sources.md`. The build is in ten phases, at the end of this document; the violin ships first.

## Purpose

mpxGuitar models a plucked string. A bowed string is a different problem and a harder one, and it is the gap in the plugin's instruments: everything here either strikes a string, plucks one or plays a sample.

It is also the instrument family where the research supports a design best. The acoustics of the bowed string have been measured for a century and a half, and the results that matter are specifications rather than descriptions — they say which combinations of bow force, bow position and bow speed produce a musical note and which do not. That makes the model testable before anybody listens to it, which is how the piano benches earned their keep.

## Why this is a better prospect than the modelled piano was

The piano model was deprecated because it did not reach a usable sound. Its design and the measurements behind it are on the `piano-model` branch, in `docs/piano-model.md`; nothing of it is on main. The failure mode there was unforgiving: a piano that is slightly wrong sounds like a synthesiser imitating a piano.

A bowed string is more tolerant. Helmholtz motion — one kink travelling around the string, giving a sawtooth of velocity at the bow — is a strong attractor, and a string falls into it from a wide range of conditions. A rough bowed model still sounds bowed.

The difficulty moves rather than disappearing. A plucked or struck string is an initial-condition problem: set it going and the instrument does the rest. A bowed string is driven continuously through a strongly nonlinear loop, and the player is in that loop for the whole of every note. The hard part will be the bowing, not the string.

## The string

A waveguide, as in mpxGuitar and the deprecated piano: a delay line a period long, a loss filter, and an allpass chain for dispersion. The bow is a nonlinear junction partway along it, so the string is two delay lines — bridge to bow, bow to nut — with the friction solved where they meet.

The bridge side's round trip, its two delays and the bridge's reflection together, is exactly beta of the period, so the bow is where it is asked to be. Anything with a delay of its own that does not belong to the bridge side sits at the nut, at the far end of the long segment.

**The losses.** The bridge takes the fundamental's loss, the same at every frequency: the bridge's resistance, Schelleng's R, where the energy leaves for the body. What the string loses inside itself grows with frequency and is spread along it, so it is shared between the two ends by the length of string on each side, beta of it at the bridge and the rest at the nut; a ripple running between the bow and the bridge loses what that much string would. Both ends pass a steady velocity back exactly, so the string cannot drift sideways.

**The string twists.** The friction acts on the string's surface, so the bow turns the string about its axis as well as pushing it, and the twist travels as waves of its own. It is a second waveguide, split at the bow as the first is, its waves 5.2 times as fast; the bow sees the two impedances in parallel, and what it adds divides between the two motions as their admittances do. The twist's partials all die at the same Q of 45, so its high ones go fast, and it carries away much of what would otherwise ring between the bow and the bridge. Woodhouse and Loach's cello D: 1.8 kg/s against 0.55, which together give the 0.42 Galluzzo used for the same string.

Stiffness matters less than on a piano and is still present. The allpass chain covers it.

## The bow

Friction between rosin and string, solved once per sample at the bow's contact point.

The classical model makes friction a function of the sliding speed alone: a curve with a peak at zero relative velocity, falling as the string slips faster. It is simple, it produces Helmholtz motion, and it is wrong. Measurements of rosin show the friction is thermal — the rosin layer heats under the bow and softens, so the force depends on the temperature history of the contact, not on the instantaneous speed. The velocity curve gets steady tone approximately right and transients wrong, and transients are where a bowed instrument is recognised.

Build the velocity curve first because it is a known quantity and the rest of the model can be exercised against it. Treat the thermal model as the planned replacement rather than as an improvement that may never come.

The string has two friction models. The friction curve plays; the bristles stay for comparison on the benches. Starting notes cleanly is the player's job, as it is a cellist's (phase 7).

**The friction curve**: Woodhouse and Galluzzo's hyperbola, static 0.8 falling to 0.4, halfway at 0.2 m/s, which they fitted to steady bowing. It plays held notes where cellos play them and does not start them as cellists do; the steeper curve Smith and Woodhouse measured for rosin sliding steadily starts them better and holds them worse.

**The bristles** (`BowedString::setBristles`): the elasto-plastic model of Dupont and colleagues (2002), as Serafin, Avanzini and Rocchesso put it at a waveguide's bow (2003) and Matusiak and Chatziioannou fitted it to a cello string (2024). The rosin contact is a field of elastic bristles whose mean bend is the model's memory: while the bend is small they only bend and the string moves with the bow, past a breakaway bend they start to break, and at the steady bend for the sliding speed all are broken and the string slides at the friction of a Stribeck curve. So whether the string sticks depends on how the contact got where it is, which a curve cannot say. The bow hair's own give, a spring and damper between the stick and the rosin, is part of it. Solved each sample for the bend's rate by Newton's method kept inside a bracket, the bend stepped by backward Euler because the bristles settle in microseconds. Under a heavy, slow bow it lets the string creep, a few centimetres a second behind the bow at the static friction, before it flies back — which a curve does not do.

The bow is not a point. The hair covers about a centimetre of string, which is why some effects need several contact points. Out of scope for the first version.

## What makes it playable, and how that is tested

Two published diagrams define the model's acceptance tests. Both are pass or fail per combination of controls, which means a bench can sweep them and report a region rather than a number.

**Schelleng's diagram** maps bow force against the bow's distance from the bridge. Below a minimum force the string never sticks and the sound is thin and unstable; above a maximum it never slips and the sound is a crunch. Between them is Helmholtz motion. Both limits move with the sounding point, which is why a player bowing near the bridge must press harder. The limits scale with the string's wave impedance, so the same rule gives the four instruments their different playable ranges.

**Guettler's diagram** does the same for attacks: bow acceleration against bow force, with a wedge-shaped region where the note starts cleanly and areas either side where it starts with multiple slips or with a scratch.

A model that reproduces both diagrams is playable whatever it sounds like. A model that does not is unplayable however good a single note sounds. The benches come before the sound.

## The body

`src/BowedBody.hpp`, `.cpp`. The force the strings put on the bridge in, the sound a metre away out — the body's radiativity, in pascals per newton, which is how the measurements describe a violin's voice. As the deprecated piano's soundboard was built: the low modes, the instrument's signature, a resonator each; the dense ones above, where they overlap and no one of them is heard, generated from the body's modal density and damping, deterministically, so an instrument is the same instrument every time it is built. Each mode carries both how much it radiates and how readily it lets the bridge move.

**The body's voice is one way; its low modes move the bridge.** What is heard is the whole body's radiation, driven by the force on the bridge. What the strings not being bowed feel is the bridge moving with the body's low modes, where they are few and each one counts — the signature modes, with their measured mobility (phase 6). The dense modes above stay out of that loop: the strings' own losses were set to what strings on instruments measure, and the dense modes in the loop as well would count those losses twice.

The bridge's own resonance, its rocking at about three kilohertz, makes the bridge hill: the broad rise near 2.4 kHz that much of a violin's brilliance comes from. The violin's measured radiativity has it in already, so there is no bridge filter on top; the body offers one, for an instrument built from its mobility rather than from a measured voice.

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

**The schedule**, measured in phase 4: run the string fast enough that a period is at least three hundred samples, in powers of two, at most sixteen times (`BowedString::oversampling`). At 48 kHz that is none up to 160 Hz, so the cello's three lower strings; two times to 320 Hz; four to 640, eight to 1,280, and sixteen above, to the top of the violin's E string. A string costs 18 to 40 nanoseconds a sample, so at sixteen times one violin string takes about one and a half per cent of a processor core.

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

**The violin ships first, but the engine is exercised at cello pitches until the physics is right.** At 48 kHz a cello's periods run from 218 to 734 samples, where the violin's top string gives 73 and its high positions 18. Every measurement is easier to read an octave or two down. The constants are parameters, so this costs nothing: see the phases below.

The physical constants are parameters, not four hardcoded sets, so that a five-string bass or an unusual viola is possible.

## What is not being taken from elsewhere

**Artifastring** is a rigorous modal-synthesis library for violin, viola and cello. It is GPLv3 and this plugin is MIT, so it cannot be linked without relicensing everything. Ruled out on licence, not on quality.

**Faust's physical modelling library** includes a bow table and bowed-string models, useful as a cross-check on structure.

**STK's `Bowed`** is the classic simple implementation under a permissive licence. It uses the velocity-curve friction model, so it is a reference for shape and not for physics.

The literature is where the value is, because Schelleng and Guettler give testable targets and no codebase does.

## Tests

Benches before the module, as the piano model's were. `make bowtest` is the string and the bow; `make bowplay` is where it plays and how its notes start.

- Schelleng sweep: for a grid of bow force and sounding point, does a held note keep Helmholtz motion.
- Guettler sweep: for a grid of bow acceleration and force, does the attack start cleanly, with multiple slips, or with a creak.
- Oversampling: both sweeps at one, two, four and eight times, to decide the schedule.
- Tuning and decay, as for the other string models.

## Phases

The violin ships first. The engine's physical constants are parameters rather than four hardcoded sets, so which instrument ships first decides only which slug is minted, not how the engine is written — but it does decide where the early benches run, and running them at violin pitches from the start would be a mistake. A violin's top string gives 73 samples a period at 48 kHz and its high positions 18, where a cello's lowest string gives 734. Every measurement below is easier to read, and every fault easier to see, an octave or two down. So the engine is exercised at cello pitches through phase 3 and raised to violin pitches in phase 4, which is also where the oversampling schedule is decided. Nothing is thrown away by doing it in that order, and the cello module in phase 9 then arrives already tested.

Each phase ends with something measurable, and most of them with something to listen to. The piano model reached a usable design and never reached a usable sound, so each phase states what would make it worth stopping.

### 1. The string, with no bow — built

`BowedString.hpp` and `.cpp`. Four one-way delay lines, a pair each side of the contact point, so the travelling waves are available where the bow will go. The loss filter and the dispersion allpasses sit in the bridge reflection, crossed once a round trip as `GuitarString`'s are; the nut reflects 0.995 of what reaches it. Velocity waves, so phase 2 can add the bow's velocity to both outgoing waves. Excited by a pluck — the guitar's triangle, halved into the two outgoing waves — which is also the pizzicato.

`make bowtest` measures it. In tune within 0.8 cents over both instruments' ranges including high positions. The partials land on whole multiples within a tenth of a cent to the seventh, which Helmholtz motion will want and which there was no reason to assume. The contact point moves the fundamental by at most 0.06 cents anywhere from 0.02 of the length to the midpoint, and the partial with a node under the contact point is 33 to 45 dB down, so the split is a place on the string and not only an arithmetic division. Decay within 2.3 per cent of the time asked for. Sweeping the contact point while the string rings stays bounded, in tune, and shows no jump between samples bigger than the waveform's own slope.

Two things it cost. The round trip meets each end once, not twice, and counting the nut's reflection for both of the segments it borders left a string asked for six seconds ringing for nearly nine. And autocorrelation is the wrong way to measure this: it reads the lag at which the whole waveform repeats, which moves with whichever partials are loud, so it showed 1.4 cents of error at the midpoint where the fundamental had not moved at all.

The bench also prints how close to the bridge the contact point can be had, which is the figure phase 4 needs. At 48 kHz, with the shortest segment the interpolated read allows: cello C2 reaches 0.006 of the length, cello A3 0.019, violin G3 0.017, violin A4 0.039, violin E5 0.059, violin E6 0.125, violin E7 0.288. So a cello can be bowed anywhere a player would, and a violin cannot be bowed near the bridge above about the A string without oversampling.

### 2. The bow — built

The nonlinear junction at the contact point, solved once per sample, with the classical friction curve that depends on sliding speed alone. Bow force, bow speed and sounding point as explicit inputs.

**The junction** (`BowedString::bowJunction`): the hyperbolic friction curve with the published constants, and the hysteresis rule that keeps a stuck string stuck until the static limit is passed and a slipping one on its outer branch until that branch disappears, which matches the published closed form exactly. The losses are physical — the fundamental at a Q of about five hundred, as a cello's open strings measure, the string's own loss set per hertz and shared by length, each string its own measured impedance — and the string twists; see The string.

**What `make bowtest` measures.** A held cello D3 at a newton and 0.2 m/s, a tenth of the length from the bridge, settles into Helmholtz motion in about half a second and holds it: one slip a period, slipping for 0.113 of it against the 0.1 of the ideal sharp-cornered motion, as losses round the corner. It goes nowhere on average, is in tune to 1.2 cents, has nothing at half the pitch, and its harmonics fall as one over the harmonic number to within 2 dB up to the seventh. An up-bow is the same motion reversed, and a lifted bow lets the string ring down. Each string held at a force a player would use plays Helmholtz motion every period: cello C2 at 3 N, G2 at 1.5 N, A3 at 0.8 N, violin G3 and A4 at 0.5 N, E5 at 0.3 N. How soon it gets there is phase 3's question: the low strings take up to a second.

This is the first bowed sound. It will be thin, because there is no body, and it should already be recognisably a bowed string rather than a synthesiser, because Helmholtz motion is a strong attractor.

Bench: a held note at a plausible force and speed reaches Helmholtz motion and stays there; the velocity at the bow is a sawtooth; the fundamental is the string's pitch and not half or double it.

Worth stopping if: Helmholtz motion cannot be reached at any setting. That would mean the junction is wrong, not the parameters.

### 3. The acceptance benches — under way

Schelleng's sweep and Guettler's sweep, as command-line tests that print the playable region as a grid: `test/bowplay.cpp`, `make bowplay`.

This is the gate the whole design rests on, and it is the reason this is a better prospect than the piano was. A model that reproduces both diagrams is playable whatever it sounds like; one that does not is unplayable however good a single note sounds. Both are pass or fail per combination, so the bench reports a region and the region either has the published shape or it does not.

**The junction against Guettler's analysis — met.** On his simple string (a 5 ms period, 0.22 kg/s, nothing lost, his hyperbolic friction 1.0 falling to 0.5): the friction under a bow held stuck climbs in steps exactly as the count of waves returning from the two ends gives, along the slope of his equation (3) to 0.6 per cent; an accelerating bow first lets the string go when his (5b) says, to 0.08 per cent; and over the grid of his Figure 10 — six bow positions, 0.05 to 1 N, 0.5 to 9 m/s² — 80 per cent of the attacks inside his four limits A to D start perfectly and 1.6 per cent outside them do. The wedge widens away from the bridge as his does. His equations are for a bow at a whole-number fraction of the length, so each position runs at the period nearest 5 ms that it divides in whole samples: where the fraction falls between samples, the interpolated reads reshape the first slip's pulse a little on each trip, and by the ninth it triggers a stray slip.

**Schelleng's diagram, held notes — met where it was measured.** Galluzzo measured a cello D string bowed by a rosined rod at 0.05 m/s, each run started from Helmholtz motion: above 0.13/beta N it crunched, below 0.00086/beta² N it slipped more than once a period, both scaling with bow speed. The bench starts the modelled D3 in Helmholtz motion a tenth of the way from the bridge and moves it to each force and position. Over the range he measured, 0.02 to 0.1 of the length, the middle of his band plays at every position at both 0.05 and 0.2 m/s, and both limits are within a factor of three of his at four positions of five, mostly within two. At exactly a tenth, the position every note starts from, Helmholtz motion holds at 0.2 m/s to 16 N, three times his maximum, and at 0.05 m/s to 4 N: whether it is hanging on past its limit because the bow never moves there is open. Further from the bridge than he measured, the model's upper limit falls below his extrapolated line from about a quarter of the length, which is further than cellists bow — Schoonderwaldt's players stayed between 2.4 and 12 cm of a 69 cm string, 0.035 to 0.17.

**Guettler's diagram, attacks — not met.** The modelled cello D3 is bowed from rest over 0.1 to 4 N and 0.25 to 4 m/s², at Galluzzo's 0.0899 of the length and three positions further out, and each attack graded by Guettler and Askenfelt's listening limits: perfect if regular from the first period, acceptable if the multiple slipping is over within 90 ms or the creak within 50. Of 256 attacks at each position, perfect and acceptable:

| friction curve | twist | 0.090 | 0.100 | 0.125 | 0.167 |
|---|---|---|---|---|---|
| 0.8 to 0.4 over 0.2 m/s, as the model plays | yes | 0 / 4 | 1 / 6 | 0 / 7 | 0 / 3 |
| the same | no | 3 / 10 | 3 / 10 | 4 / 14 | 1 / 13 |
| 1.2 to 0.35 over 0.02 m/s | yes | 0 / 2 | 0 / 15 | 2 / 21 | 0 / 20 |
| the same | no | 0 / 0 | 3 / 6 | 12 / 17 | 13 / 30 |

The steeper curve, Smith and Woodhouse's measured rosin as the nearest hyperbola, draws a band of acceptable starts with the twist in, in the wedge's orientation but at about half the accelerations Guettler's equations give for this string. It would also halve Schelleng's upper limit, which the gentler curve meets. One friction curve does not serve both diagrams, and that is what the literature finds for this model. Galluzzo's simulations, as Woodhouse's 2014 review reports them, put the friction-curve wedge in the wrong part of the diagram with far fewer successes, and the thermal model's much closer. Galluzzo, Woodhouse and Mansour (2017) found that none of the classical, reconstructed or thermal friction models predicted the measured region. Matusiak and Chatziioannou (2024) got a region roughly like the measured one from an elasto-plastic model, though reconstructing individual attacks needed friction coefficients that change with force and acceleration. From plain starts the model's notes take a fifth of a second to more than a second to settle, the low strings longest.

**Against a measured Guettler diagram.** Matusiak and Chatziioannou measured one with a robot arm and a real bow on a cello G string, 30 by 30 cells of force and acceleration; it is read off their figure cell by cell into `research/bowed-strings/guettler-measured-cello-g.txt`, and the bench models their string from their published figures — weight, tension, damping, twist and bow position. Of the 231 cells they found playable, Helmholtz motion within 20 periods:

| friction | measured playable cells the model plays | measured unplayable cells it plays | overlap |
|---|---|---|---|
| the curve | 1 per cent | 1 per cent | 0.01 |
| bristles, their fitted values | 76 per cent | 21 per cent | 0.48 |

The bristles put the playable region where the measurement has it: heavy bow, gentle acceleration, above the diagonal. Their own simulation found theirs larger than the measured region too.

**But the bristles miss Schelleng's diagram.** On the cello D3 with their fitted values, held Helmholtz motion reaches far past Galluzzo's limits at both ends: at 0.05 m/s down to a hundredth of a newton, ten times below his minimum, and at 0.2 m/s up to the 16 N the bench reaches, three times his maximum and more. Both limits are within a factor of three of his at only one measured position of five, against four for the curve. A search over the bristles' stiffness, coefficients, Stribeck speed, breakaway and hair found no values that do well on both diagrams: the best cut the Schelleng error by about a third and the overlap from 0.48 to 0.42, and the attacks are chaotic enough that a change in the third figure of a parameter moves the score. One model serves held notes, the other attacks, as the two friction curves did. Matusiak and Chatziioannou reach the same place from the other side: reconstructing individual measured attacks needed the friction parameters to change with force and acceleration.

`build/bowed-attacks.wav`: the cleanest start on the D3 grid a tenth of the way from the bridge, a creaky one and a slipping one. `build/bowed-attacks-friction.wav`: three attacks the robot found playable on the cello G, each with the curve and then the bristles; with the curve none starts, with the bristles all three within two periods.

**Decided: the curve plays, and the player starts the notes.** The curve meets the diagram that can be checked exactly, and a player can be built to stay inside it; an attack the string will not start can be avoided, a held note the string plays wrongly cannot. How hard that makes the player's job (`make bowplay ARGS=rehearse`), measured on the cello's four strings and the violin's G, A and E at five bow positions from 0.06 to 0.15 and six forces across the upper half of each position's band, 210 notes:

- one acceleration for every note, scaled from string to string as Guettler's limits scale: at best about a quarter start within the listening limits, the rate Galluzzo's simulations of this friction found;
- a player that listens and corrects the force or the bow's acceleration as the slips come: no better;
- a player that rehearses, trying twenty-four accelerations at seven starting forces from the intended one to four times it on a copy of the string, and playing the smoothest of the attacks that turn regular within five periods — and where none turns regular within three, trying again at four bow positions a little nearer the bridge and further from it, and playing the soonest it finds there, where it found it: all 210 notes start within ten periods and 209 within five, 135 of them with the bow moved, and 190 are still in Helmholtz motion after easing back to the intended force over 0.8 s. The finer search and the smoothness were settled by ear on the violin's open G and D: twelve accelerations at four forces, taking whichever turned regular soonest, found only attacks at four times the force, and they started harshly. The bow positions were settled on the cello's open C: at a tenth of the length the best of 7,800 attacks took five periods, about 76 ms, and at 0.085 one starts at once, which sounded best. An attack is chaotic enough there that its acceleration matters in the fourth figure.

See phase 7.

Worth stopping if: the playable region cannot be made to resemble the published one. Everything after this assumes a player can be given force and speed and get a note.

### 4. Pitch, and the oversampling schedule — built

Schelleng's limits from the cello's A to the top of the violin's E string, at one to thirty-two times 48 kHz (`make bowplay ARGS=oversampling`). Each limit is found as a bowing machine finds it: Helmholtz motion started in the middle of the band at 0.2 m/s, then the force slid down, and separately up, an octave every 0.3 s until three periods running are not one slip each or the period has stretched past three per cent. A grid of separate notes scatters stray cells, because the glide into each cell is chaotic; a ramp gives one number per limit. Eleven bow positions from 0.04 to 0.16 at each pitch, and the result at each rate compared with the median of the three fastest, in octaves of force, the median over the positions:

| | samples a period at 48 kHz | x1 | x2 | x4 | x8 | x16 |
|---|---|---|---|---|---|---|
| cello A3 | 218 | 0.35 | **0.06** | 0.04 | 0.03 | 0.03 |
| violin G3 | 245 | 0.19 | **0.12** | 0.08 | 0.05 | 0.02 |
| violin D4 | 163 | 0.22 | **0.07** | 0.05 | 0.02 | 0.01 |
| violin A4 | 109 | 0.29 | 0.12 | **0.06** | 0.01 | 0.03 |
| violin E5 | 73 | 0.53 | 0.25 | 0.01 | **0.02** | 0.00 |
| violin E6 | 36 | 1.52 | 0.63 | 0.08 | 0.04 | **0.00** |
| violin E7 | 18 | 3.55 | 1.22 | 0.57 | 0.06 | **0.00** |

What decides it is samples a period at the rate the string runs at. At three hundred or more the limits are within an eighth of an octave of the fastest rates', mostly a twentieth; below about two hundred they can move by a quarter of an octave or more, and at seventy-three, the open E unoversampled, by half. Hence the schedule (Sample rate, above), the factor in bold. Eight times is enough even at E7, a tenth of an octave out, so the stopping test is passed: the violin can ship first.

Some limits are bimodal — on the violin's G at 0.06 of the length the motion breaks either at about 0.14 N or at 0.21 N, at any rate, and a ramp lands on one or the other — which is why the comparison is a median over eleven positions. The cello's C is left out: its notes are hard to start at all, which is phase 3's attack problem and not the sample rate's. At thirty-two times a low note's loss filter needs a coefficient close to one, which the engine now allows.

**For the module.** The factor is chosen at each note's start, from its pitch, and kept through the note, so vibrato and slides do not change the rate under a sounding string; the string's output is decimated back to the engine's rate. Both are phase 8's.

### 5. The body — built, the violin from measurements and the cello provisional

`make bowbody`; the sources are in `research/bowed-strings/body-sources.md`.

**The violin.** The five signature modes as measured: A0 at 275 Hz, CBR at 407, A1 at 469, B1− at 475 and B1+ at 541, frequencies the means over seventeen violins and damping the averages over fourteen (Bissinger 2008, 2012), the B1 modes' radiativity his averages. Above 630 Hz the modes are dense, two in a hundred hertz at Q 50, the density and damping that agree with Woodhouse's modal overlap for a violin being played, and their radiativity follows Bissinger's seventeen-violin average band by band to 3,875 Hz, where his measurements stop, falling 8.5 dB an octave above, as the measured bridge mobility does. Against his measurements: A0, B1− and B1+ within a decibel of his values; the 250 Hz bands within 0.8 dB in the median and 2.6 at worst. The upper modes can be scattered in many ways and each is a different violin, a few decibels from the average here and there as any real one is; this one is the closest of four hundred.

**The cello**, provisional. Measured: the air mode near 97 Hz; the strongest body mode at 175 Hz, Q 18, 0.056 m/(s N), the one that makes a real cello's wolf; peaks at 140, 200, 209 and 281 Hz. Not measured anywhere that could be found: a cello's radiativity, the damping of all but the wolf mode, and the density of its upper modes. Those are judgement — the upper modes two and a half times the violin's density for a body about twice its size, shaped as the measured mobility is with a radiation efficiency rising to about 1.5 kHz — to be settled by ear.

**The filter** runs as its resonators' sum says it should, within a millionth of a decibel, and nothing in it rings on.

**To listen to.** `build/bowed-violin-body.wav` and `build/bowed-cello-body.wav`: six notes on each, the open strings and the top string stopped higher, every note started by a player who rehearses it (phase 3). `build/bowed-violin-bridge.wav` and `build/bowed-cello-bridge.wav` are the same notes as the force on the bridge, without the body, to hear what the body does. No vibrato and no player yet: held notes at one force and speed.

This is the first phase whose result is judged by ear as well as by measurement, and the first point at which it is worth you listening properly.

### 6. The instrument — built

`src/BowedInstrument.hpp`, `.cpp`; `make bowinstrument`. Four strings on one bridge, on one body. The violin's and the cello's constants live here, with where every figure came from.

**The bridge moves, and the strings nobody is bowing feel it.** Each string pushes on the bridge; the bridge moves as the body's low modes let it; its motion goes back down every string not being bowed. The push and the motion depend on each other in the same sample, which is solved rather than broken with a delay, as the deprecated piano's soundboard was: each string says what it would push on a still bridge, the body how fast the bridge moves per newton and how fast it would move unpushed, and one line gives the motion. The string's sample is split in two halves for it, and on a bridge that stands still a string in the instrument is exactly a string on its own, which the bench checks to the last bit.

**The bowed string plays against a still bridge.** On a real instrument it feels the bridge too, and the instrument can do that (`setBowedFeelsBridge`). By ear it does not help: on the cello's low notes, with the bridge moving under the bow, the starts sounded rough, and with the bow's string against a still bridge and the other strings still ringing with the bridge, they sounded best. With it on, the model makes no wolf of the cello's F3 on the strongest body mode with this provisional body: started cleanly, the note is as steady as its neighbours at every force tried, from 0.1 N to 1.2 N. (A first measurement seemed to show one; it was a start that had never settled, its attack rehearsed on a string alone and played on a string whose bridge moved.)

**Attacks are confirmed on the instrument.** The player rehearses on a copy of the string alone, which is quick, and tries its best few on a copy of the whole instrument, the other strings as they are, playing the first that starts within three periods there: a string's start is sensitive enough that what starts cleanly alone may not on the instrument.

**One rate for the whole instrument**, the engine's times a factor, since strings that share a bridge sample by sample have to share the sample; the force on the bridge is brought down to the engine's rate for the body's voice. Which factor follows the schedule for the highest note to be played; the module will choose it (phase 8).

**Which string.** A note is played on the string that puts it lowest on the fingerboard, the highest string tuned at or below it. A finger shortens the string, so the bow at the same distance from the bridge is a larger share of what is left, and a finger takes more at each reflection than the nut does: a violin's D measured at Q 281 stopped against 410 open, a finger returning 0.9965 where the nut returns 0.9995.

**Sympathy.** A string nobody plays rings when another plays a note its partials share. On the violin, D4 bowed on the G string sets the open D string moving 34.6 dB more than D sharp does, which shares nothing with it; it rings on after the note; and on a bridge that stands still it does not ring at all.

**Nothing grows.** Every string plucked hard and one bowed, the bridge moving, on both instruments: the bridge's velocity peaks at 0.014 m/s on the violin and 0.021 on the cello, and what is left after the bow lifts dies away.

**To listen to.** `build/bowed-violin-scale-sympathy.wav` and `build/bowed-cello-scale-sympathy.wav`: two octaves up and back, each note on its string and started by the rehearsing player on a copy of the string as it stands; `-still-bridge.wav` the same with the bridge standing still.

Pizzicato comes free from phase 1: `BowedInstrument::pluck`.

### 7. The player

The bowing, in the performer library beside the guitarist. A slur is a bow stroke; within one slur the bow has a fixed length to spend across the notes under it, so bow speed is bow remaining over time remaining. Force follows from the dynamic and the sounding point, bounded by the Schelleng limits phase 3 measured. Direction is inferred, down-bow on a strong beat, with the slur grouping deciding where alternation resumes.

Pressure and timbre on the cable map onto bow force and sounding point, continuously through each note.

**It starts each note by rehearsing it.** Before a note from rest, the player tries a set of attacks — twenty-four bow accelerations at each of seven starting forces from the one the dynamic asks for to four times it, as a cellist bites into a note and eases off — on a copy of the string, and plays the smoothest of those that reach Helmholtz motion within five periods, smoothness being how little the first eighty milliseconds have above two kilohertz against below one; where none does within three, it tries four bow positions either side of the one planned and plays the note where it found the soonest start; once four regular periods are heard it eases to the intended force, over about 0.8 s: chosen by ear on the cello's open C and G, where easing over a tenth of a second swept every start quickly from bright to dull and sounded rough, and 0.8 s sounded the most natural of the times tried. Phase 3 measured what this buys (above). It costs computing in bursts at note starts: 168 attacks, 30 to 50 milliseconds of one core a note, and where the bow has to move, 2,080 more, about 0.9 s for the cello's open C. But a start from rest depends only on the string, the bow's position, the force and the speed, so what a rehearsal finds can be kept: the first open C at a dynamic is rehearsed and every later one plays at once, and the common ones can be rehearsed before the music starts. Whether it runs on another thread, delays the note, or rehearses ahead from the score's knowing the note is coming, and how few candidates suffice when the best ones are tried first, is this phase's to settle.

`pizz` and `arco` arrive as words rather than as elements, which is the reading problem already solved for the worded crescendos.

This is the phase where the thing stops sounding like a test tone and starts sounding like a part being played, and it is the one most likely to need several passes by ear.

### 8. The module

mpxViolin: panel, layout file, controls, help text from the `//?` lines, MPX in, one mono output, level lamp. The guitar's pattern throughout.

Which controls reach the panel is decided here and not before, because until phase 7 it is not known which of them a player actually moves.

**The player has to know what is coming, and the cable did not say.** A note arrived on the MPX cable at the sample it was to sound, with its length, its level and whether it runs into the next, and nothing else; the plugin's other instruments are built on that (see `guitar-player.md`, "Nothing is known about the future"). The bowed player cannot be. Its bow slows into a change before the note the change belongs to, its finger lands a few milliseconds early, and it shares out its bow by how long the slur has to last. And it rehearses: on the Fauré's first thirty seconds it simulated 129 s of violin and 142 s of cello to play 31 s, about a third of a second of computing a note, far too much to do in the sample a note arrives in.

**So the cable announces notes.** A source that knows its future says so: each note goes on the cable `MPX_ANNOUNCE_AHEAD`, three seconds, before it sounds, as an announcement on a ring of its own beside the bus's events, carrying the note exactly as it will arrive — its handle, pitch, level, length and technique — with the engine frame it will start on, how long the slur it begins or continues has to run, and whether it falls on the first beat of a bar. The note itself still arrives at its moment as it always has, so every module that reads the cable today reads exactly what it read before, and none of them sees an announcement unless it asks for one. When what was announced will not happen — the transport stopped, the music sent to another bar, the tempo moved — the source withdraws everything it announced and has not yet played, and announces again from where the music now is. Announcements go only from source to receiver: a module that changes notes on their way through does not pass them on, since what it announced would not be what it played.

mpxScore announces, from the timeline: a note's start is its swung position taken through the song's tempo map and the tempo control, so with the file's tempo the frame is exact. The part's lowest and highest notes go on the cable with the instrument, so a receiver can choose its oversampling for the whole part when the song is loaded rather than discovering a high note in the middle of a phrase.

**The violin plans on threads of its own** (`src/BowedEngine.hpp`). Two copies of the player: one on the audio thread, playing; one on a planning thread, running ahead of it through the announced notes and rehearsing each as it comes, on a copy of the instrument that is exactly what the playing one will be when the note starts. Each decision — a note begun, or the bow lifted at a note's end — is numbered, and the planner keeps it as a plan: the actions it scheduled on each string with the moments they happen, and the player's bookkeeping after it. The playing copy, reaching the same decision at the same sample, carries out the plan instead of deciding. The two simulate the same arithmetic from the same state, so a part played this way is the same, sample for sample, as the player playing it alone.

The planner shares each note's trials out over a crew of helper threads (`src/Crew.hpp`), about half the machine's cores, at a lower priority than the audio: the shortlist's starts from rest, the dozen bites, the six glides, a bow change's ways of turning. Each trial runs on its own copy and the comparing stays in order on the planner, so what is chosen is what one thread would choose. It runs at most `MPX_ANNOUNCE_AHEAD` less a seventh of a second ahead of the playing copy, so every note it decides about has been announced to it.

A note nobody announced — from mpxIn, a generator, or a planner that fell behind — is played at once by the playing copy without rehearsing, started as rehearsal found best for the nearest note like it on that string, a few semitones or a dynamic either way, or with a 20 ms glide where nothing near has been found; and the planner starts again from the playing copy as it then stands. So does a note from a keyboard let go, and everything announced withdrawn.

`make bowplayer ARGS="engine <file> <part> <seconds> violin|cello"` plays a part alone and through the engine, each note announced as mpxScore announces it. On the Fauré's first thirty seconds:

| | alone | the engine waiting for its planner | in real time |
|---|---|---|---|
| violin | 43.6 s of computing | 14.8 s, the same sample for sample | the audio thread busy 23 % of the time; the planner started again once, at the second note |
| cello | 30.4 s | 9.7 s, the same sample for sample | busy 15 %; never started again: the same sample for sample |

The first notes after the music starts cannot have the full three seconds, and the violin's second note is one whose start takes the longest search there is — no start from rest regular within three periods, and four more bow positions tried — about two seconds even shared out; with half a second's warning, and with a second and a half, the planner fell behind there and twice more.

**What it costs.** The playing copy, on the audio thread, with Rack's compiler settings on an Apple M-series core: the violin 0.086 of a core playing and 0.006 silent, the cello 0.050 and 0.004. Five things brought it there from 0.12 and 0.11, none of them changing a sample of what is heard — the Fauré renders came out identical to the bit after each:

- **No heat stepped where no bow is.** With nothing coming in and nothing carried away, a sample of the contact's cooling is one fixed matrix, so on a string the bow is not on the steps are counted rather than taken, and applied all at once, as the product of the matrix's powers of two that make up the count, when the bow comes back (`BowedString::catchUpHeat`). Three strings of four are unbowed at any moment, and stepping their heat was a third of the cost.
- **At rest when silent.** With no bow on any string and the sound under a millionth of a pascal for a second, the instrument is not stepped, until the engine sample before the next thing the player does; the rest's cooling is owed as above. Copies rest and wake exactly as the instrument does, so a rehearsal is still what is played.
- **The heard body two modes at a time.** The body's voice, which nothing reads back, runs its hundreds of resonators side by side in pairs and sums only the sound. The bridge as the strings feel it, which is in their loop, is untouched.
- **The downsampling filter read straight through.** Each sample is written twice, a filter length apart, so the window is one unbroken run rather than a circle wrapped at every tap.
- **Nothing worked out twice.** A bow unchanged since the last sample is not given to the string again, and the bridge's response to a newton, fixed by its coefficients, is summed once.

What remains is mostly the heat of the one string being bowed, a quarter, and the four strings' waveguides, a fifth. The waveguides of all four strings could be run side by side; it would change the arithmetic inside the strings' loop, so its renders could not be shown identical, for perhaps a tenth more, and is not done.

**Economy**, in each module's right-click menu, for less computing at less fidelity: the strings made ready for the part's highest note an octave down, so they run half as many times faster than the engine, and a string nobody is playing stopped once its push on the bridge has stayed under a thousandth of a newton for a tenth of a second — not stepped, so neither ringing on nor ringing in sympathy, until it is bowed, stopped or plucked again, its rosin's cooling owed for the time as when the whole instrument rests (`BowedInstrument::setQuietStringsRest`). On the third movement in real time the violin's playing came to 4.9 per cent against 9.5, the viola's 6.9 against 11.5 and the cello's 6.0 against 7.3; rehearsing a part alone took about half the computing, 11.8 s against 34.4 for the violin's thirty seconds of the first movement and 11.4 against 25.3 for the cello's; and with the three playing together in real time their planners fell behind 2, 0 and 1 times where they had 20, 19 and 12, so far fewer notes are started without rehearsing. Notes not at one slip a period, of 78: the violin 3 against 2, the cello 3 against 4. The planner's copies rest strings exactly as the playing one does, so the engine still plays as the player alone.

**In Rack the meter reads about twice that.** `make stringbench` runs the real modules — mpxScore into mpxViolin, mpxViola and mpxCello, and mpxPiano if asked — made by their Models and stepped as Rack's engine steps them, each one's `process()` timed as its meter times it, in blocks of 256 samples paced to the clock. On the third movement of the Fauré quartet, on an Apple M4's performance cores, the violin, viola and cello came to 9, 11 and 7 per cent, the same with the piano playing beside them; the planners' crew, rehearsing on other cores, adds about a fifth (7.5, 8.9 and 5.3 without it), and their bursts as the strings come in put single samples at up to 17 ms. Confined to the efficiency cores the same took 32, 43 and 30. Rack showed 18 to 20: the playing thread's own work is what is measured here, and the rest is where the machine runs it and what competes with it.

**Fidelity, four levels**, in each module's right-click menu (`BowedPlayer::Rehearsing`, `BowedEngine::Setup::rehearsing`), saved with the patch as "fidelity", 1 to 4. Level 1: each start from the string rehearsed as above, the shortlist found on the string alone and up to twelve of it and six glides tried on the instrument. Level 2: a glide over 20 ms from a fifth of the force and one bite, tried on the instrument and the better played (`Habits::twoStarts`) — for the violin and the viola a bite of 1.19 times the note's force at 1.4 of the planned distance from the bridge, its acceleration e^(−19/12) of what Guettler's limits allow; for the cello 1.59 times the force at the planned place, e^(−41/12). Level 3: nothing tried, a bite of 1.59 times the force at e^(−41/12) played (`Habits::guessBite`). At these three every bow change is rehearsed. Level 4, live, the default: nothing rehearsed at all — the guessed bite, and each bow change with no extra press and a turn of 10 ms on the violin and viola and 20 ms on the cello, the turns their rehearsal chose most often (59, 42 and 43 per cent of changes; `Habits::guessTurn`) — so the engine starts no planner, the instrument is worked out once, and nothing need be announced ahead. By ear, even level 4 is good in a string quartet and a piano quartet, and good enough for everyday synthesis; a performance can take a higher level. The shortlist on the string alone is two thirds of all the computing (168 attacks, and 2,080 more at four bow positions when the best of the first is not regular within three periods), the trials on the instrument most of the rest. By ear level 2 was very nearly as good as level 1 on the Saint-Saëns Swan and on the Fauré's violin part, and the better of the two on the first movement of Brahms's viola sonata; the computing came to about a half of full rehearsal's on the Swan and a fifth to a quarter on the Fauré and the Brahms, and level 3 to about a fifth. The two starts are the glide and the bite full rehearsal chose most often over 15,362 logged decisions (`BowedPlayer::setLog`). Measured against full rehearsal note by note, with a second full rehearsal at another seed as the measure of chance (0 to 2 notes in a passage rougher by 4 dB or more): on the Swan within chance; on the viola sonata 6 rougher and 9 smoother.

**What was found not to work.** A table of starts by situation — the string, the note in steps of three semitones, the marking and how strongly the string still rang — played without rehearsing: right for bow changes, its one change matching rehearsal's best 86 per cent of the time, but not for starts from the string, where the best start on each occasion depends on the state the string is in and not on the situation (the table's best six for a situation held rehearsal's best only 40 per cent of the time); in music it played about as many bad starts as one fixed glide. One fixed bite for every start: a firm one, 2.5 to 3 times the force, started every note on time and by ear a quarter of them harshly; a soft one started smoothly and too often failed to settle; no setting between was both. Cheaper shortlists — fewer forces and accelerations, or attacks given up at 50 ms without a slip — chose rougher starts than chance allows on every passage tried. Setting the bow on the string still before moving it, pressed or touching, changed nothing: a still bow only grips a string barely ringing. The rosin model of van Walstijn and colleagues (2026), with the hair's give, made a guessed start no nearer its rehearsed one. Players, by the literature (research/bowed-strings/attack-control-sources.md), cannot correct an attack — its fate is settled within about four periods, faster than any human reaction — and a bowing machine with no feedback scatters as this model does.

**A slurred change of three semitones or more** slides over 15 ms on a raised cosine, and the old note lingers: an unbowed copy of the string as it was rings on, its push on the bridge heard fading out over 40 ms while the string's own is heard fading in (`BowedPlayer::SlurSmoothing`, `BowedInstrument::linger`). Only what is heard is faded; the string's motion and its push on the bridge, which the other strings feel, are untouched. Changed at once, every harmonic of the note moves at once, and dry that is heard as a blip of pink noise: an ideal tone with a bowed string's spectrum switched the same way, phase unbroken, has it too, and slid over 40 ms has not but is heard to slide. The blip was the same a semitone higher and a semitone lower, with the bow's force, speed and place held or changed over 300 ms, and with the string's length changed so that the wave on it was kept rather than read again; the string's slips were regular from 5 ms on.

**The oversampling** is chosen when the part's range is known, from its highest note on the schedule above; without one, for notes up to 1,280 Hz, eight times. A new range or a new sample rate builds a new engine on the main thread, taken up by the audio thread at the next sample.

**The module** (`src/BowedModule.hpp`, the violin named in `src/mpxViolin.cpp`; see phase 9), 4 HP, one column from the top: the MPX input; four ports for playing without a cable, 15.5 mm apart; VIBRATO — the width of each note's vibrato as a share of what the player chose, applied on the playing copy so that it is heard at once; and the level lamp, unnamed, just above the one mono output, which is placed in the stereo field at the mixer. The vibrato port is named Vibrato CV, so that Clarity's word rules colour it as a control voltage. The ports (`BowedModule::playFromPorts`) play one instrument a channel: channel c of GATE, V/OCT, LEVEL and VIB belong to player c and to no other, as many players as the gate has channels, and a one-channel cable into V/OCT, LEVEL or VIB is read by every player, as Rack's polyphonic cables are. Each channel is played as a monophonic synthesiser is played: GATE, a note while the gate is up; V/OCT, a move of more than half a semitone while the gate stays up the next note slurred in the same bow, a smaller one left alone; LEVEL, read as each note begins, nought to ten volts from ppp to fff, mf unpatched; VIB, added to the knob, five volts as much as its middle. A gate's note is held until the gate falls (`Note::held`), drawn as for a note of a second, and slurred by the new note's own word (`Note::fromSlur`); nothing being known ahead, it is played as the live level plays it, whatever Fidelity says. While an MPX cable is in, the four ports are not read. `make stringbench ARGS="ports violin"` plays a module from its ports alone, chords and a four-channel unison among it.

**A section in one module** (`BowedSection`): sixteen players, each an engine of its own (instrument, player, and at levels 1 to 3 its planner), set up alike but for their tuning — the first true, the others spread up to four cents sharp or flat by the golden angle (`BowedSection::centsOf`) — and their vibrato's pace (`Habits::seed`). From the ports, channel c is player c. From the cable, which names notes by handle and has no channels, notes starting on the same frame are a chord and are shared out highest first (`BowedModule::shareOut`): a note goes to a player whose notes have ended by its start, or end within 50 ms after, as a keyboard's legato overlaps; of those, to one whose last note ended within 30 ms before its start, the nearest in pitch, so a line stays on one player; else to the first player resting; with every player busy, the one whose note ends soonest gives it up. A player given a note announced for later counts as busy from then, so a note nobody announced goes to another player rather than cutting the announced one short. The players are summed into the one output: notes add, as the cable's levels say they do. A player with nothing to play is asleep from the moment it is made (`BowedPlayer::setInstrument`) and is passed over an engine sample at a time (`BowedPlayer::process`), so the fifteen that are resting while one plays a line cost about half a per cent of a core between them; a line on its own plays sample for sample as the one-instrument module did. Each player sounding costs what a module of its own does, on the one thread Rack gives a module: the Fauré's piano part, up to a dozen notes at once, took 126 per cent of a core on the violin, more than one thread can keep up with. Each instrument's output is set to be as loud as mpxPiano playing the same markings, measured as a loudness meter does — K-weighted, bar by bar over the Fauré's first minute — which puts a single note at fff at 2.5 to 2.8 V: a held note is louder than a struck one at the same peak, so at the peaks the piano and the strings were matched to, the strings were 9 to 10 dB too loud. Which other controls belong on the panel is still to be settled by playing it.

### 9. The other three — the viola and the cello built, the double bass to come

Each instrument a slug of its own, mpxViolin, mpxViola and mpxCello, because somebody looking for a cello searches for a cello. Everything that plays is one module shared by them, `src/BowedModule.hpp` and `.cpp`; each slug's file is short — which instrument, its player's habits, how many volts a pascal, the highest note to be ready for when the part does not say — and configures its own controls with their help beside them, since the help is read from beside each configure call. DreamerHelp's `helpscan` reads a module that derives from another module given to it, the shared header first, its control lists its parent's.

**The viola** (`BowedInstrument::viola`, `BowedPlayer::violaHabits`), from `research/bowed-strings/viola-sources.md`. Its strings are measured: the Thomastik Dominant set's tensions on its 370 mm length, impedances 0.50, 0.32, 0.23 and 0.18 kg/s. Its body is one measurement and the violin's scaled, the split marked in the source: the signature modes' frequencies the medians of the violas measured — the air mode at 228 Hz, the B1 modes at 370 and 448 Hz, three to four semitones under the violin's — their damping the violin's, their mobility and radiativity the violin's lowered 6 to 8 dB as the one viola measured beside a violin on the same rig lies under it; above 500 Hz the violin's curves at their own frequencies, the bridge hill where the violin's is, lowered about 2 dB to 2.8 kHz and 5 to 6 above. The air mode's level is to be settled by ear. Its player is the violinist with a viola bow's 0.63 m of hair and a vibrato between the violin's and the cello's, 5.7 Hz and 18 cents. On the Fauré's viola part: one note of 78 not at one slip a period, every bench passing, and through the engine in real time the same sample for sample as alone. A single note at fff peaks at 0.66 Pa, and through the module at 2.8 V.

**The cello** is the instrument of phases 5 to 7, its body still provisional.

The double bass needs its own parameter set rather than the same one scaled: tuned in fourths where the others are in fifths, a far higher impedance, and a different bow.

Not before this point, because slugs are permanent and the engine's shape must be settled first.

### 10. A quartet

The Fauré piano quartet, first movement, already measured for its markings, played with violin, viola and cello parts against the piano. Four modules in one patch is also where the oversampling schedule gets its real test, since the cost that matters is the cost of all of them at once.

## Still to come

A finite-width bow. Pizzicato, which is a different excitation of the same string and nearly free. Mutes, harmonics, and the sounding-point extremes of sul tasto and sul ponticello, all of which are parameter moves rather than new mechanisms.

**A cheaper playing thread**, to explore. Profiled at level 3 on the violin (Apple M4): the bowed string's friction with its heat is 28 per cent of the playing thread, the heat model's twelve-layer solves alone 24; the four strings' waveguides 16, of which re-working the tuning for vibrato and bow moves 6; the body 3; the player's bookkeeping 5. In order of gain for effort:

- The heat stepped every second or fourth string sample rather than every one: its own changes are far slower than 768 kHz. Perhaps a sixth to a fifth of the playing thread; it changes the sound, so it is to be judged by ear.
- The tuning re-worked only when the pitch or the bow's place has moved enough to matter, or its parts that do not depend on the contact kept: up to about 5 per cent, with no change to the sound if done carefully.
- Each string at its own rate: only the strings that reach high notes need sixteen times 48 kHz, the G and D strings eight or four. Perhaps 30 to 40 per cent of the strings' work; the bridge then joins strings at different rates.
- Fewer samples a period than 300 at the highest note, which would halve again; the high notes to be judged by ear.
- The four strings' waveguides side by side in vector arithmetic: about a tenth; the arithmetic changes, so renders could not be shown identical.

A plugin's meter measures time, not work: flat out, levels 3 and 4 cost the same on the playing thread, but paced to the clock with no planner busy, the system ran level 4's thread on a slower core or clock and it read twice as high. Any of the above is to be judged by computing done, not by the meter alone.
