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

**Done.** `make pianobench` measures all of it. Two faults in the bench itself were found and fixed by its own numbers: the stretch was fitted against the pitch the string was *asked* for rather than the one it produced, which folded a fiftieth of a per cent of tuning error into every partial and grew it with n until more stiffness read as less; and the partial search used one window, which for a stiff string is wider than the gap between the low partials of a slack one, so it locked onto neighbours. It finds the low partials first, fits a rough stretch to those, and uses it to say where the high ones are.

#### What the bench says about the starting point

Measured on `GuitarString`, which is what the piano's strings will be built from:

| | stiffness 0 | 0.25 | 0.6 | 1.0 |
|---|---|---|---|---|
| A1 | 0.000004 | 0.000030 | 0.000062 | 0.000099 |
| A3 | 0.000004 | 0.000030 | 0.000065 | 0.000105 |
| A5 | 0.000033 | 0.000057 | 0.000093 | 0.000139 |

**Three things follow, and they are phase one's work.**

**The range is too small and in the wrong place.** The control reaches about 0.0001 at full travel, which is a midrange piano string. The bass needs several times that.

**It does not vary with pitch, and it must.** A1 and A3 give nearly the same stretch at the same setting. On a piano the coefficient is least near the middle and rises toward both ends; a single number cannot express that, so the string will have to be given its stretch per note rather than asked for an amount.

**There is already stretch with the control at nought** — 0.000033 at A5, from the fractional-delay interpolation's own dispersion. It is small, it grows with pitch, and it is in the same direction as the real thing, so it has to be subtracted rather than ignored or the treble will come out doubly stretched.

Beating measures true: 0.061 Hz against 0.064 expected at half a cent, 0.254 against 0.254 at two cents. Decay is clean and the damping control moves the upper partials as it should — six seconds at the fundamental against 0.71 at the eighth partial. And the centroid does not move with force at all, 571 Hz at every level, which is exactly the number phase two has to change.

### Phase 1 — one string, measured

**Done.** `src/PianoString.hpp` and `.cpp`, measured by `make pianobench`. It is not `GuitarString` with different numbers; it is the same kind of thing — a delay line closed through a loss filter with an allpass chain for stiffness — asked different questions.

**A piano string is not given its stiffness, it is given its note.** That was phase 0's first finding and it decides the whole interface. The string is told which MIDI note it is and takes its own stretch and its own decay from the compass; a detuning in cents is separate, for the unisons of phase 3, and does not change what the note takes. The stretch is asked for as B itself — the coefficient in the stiff-string relation — rather than as an amount of stiffness, because B is what the literature publishes, what the bench measures, and what a tuner's stretched octaves follow.

Both curves are kept as anchors with straight lines in the logarithm between them, not as formulae, because neither quantity has a tidy one: both are set by how a particular instrument is strung, so the published measurements are a curve through points. They are representative of a medium grand rather than exact — real instruments differ by a factor of two — and phase 9 puts them on the panel. The shape is not in doubt.

| | B | fundamental's T60, seconds |
|---|---|---|
| A0 | 2.0e-4 | 30 |
| A1 | 1.1e-4 | 25 |
| A2 | 6.0e-5 | 18 |
| E3 | 5.0e-5 | — |
| A3 | 6.5e-5 | 11 |
| A4 | 1.3e-4 | 6 |
| A5 | 3.2e-4 | 3 |
| A6 | 8.0e-4 | 1.2 |
| C8 | 1.6e-3 | 0.4 |

B is least below the middle of the keyboard, around the bottom of the treble bridge, and rises far more steeply toward the top than toward the bottom: the top strings are a couple of inches of thick wire, which is nearly a bar, and a bar's partials are stretched enormously.

#### The allpass chain is fitted, not guessed

The guitar's string places its eighth partial a fixed number of cents sharp and lets the rest fall where they may. That is enough for steel and nowhere near enough here — being right at the eighth and wrong at the sixteenth is audible as a note that will not quite tune, because the ear tunes a piano by its upper partials.

