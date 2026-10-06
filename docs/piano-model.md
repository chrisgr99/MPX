# A physically modelled piano

mpxPiano plays the Salamander sample set. This is the other thing: the instrument itself, modelled, so that what it does under the hands is a consequence rather than a setting. Sampled pianos are convincing until you ask them something the samples were not recorded doing — a key released while the pedal is half down, a note struck again before its damper has landed, a chord that rings sympathetically with one held silently. All of those are ordinary piano playing and none of them is in a sample set.

**It models the whole instrument, not the notes being played.** All two hundred-odd strings exist all the time, damped or not, coupled through one bridge. Sympathetic resonance is then not a feature that was added: it happens. As of phase 4 it does — strike a C and the G a twelfth above it answers, because the C's third partial is the G's first, and there is no resonance code anywhere in the model. The sustain pedal works because the dampers lift and the strings are already there. The undamped top two octaves ring along by themselves. A string that is not moving costs nothing, and most of them are not moving most of the time.

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

**Done.** `PianoNote` holds a note's two or three strings, the one hammer that strikes them, and the bridge that joins them.

#### The note is the object, not the string

A piano note is not a string; it is a small choir of them, slightly out of tune with each other and joined at one end. Almost everything interesting about the start and the end of a piano note comes from that, and none of it can be a string's private business, because the hammer and the bridge each need every string's state at the same instant.

So `PianoString` no longer has a `process`. It has four calls, and a sample goes like this: every string says what has reached its strike point; the hammer, knowing all of that, works out its force and how it is shared; every string takes its share and sends its waves away; every string says what has reached the bridge; the bridge, knowing all of that, works out its own motion and what goes back; every string takes its returning wave.

**The bridge had to become a place.** A rigid end can be a sign turned over in the middle of a delay line, which is what phase 2 had, and it costs half the memory and half the arithmetic. A bridge that **moves** cannot be, because the note's other strings are pushing on it at that same moment. So the section from the strike point to the bridge is now two one-way lines with the bridge between them. The agraffe stays as it was — an end that cannot move needs no junction.

**And the strike point is now always a junction, however short the note.** Phase 2 left the top of the compass undivided, where a period is eleven samples, and had to special-case both the sign of the loop and the junction's arithmetic — getting each wrong before getting it right. A section of one whole sample is no worse a model of a very short string than none at all, and it is the same model as every other note. There are now no special cases.

**The hammer no longer needs to be told how many strings there are**, which phase 2 had to fake with a count so that a lone string would resist like three. It pushes all of them and they resist it together.

#### The contacts are separate but the hammer is one

Each string compresses its own patch of felt by its own amount — they are a fraction of a cent apart and drift out of step — so each has its own force. But all those forces push back on one hammer, so each depends a little on all the others through how fast the hammer is slowing.

Solved by going round a few times rather than as a system, and the arithmetic says why that is enough: the term through which the contacts feel each other is the hammer's inertia, a sample squared over its mass; the term by which each stands alone is a sample over its string's impedance. At a piano's numbers the second is a hundred times the first, so the coupling is a small correction to nearly independent solves and three passes settle it.

#### The decomposition of the decay, which took being refused to find

The bridge was first told to the strings so their loss filters could divide it out, the idea being that the compass's decay figure would then be what came out of the finished instrument. The arithmetic refused: at any bridge impedance worth having, what the bridge absorbs is **several times a note's whole decay budget**, so making up for it would need a loop gain above one. A3 asked for eleven seconds and rang for two and a fifth, which is the bridge's rate and nothing else.

The refusal was pointing at the right answer. **A string's internal losses are the slow floor under everything; what the bridge takes is the sound leaving, and that is the fast one.** So the compass's curve is the string's own, and what it describes is the **aftersound** — the long tail, when the strings of a choir are out of step and barely moving the bridge at all. The initial fall is faster by whatever the bridge is good at. A note with one string has no out-of-step mode and so no aftersound, and rings for rather less than the curve says, because there is nothing to sustain it.

Phase 1's decay gate is therefore measured with one string and a rigid bridge, which is what it fitted. Measuring it through the finished note would be measuring two things at once and blaming either for the other.

#### The bridge impedance, and how easy it is to get wrong

About a thousand times a string's, which is a soundboard's measured driving-point impedance of a thousand-odd kilograms per second against a string's two. The number decides two things at once: how much of the sound leaves at every reflection, and how strongly the strings of a choir hear one another.

A hundred and forty was tried first. It is five times a midrange note's whole decay budget: every note rang for a fifth of the time it should, and the two rates came out the wrong way round, with the aftersound dying faster than the attack.

#### What it does

| note | strings | spread | first T60 | then T60 | ratio | lines cross at |
|---|---|---|---|---|---|---|
| A1 | 2 | 0.73 | 13.10 | 45.21 | 3.45 | 13.7 s |
| A2 | 3 | 0.60 | 6.42 | 24.29 | 3.78 | 6.7 s |
| A3 | 3 | 0.49 | 3.46 | 14.18 | 4.10 | 3.6 s |
| A4 | 3 | 0.40 | 1.78 | 7.21 | 4.04 | 1.9 s |
| A5 | 3 | 0.37 | 0.89 | 3.30 | 3.69 | 1.0 s |

Seconds, with the spread in cents. A real grand falls two to five times faster at first than afterwards. Forced to a single string the same notes give one rate and a ratio of exactly one, which is the control the measurement needed.

**Being slightly out of tune is what makes any of it audible.** Exactly tuned unisons would stay in step for ever and the aftersound would never arrive. A tuner aims at nought and nobody reaches it — a few tenths of a cent is a clean unison and about one is a tired one — and the residue is not a defect to be minimised: it is where the shimmer of a piano note comes from, and a note whose strings are exactly in tune sounds dead. The curve is a little wider in the bass, where the beats are slow enough to be heard as beats rather than as warmth, and tuners leave them so on purpose.

#### The null between the two rates, which is real

Where the in-phase part of the note has decayed to the level of the out-of-step part, the two **cancel**, and the note passes through a deep momentary null — twenty decibels at A3, in and out again within half a second. It is not an artefact. It is why a piano note seems to breathe rather than simply fade.

It also broke the first attempt at measuring the two rates. Fitting two lines and putting the bend wherever it fitted best reported the second rate as the **faster** one on every note, the exact opposite of the truth, because a knee-finding fit lands in that hole every time. The rates are now measured over two stretches of level where each plainly owns the sound: the first from just below the peak down to twenty-five decibels below it, which on every note is before the null, and the second from forty-five down to seventy-five, which is after it. Where the two fitted lines cross is reported as the bend, which is better defined than any point on the curve.

The second rate reads fifteen to thirty per cent longer than the string's own, and that is the measurement rather than the model: a note's out-of-step components sit at slightly different frequencies and beat against each other, so taking the strongest bin of a few reads an upper envelope, which follows the slowest of them rather than their sum.

#### A trap closed

`PianoString`'s fits are lazy, so that setting a note and then three other things about it costs one design and not four. The cost is that until the first sample has gone through, every accessor reports the **last** note's answers — so a measurement taken straight after setting the note reads a frequency of 220 Hz whatever the note is, and silently measures the wrong bin. It cost an hour of chasing a decay rate that was never wrong. There is now a `prepare`, which `PianoNote` calls as soon as it has finished setting its strings up, so nothing outside ever sees a stale answer.

#### What a note costs, which is phase 4's whole problem

Measured now, while there is still one note to measure.

| note | strings | chain stages | share of a processor |
|---|---|---|---|
| A0 | 1 | 16 | 0.0014 |
| A2 | 3 | 24 | 0.0050 |
| A4 | 3 | 16 | 0.0034 |
| A6 | 3 | 6 | 0.0019 |
| C8 | 3 | 1 | 0.0013 |