So the chain is solved for. For each candidate number of stages and coefficient, the model works out where the loop will **actually** ring — every partial up to the twenty-fourth, including what the interpolated read and the loss filter do to them — and keeps the chain whose **worst** partial is least wrong in cents. Worst and not average, for the reason above.

**The read's own stretch is subtracted rather than ignored**, and this falls out of the method rather than being arranged. Phase 0 measured three ten-thousandths of stretch at A5 with the stiffness control at nought, from the interpolated read's own dispersion; a model that added its own stretch on top would come out doubly stretched in the treble. Because the fit asks where the partials land rather than where the dispersion relation says they should, at B of nought it chooses a chain that flattens the partials by exactly as much as the read sharpens them.

**The error falls as one over the number of stages**, from twenty-five cents with one stage at the bottom A to under one with eight and forty. **Two groups of stages at different coefficients were tried and do not help**: the best pair is always the equal pair, so the extra freedom is a valley with nothing at the bottom of it. Quantity is the answer.

**And quantity is cheap where it is needed**, which is what makes that bearable. The stages wanted go as the stretch does — most in the bass, almost none at the top — and the bass is where a note has one string rather than three. The bottom dozen notes at eight and forty stages each are a small fraction of the whole instrument's filtering; the treble, which is three strings a note and most of the notes, wants a handful.

#### What the string does

| note | asked B | measured B | stages | worst partial, cents | pitch, cents |
|---|---|---|---|---|---|
| A0 | 2.00e-4 | 2.52e-4 | 48 | 2.00 | -0.48 |
| A1 | 1.10e-4 | 1.39e-4 | 48 | 1.17 | -0.28 |
| A2 | 6.00e-5 | 8.65e-5 | 48 | 0.89 | +0.14 |
| A3 | 6.50e-5 | 9.67e-5 | 48 | 1.40 | +0.14 |
| A4 | 1.30e-4 | 1.32e-4 | 48 | 0.80 | +0.14 |
| A5 | 3.20e-4 | 3.38e-4 | 24 | 0.62 | +0.14 |
| A6 | 8.00e-4 | 7.87e-4 | 12 | 0.29 | +0.14 |
| C8 | 1.60e-3 | 1.65e-3 | 1 | 0.00 | -0.22 |

Measured B is fitted to the first twenty-four partials of the sound itself. It reads high in the bass — a quarter to a half — while the worst partial is under two cents, which is the same fact twice: B is fitted with the high partials dominating, so a cent at the twenty-fourth shows as tens of per cent of B. Tuning is within a fifth of a cent everywhere above A2 and within half a cent at the bottom.

#### The decay, and the thing the bench caught

**Every partial goes round the loop at the same rate.** The delay line is one period of the fundamental long, so everything in it — fundamental, second partial, twentieth — comes round once per that period, not once per cycle of itself. The first version of the decay arithmetic assumed otherwise, and with it a flat loop gain would have given a one-over-n decay for nothing. It does not: it gives every partial the **same** decay. The bench said so plainly, reporting a power of 0.04 where the model claimed 1.18, and that disagreement is the whole reason phase 0 came first. The entire power law is the loss filter's work and none of it is free.

The decay is therefore asked for as two numbers: the fundamental's time to fall sixty decibels, from the compass, and a power — the partial at f falls in the fundamental's time multiplied by f0 over f raised to it. Real pianos sit near one, which is to say the eighth partial dies in about an eighth of the fundamental's time.

**The loss filter is three first-order shelves**, and three other forms were measured and rejected first. `test/lossprobe.cpp` asked of each the only question that matters: with the best coefficients it can have, how far out is its worst partial's decay time?

| form | bottom A | middle | top |
|---|---|---|---|
| one pole | 19% | 23% | 10% |
| two bare poles | 13% | 12% | 10% |
| symmetric FIR, 7 to 63 taps | unusable | unusable | unusable |
| three pole-zero shelves | 5% | 10–14% | 10% |