**All eighty-eight ringing at once comes to 0.29 of one processor**, which is a great deal better than feared when phase 1 chose to spend up to forty-eight allpass stages on a bass note. Every damper up is the worst case and a rare one; the way out was always that a string nobody has struck costs nothing, and it turns out not to be needed for the instrument merely to run. The gating still has to be cheap and must not click when a string wakes.

### Phase 4 — the whole board

**Done.** `PianoBoard`: eighty-eight notes, and every string of them tied to one bridge.

#### One bridge, not eighty-eight

Phase 3 gave each note a bridge of its own, which is enough to make the strings of one note hear each other and so to produce the double decay. It is not a piano. On a real instrument every string is tied down to the same bridge on the same soundboard, and that single fact is what makes a piano sound like a room rather than like a set of strings.

**There is no resonance code.** The strings were always there and the bridge was always shared, and what follows from that is what a piano does. Striking one note moves the bridge, and every other string in the instrument is driven by that motion — strongly if it happens to be in tune with one of the struck note's partials, imperceptibly otherwise. The sustain pedal of phase 5 will work for the same reason and will need no resonance code either.

**This is why it cannot be eighty-eight notes mixed together.** It is one junction, not a sum. And the same fact seen from the other side is that the other strings *load* the bridge: two hundred and sixty strings hanging on it make it harder to move than three do, so every note's decay depends on the whole instrument being there.

`PianoNote` keeps a `process` of its own, which puts a private bridge on one note. That is a note on a bridge with nothing else attached, which is exactly what phases 1 to 3 were measured on, so keeping it means those measurements still mean what they said.

#### The sum of the impedances is over every string, not the ringing ones

A string is tied to the bridge whether or not it is moving: it is still there and still has to be shifted. Leaving a sleeping string out of that sum would make the bridge easier to move the quieter the music got, so every note's decay would creep as the texture thinned and a chord would die at a different rate from the notes composing it. The sum is therefore a constant of the instrument, fixed once. Sleeping only ever leaves a string out of the *other* sum — the one weighted by what the strings are actually doing, which a silent string contributes nothing to anyway.

The strings hang 469 kg/s on the bridge between them, against the soundboard's own two thousand: a quarter again as hard to move as the soundboard alone.

#### One criterion for sleeping, not two

The first attempt slept a note when its own motion was under the floor and woke it when the bridge was over it, and those are different questions asked at different moments. A note woken by the bridge has not moved **yet**, so it was put straight back to sleep in the same sample, before it had been given anything to be driven by. Nothing ever resonated, and the measurement meant to prove phase 4 reported a piano with one string in it.

So the bridge's motion decides both. While the bridge is moving, nothing sleeps — every string is being driven and has a right to be heard. When the bridge has gone quiet, a note that is itself quiet may sleep. The two can then never disagree.

#### The gate: striking one note sets the others ringing

C3 struck and nothing else touched. Two seconds later, how much every note is moving, in decibels below what C3 itself was doing at the moment of the blow:

| note | dB | why |
|---|---|---|
| C3 | -43.5 | struck |
| C2 | -50.4 | C3 is its partial 2 |
| C4 | -52.5 | partial 2 of C3 |
| F1 | -61.2 | C3 is its partial 3 |
| C1 | -63.1 | C3 is its partial 4 |
| F2 | -64.9 | its 3rd = C3's 2nd |
| G2 | -67.8 | its 4th = C3's 3rd |
| F3 | -69.1 | its 3rd = C3's 4th |
| G3 | -74.6 | its 2nd = C3's 3rd |
| **G4** | **-74.8** | **partial 3 of C3 — the twelfth, which is the gate** |
| C5 | -80.5 | partial 4 of C3 |
| B2 | -86.9 | — |

Every note that answers is accounted for by a partial it shares with C3, and finding that out took fixing the explanation rather than the model: looking only for a whole-number ratio between the two **fundamentals** explains C2 and C4 and leaves F2, G2, F3 and G3 looking like noise. They are not noise. F2's third partial is C4, which is C3's second; G2's fourth is G4, which is C3's third. Two strings talk through any partial they have in common.