A pole's loss grows as the **square** of the frequency below its corner and stops growing above it; those are the only two shapes it has, and the law wanted is between them. Cascading a second bare pole changes nothing, because two curves that are each squared-then-flat still make squared-then-flat. The symmetric FIR was tried because its taps enter its magnitude linearly, so the best of them is a linear solve rather than a search — and it is unusable, badly: fitted to the partials it is free to do as it likes between and above them, and it peaks at gains of twenty thousand and upward outside the fitted band. A loop that gains anywhere finds that frequency and screams on it. Seven taps also cannot shape anything across the bottom hundredth of the band, where a bass note's partials all live, so the bass came out silent.

Three shelves work because each contributes its own squared-law rise over its own octave or two and then levels off, and three staggered follow a gentler power law across the whole run. It is the ordinary way a fractional-order response is approximated, and it costs three multiplies a sample against a dispersion chain of up to forty-eight stages.

**And the form carries its own stability argument**, which the FIR could not. A shelf with its zero further from one than its pole is exactly transparent at DC and falls monotonically from there; three in series do the same; so the loop's gain is highest at DC and is simply the flat gain. Keep that below one and nothing at any frequency can grow — no sweeping of the band to check.

**Two constraints on the fit that are not refinements but corrections**, both found by the bench reporting nonsense:

**A shelf is allowed a quarter of the period of delay and no more.** The loss sits inside the tuned loop, so its delay comes out of the delay line's. Fitted on magnitude alone the search put corners below the fundamental, where a first-order section delays by hundreds of samples, and A3 came out a hundred and thirty cents flat with its partials strewn over an octave and a half.

**And it must be all but transparent at the fundamental.** The loop's gain is highest at DC and set there by the flat gain, which cannot exceed one, so the whole of the fundamental's loss has to fit inside its own budget. The fit, free to add its anchor as a signed constant, chose shapes that ate more than that and read as excellent — thirteen per cent at the bottom A, every partial in place — while what was actually built had its gain clamped at one and every partial ringing twenty times too long. The anchor is now part of what is minimised rather than applied afterwards, so the number reported is the error the loop will have.

| note | asked | partial 1 | partial 2 | partial 4 | partial 8 | power measured | worst partial |
|---|---|---|---|---|---|---|---|
| A0 | 30.0 | 29.99 | 17.75 | 6.98 | 2.33 | 1.24 | 24% |
| A1 | 25.0 | 25.00 | 14.99 | 5.95 | 1.99 | 1.23 | 25% |
| A2 | 18.0 | 18.00 | 8.92 | 3.83 | 1.63 | 1.16 | 9% |
| A3 | 11.0 | 11.00 | 4.59 | 1.65 | 0.75 | 1.31 | 40% |
| A4 | 6.0 | 6.00 | 4.32 | 2.05 | 0.68 | 1.05 | 43% |
| A5 | 3.0 | 3.00 | 1.63 | 0.78 | 0.26 | 1.17 | 24% |
| A6 | 1.2 | 1.20 | 0.73 | 0.23 | — | 1.17 | 28% |

Seconds to fall sixty decibels, against a power of 1.15 asked for. The fundamental is exact by construction. The measured power comes out between 1.05 and 1.31, and an individual partial's decay time is up to about forty per cent from the law — which is within the spread between real instruments, and well inside what phases 3 and 6 will change, since a coupled choir's double decay and the soundboard's own losses both arrive in the same place. The top octave is the exception and is left as it is: C8 has two partials below ten kilohertz, no dampers on a real piano, and no sustain worth the name.

#### The ring of dependencies, and where to cut it

The delay line's length depends on what the chain and the loss delay at the fundamental; the chain's fit depends on what the loss delays at every partial; the loss's fit depends on what the interpolated read takes off the top, which depends on the delay line's length. The first version cut that ring in the wrong place — it fitted the chain and **then** fitted the loss again, so the chain was tuned against a filter that no longer existed. While the loss was a bare pole, whose delay is a fraction of a sample, that was a slip with no consequences; with three shelves in it, it was the hundred-and-thirty-cent error above. The order is loss, then chain, then round again, then the flat gain alone. The second round settles it because the only thing feeding backwards is the read's own gain, which is within a thousandth of one across the whole band, and because the loss fit is given its previous answer as a starting point so it returns to the same valley rather than to another one that is as good on magnitude and quite different on delay.

None of this runs on the audio thread. A detuning of a cent for a unison refits nothing — the design for a note a cent away is the same design — which is what makes the choir of phase 3 affordable.

#### Fitting above ten kilohertz is fitting what nobody hears

Both fits stop at ten kilohertz as well as at the twenty-fourth partial. At A5 the twenty-fourth is above twenty kilohertz, where the interpolated read has taken most of it away already; asking the loss filter to get its decay right as well threw the fit for every partial below it out by a factor of two hundred. A piano has nothing worth modelling up there.

#### What is not in it yet

The hammer is phase 2, and the string is struck with a visible placeholder: a plain triangle, the string's own shape when a finger lets it go. What the placeholder does have right is the corner's place — a triangle cornered at a seventh of the length has no seventh partial, and the bench measures the seventh at 79 dB down and the fourteenth at 120 dB down against the fundamental, which is true of a real piano and one reason the strike point is where it is. Phase 2 has something to not break.

### Phase 2 — the hammer

**Done.** The hammer is in `PianoString` with the string, because the two cannot be separated: they are in contact, pushing against each other, for a millisecond or two at the start of every note.

#### The string had to be split, and that is the structural price of having a hammer at all

A hammer is not a shape put into a string. It is a mass in contact with it, pushing while the string pushes back, so there has to be a **place** on the string where the pushing happens. Phase 1's single loop therefore becomes two sections — strike point to the agraffe, strike point to the bridge — meeting at a junction where the force goes in.

**The round trip is the same length through the same filters, so every number phase 1 fitted still holds.** Two sections of the same string have the same impedance, which makes the junction transparent when nothing is pushing on it: the wave from one side goes straight out the other. The left section is a whole number of samples, exact and with no dispersion of its own, so all the interpolating stays in the right one and the delay solve only had to learn to subtract the left section's length.

Each section is one delay line carrying its whole round trip — out to the end and back — rather than a pair of one-way lines. The two are equivalent and this is half the memory and half the arithmetic.

**A rigid end turns the sign over, and there are two of them**, so a full round trip comes back the way it started, as phase 1's loop did. Where the note is too short to divide there is no second inversion to pair with, so that one is left out. Getting this wrong inverted once a round trip, which puts the resonances halfway between where they belong: A6 came out twenty cents flat and the top C seventy-two.

**At the top of the compass it cannot be divided at all.** The top C's period is eleven samples and a tenth of that is one. Those notes keep phase 1's single loop with the force fed straight into it, which costs nothing real — the first partial with a node under the hammer would be the tenth, at forty kilohertz. The junction's own arithmetic has to be skipped there too, not merely given a zero-length section: what goes back into the line is the junction's motion less what arrived from each side, and with nothing arriving from the other side that is the force and nothing else. The circulating wave was being thrown away and the top octave came out as a whine at forty hertz.

#### The missing partials now happen rather than being arranged

A mode with a node at the strike point cannot be moved by a force applied there. Phase 1's placeholder faked this with the corner of a triangle; now it is simply what the junction cannot excite. A3 struck an eighth of the way along gives the eighth partial 71 dB below the fundamental and the sixteenth 106 dB below, with their neighbours at 60.

#### Felt is a nonlinear spring, and that is the whole point