**The low notes answer loudest, which is right**, and is the same mechanism seen from the other end: C3 is the second partial of C2 and the fourth of C1, so those strings are being driven at a frequency they ring at. It is what a real piano does with the pedal down, where the bass hums under everything. The bass also holds what it picks up for twenty seconds, so it has longer than anything else to gather it.

**The control**, which the measurement needs as much as the result: the same strike with the bridge made rigid leaves the loudest other note at -79 dB instead of -50. Nothing resonates through a bridge that cannot be shifted.

#### And a note's decay depends on the whole instrument being there

| A3 | first T60 | then T60 |
|---|---|---|
| on the board | 3.62 | 6.86 |
| alone | 3.46 | 14.18 |

The initial fall is a little slower, because the loaded bridge is harder to shift, so the note keeps rather more of its first energy. **And the aftersound is halved**, which is the more interesting half of it. A note alone has nowhere to lose its out-of-step energy to and rings on at its own internal rate; a note on a board full of undamped strings leaks it into every string that shares a partial with it — the halo above is where it goes. Sympathetic resonance is not free, and this is its price measured. On a real piano it is the price of the sustain pedal, and phase 5, which puts the dampers back on the other strings, should hand most of it back.

#### What it costs

| | awake | share of a processor |
|---|---|---|
| silence, nothing struck | 0 | 0.004 |
| one note | 88 | 0.48 |
| a four-note chord | 88 | 0.49 |
| every note at once | 88 | 0.48 |

**Silence is free and nothing else is**, which is worth being plain about rather than dressing up. Once anything at all is sounding the bridge is moving, so every string in the instrument is being driven, and a string being driven has a right to be heard — so they are all awake, and one note costs what the whole keyboard does. That is the resonance doing its job rather than the gating failing to do its own.

Phase 4's gate was that the instrument runs at a sensible cost with every damper up, and half a processor for a physically modelled piano is sensible. If phase 7 wants it cheaper, the thing to do is **not** a higher floor — the bridge is loud either way — but to check the sleeping notes a few at a time on a rotation, so each is woken briefly and put back unless it has something to say.

**Building it takes half a second**, for every string's dispersion chain and loss filter. That is once, when the sample rate is set, and phase 7 has to keep it off the audio thread and out of the way of a module appearing in a rack.

It was a second and a fifth until the three strings of a note stopped each solving the same design from scratch. They are the same string to within a fraction of a cent, and `tune` already holds a design good across a twenty-fourth of an octave before refitting — a hundred times the spread of a unison — so the first string solves and the rest adopt its answer, re-solving only their own delay-line length and flat gain from their own pitch. That is what makes the detuning real rather than nominal, and it costs no search.

#### A regression phase 3 left behind, found here

Restoring the long bass chains needed fixing something phase 3 had quietly broken. The chain-length guard allowed the dispersion chain three fifths of what the loop had left, which was generous when the only whole-sample section was the short one up to the agraffe. Phase 3 split the bridge section into a way out and a way back, putting half the period into whole samples, and three fifths of the remainder was no longer enough: the bottom A's forty-eight-stage chain wants seven hundred samples of a seventeen-hundred-sample period and was refused, so it settled for sixteen stages and five cents of error where phase 1 had had it at one and a tenth.

What is really needed is that the interpolating read is left a few samples to work with, so that is now the test. And at the top of the compass the bridge has to be put nearer than halfway: the top C's period is eleven samples, and a bridge five and a half of them away leaves the chain and the read nothing at all, so the fit refused every candidate and the note came out with no stretch whatever. A dozen samples held back is enough for both.

With both fixed, the worst partial of any note in the compass is between nought and two cents.

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

**Cost.** Settled in phase 4: the whole instrument is half a processor, and silence is free. Most strings being still turned out to be no help at all once anything is sounding, because a shared bridge drives every string in the instrument — so the saving is in the silence between the music rather than during it, and it is an optimisation rather than the thing that makes the instrument possible.

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