The force it returns goes as its compression raised to a power of between two and three, rising toward the treble where the felt is harder and thinner. A harder blow compresses it further, which makes it **stiffer**, which **shortens** the contact, which puts a briefer and therefore brighter pulse into the string. It is why a piano brightens when played harder rather than only getting louder, and no envelope or filter reproduces it.

#### Solved implicitly, every sample

The design note named this as the part most likely to take several attempts. The reason is a circle inside a single sample: the force depends on how far the felt is compressed, which depends on where the string has moved to, which depends on the force. Taken explicitly — compressing against where the string was last sample — it is inaccurate at a whisper and unstable at full force, because at full force the string moves a long way in one sample.

Written out, the sample's updates reduce to one scalar equation. The hammer loses speed by the force over its mass and moves by its new speed; the string's junction moves by the waves arriving plus its share of the force over twice the impedance; so the compression left over is `u = u0 - cF`, where `u0` is how far the two would overlap with no force between them and `c` collects the hammer's inertia and the string's yielding. The felt says `F = K u^p`. The left side rises with F and the right side falls, both continuously, so they cross exactly once, and Newton's method reaches it in a handful of steps from any start with a derivative that is always positive. It did not take several attempts.

#### A single string has to know how many strings the note has

The hammer pushes all of a note's strings at once, so they resist it together: three strings are three times as hard to shift, the hammer's momentum drains three times as fast, and the contact is correspondingly shorter. Modelled against one string where a real hammer pushes three, the contact came out twice as long as it should. So the hammer feels the whole choir's impedance and the one string modelled takes its own share of the force — which also makes it a correct member of the choir phase 3 will build around it, rather than an approximation to be undone.

#### The impedance curve was wrong by a factor of fourteen, and the hammer caught it

The string's wave impedance is its mass per unit length times the speed waves travel along it, and it decides how hard the string pushes back. Guessed at, it came out falling from four kilograms per second at the bottom to a quarter at the top, on the reasoning that treble strings are short and thin. Worked out from the stringing instead — the speed is twice the speaking length times the pitch, the mass per unit length is the wire's cross-section times the density of steel — it is very nearly flat:

| note | length m | diameter mm | speed m/s | kg/m | Z |
|---|---|---|---|---|---|
| A0 | 1.20 | wound | 66 | 0.055 | 3.6 |
| A1 | 1.05 | wound | 116 | 0.020 | 2.3 |
| A2 | 0.95 | 1.20 | 209 | 0.0089 | 1.9 |
| A3 | 0.72 | 1.05 | 317 | 0.0068 | 2.2 |
| A4 | 0.40 | 0.95 | 352 | 0.0056 | 2.0 |
| A5 | 0.22 | 0.90 | 387 | 0.0050 | 1.9 |
| A6 | 0.115 | 0.85 | 405 | 0.0045 | 1.8 |
| C8 | 0.05 | 0.80 | 419 | 0.0039 | 1.6 |

Treble strings are only a little thinner, and what they lose in mass they make up in speed. A maker aims for an even impedance so the tone is even, and the numbers show them succeeding. The wrong curve was not caught by inspection: it let the string slide out from under the felt, and A6's contact came out at two and a sixth milliseconds where a real one is under one, with no stiffness of felt able to shorten it.

#### The felt's stiffness is set by measurement, because it cannot be looked up

Its units depend on the felt's power, so the number means nothing on its own and cannot be compared between registers or against anything published. What can be compared is how long the felt stays on the string. So the stiffness curve was chosen to put the contact times where the measurements say, and nothing else went into it.

A first attempt calibrated each anchor note separately against a hand-picked target, and produced a stiffness curve with a tenfold spike at A2 — which is not defensible, because a hammer's felt hardens smoothly toward the treble. The curve is smooth and the contact times it gives were then checked across the whole compass.

| note | strings | contact at 0.2 m/s | at 1.5 | at 6.0 | peak force at 6.0 | centroid 0.2 → 6.0 |
|---|---|---|---|---|---|---|
| A1 | 2 | 4.50 | 3.90 | 3.73 | 50 N | 345 → 827 Hz |
| A3 | 3 | 3.17 | 2.19 | 1.79 | 71 N | 385 → 881 Hz |
| A4 | 3 | 3.10 | 1.79 | 1.46 | 78 N | 491 → 1093 Hz |
| A6 | 3 | 1.42 | 0.77 | 0.65 | 54 N | 1719 → 2461 Hz |

Milliseconds. A real hammer is on the string for about four milliseconds in the bass and about one at the top, falling by something like a factor of two from the quietest blow to the loudest, with peak forces from a newton or two up to a hundred.

**The centroid is the measurement phase 2 lives or dies by**, and phase 0 set it up to catch exactly this. The plucked string it was measured against gives 571 Hz at every level, because a fixed excitation scaled by how hard it was hit cannot brighten. The hammer more than doubles the centroid across the dynamic range.

**In the bass the contact barely shortens with force, and that is not a shortcoming.** A single heavy hammer on a single slow string stays on until the string's own reflection returns from the agraffe and throws it off, which for the bottom A is four and a half milliseconds after the blow however hard it was struck. The brightening is there regardless, because it comes from the felt rather than from the contact's end.

#### Nothing anywhere may grow

All eighty-eight notes, struck at twelve metres per second — harder than anyone can play — and run for twelve seconds. The worst ratio of the last second's peak to the first's is seven parts in a thousand. This is a regression guard rather than a measurement, and worth re-running after any change to the loop, because an unstable string is silent until it is deafening. It is part of `make pianobench`.

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

**The hammer–string loop.** Done, in phase 2, and in one attempt rather than several: written out, the sample's updates reduce to a single scalar equation with exactly one root, which Newton's method reaches from any start. The trouble that was expected never arrived; the trouble that did arrive was elsewhere, in the impedance curve and in the sign of a reflection.

**Tuning against dispersion.** Done, in phase 1, and it was not hard once the fit was asked the right question: rather than correcting for the chain's delay, the fit solves for where the loop will actually ring and compares that against the stiff string. Everything the implementation does to the partials is then inside the error being minimised. The loss filter's delay belongs to the same problem and is in the same solve.

**Cost.** Two hundred waveguides with allpass chains is affordable only because most strings are still. The gating has to be cheap and must not click when a string wakes. Phase 1 settled how much filtering each note wants: up to forty-eight allpass stages in the bass, a handful at the top, and the heavy notes are the ones with a single string.

**Holding a decay law with a low-order loop filter.** Phase 1 got the measured power right and an individual partial's decay time to within about forty per cent, and that is where a three-shelf filter runs out. It is also probably the wrong target to chase further: a power law is a stand-in, and the physically grounded account of string damping — air, viscoelastic and support losses summed — gives a curve that is not a power law and may well be easier for a low-order filter to follow, because it comes from first-order physics rather than from a convenient exponent. That belongs with phase 6, where the soundboard's own frequency-dependent losses arrive in the same place.

**The bass.** Wound strings are the most inharmonic, the most strongly double-decaying, and the ones where phantom partials are audible. They will be the last to sound right.

## The two pianos, and their names

They stand beside each other. The sampler is good at what it is good at, and a modelled instrument is a different thing rather than a better one.

**The modelled piano is `mpxPiano`** — the plain name, because it is the one that will be reached for.

**The sampler becomes `mpxSPiano`**, the S for sampled.

**Neither slug moves.** A slug is what a saved patch stores, so the sampler keeps `mpxPiano` as its slug for ever, however it is named on the face; renaming it would empty every rack that has this piano in it. The modelled one therefore needs a slug of its own that is not the word anybody would guess, and the mismatch is invisible — a slug is never shown.

So: the name a person reads and the name a patch stores are different things here, deliberately, and the comment on the panel title says why.

## Open

Nothing. Phase 1 is next.
