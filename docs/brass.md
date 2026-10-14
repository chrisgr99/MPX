# Brass

A module that plays one MPX part on a physically modelled brass instrument, starting with the tenor trombone. The lips, the air column, the bell and the player are all inside it, as the string, the bow, the body and the bowing are inside the bowed-string modules.

Names: not fixed. One module, provisionally mpxTrombone, while the engine is unproven; the trumpet, flugelhorn and horn follow as their own slugs on the same engine.

Status: phases 1 and 2 built — the benchmark (`research/brass/trombone-benchmark.md`, `make brassref`) and the air column, as a grid and as delay lines with grids at the ends, both within 3 cents of the calculation in frequency (`make brassbore`). Not yet chosen between; no lips yet. The literature is surveyed in `research/brass/` (five files: the physics and methods common to brass, the trumpet and flugelhorn, the trombone, the French horn, and the player with existing implementations).

## Purpose

To push the state of the art, not to match it. Two kinds of brass model exist today and neither does what this one aims to.

Research models are physically complete and not playable. The Edinburgh finite-difference brass environment matches measured input impedances of a trumpet, a trombone and a horn to about 1 per cent in frequency (Bilbao and Chick 2013), and has a score format with physical units, but it runs offline or near real time, is driven by breakpoint files, and leaves out the slide and nonlinear propagation (Harrison et al. 2015). The finite-volume models that do include nonlinear propagation take minutes per second of sound (Berjamin et al. 2017).

Commercial models are playable and simplified. SWAM Solo Brass is a physical model with idealised straight pipes and spectral correction controls; Sample Modeling's brass is recorded material made continuous by a modelling layer, and owners commonly rate it the more convincing of the two; Arturia's Brass, built with IRCAM and now discontinued, made its physical model playable by finding control parameters for every note with an offline optimisation. All of them are played from MIDI as it arrives, so none can plan.

What nobody has built is a physically complete model, run in real time, played by a performer who reads the score ahead. MPX has the score ahead: notes are announced on the cable four and a half seconds before they sound, and the bowed strings already plan their bowing from that and rehearse difficult starts on a copy of the instrument. With the strings the largest gains came from the player — look-ahead bowing, rehearsed starts, planned bow changes — and listeners judge brass realism mostly on attacks, slurs and changes of dynamics (research/brass/player-and-implementations.md, B5), which are the player's.

Where this is least certain: matching the timbre of recorded instruments with physics alone, which is where pure physical models are most often called synthetic; the lips, the least certain physics in the instrument; and the absence of published measurements for some of what a player does (vibrato rates, tonguing speeds, trombone blowing pressures across the dynamics).

## Why the trombone first

The best open data of the four instruments: a table of every tube section and the bell's shape (Smyth and Scott 2011, open access), the measured input impedance of a named instrument (Bilbao and Chick 2013, Conn 8H), a raw impedance measurement file (UNSW), measurements of ten trombonists playing (Boutin et al. 2015, 2020), and anechoic recordings at three dynamics (University of Iowa). A real-time finite-difference trombone exists as C++ source under the MIT licence (Willemsen et al. 2021; github.com/SilvinWillemsen/cppBrass, LICENSE in the Trombone folder), to read and to measure against.

The simplest air column: mostly cylindrical, which a waveguide represents exactly, and no valves. The slide is a change of length, which also gives glissando.

The best-understood brassiness: shock waves were first measured in a trombone (Hirschberg, Gilbert, Msallam and Wijnands 1996), and the same work showed that the nonlinearity can be left out of the loop that sets up the oscillation.

The trumpet comes second (valves; the best pressure and spectrum data; a fully published real-time algorithm, D'haes and Rodet 2002), the flugelhorn third (the trumpet's valves and lips on a conical bore for which no profile is published), and the horn last (no open bore of a modern double horn, the hand in the bell, a long conical bore that is the waveguide's weak spot, and high notes on closely spaced resonances).

## The instrument in outline

A brass instrument is a self-sustained oscillator in the same form as the bowed string: a nonlinear exciter coupled to a nearly linear delay-based resonator (McIntyre, Schumacher and Woodhouse 1983).

**The lips** are a valve that the mouth pressure pushes open: in the simplest model one mass on a spring with damping, driven by the pressure difference across the lips over an effective area, with Bernoulli flow through the opening and a swept flow from the moving lip surface (Adachi and Sato 1995; Harrison et al. 2015). They sound above their own mechanical resonance. Measured lips have more than one resonance and change from outward-striking to sideways-striking in the high register (Yoshikawa 1995; Newton et al. 2008).

**The air column** — mouthpiece, leadpipe, slide, gooseneck, tuning slide and bell — is described by its input impedance. On the Conn 8H in first position the measured maxima are 38.3, 113.2, 174.0, 233.2, 296.0, 350.9 and 410.1 Hz: peaks 2 to 7 lie near a harmonic series on about 58.5 Hz, and the first, at 38 Hz, does not belong to it. The pedal note B flat 1 has no peak under it and is held by the higher peaks at its harmonics. The mouthpiece raises the impedance across the playing range.

**The bell** sets a cutoff near 1000 Hz on the trombone: below it most of the wave is reflected and sets the pitch; above it the wave leaves. What is heard is the bell's high-pass transfer of the pressure inside, with maxima and minima below the cutoff and a nearly flat response above it on axis (Beauchamp 2012).

**Brassiness** is propagation, not the lips. At fortissimo the wave leaving the mouthpiece steepens along the narrow cylindrical slide until it is nearly a shock at the slide's end, and the radiated sound gains strong high harmonics (Hirschberg et al. 1996; Gilbert and Petiot 1997). The lips' open area hardly changes when the instrument enters the brassy regime (Stevenson et al. 2009). Above the bell cutoff the harmonics are typically 20 dB stronger relative to the mouthpiece at ff than at mf (Beauchamp 2012).

**The player** chooses the slide position and the partial, sets the lip resonance near the wanted bore resonance, and sounds about 1.1 per cent above it (Boutin et al. 2015); sets the mouth pressure, which sets the level and, through propagation, the brightness; starts the note with the tongue; and joins notes with the slide, the lips or a light tongue.

## What the model is built from

Every input below is published; nothing needs an instrument to be measured.

| What | Source | Form | Use |
|---|---|---|---|
| Tube sections and bell shape | Smyth and Scott 2011, Table 2; Bessel-horn bell, length 0.502 m, mouth radius 0.108 m, flare 0.7 (Table 1) | numbers, open access | the bore |
| Mouthpiece | Smyth and Scott 2013, lumped cup volume (5 cubic centimetres) and throat | numbers | the mouthpiece |
| Input impedance of a named trombone | Bilbao and Chick 2013, Table I, Conn 8H with a Denis Wick 5AL in first position | numbers, open preprint | a target |
| Raw impedance | UNSW, ZTbn_dry_26_3degC.xls | downloadable file | a target |
| Bell reflection and transmission | Smyth and Scott 2011 | plotted | a check on the fitted bell |
| Players: offset from the resonance, bending range, pressures, lip motion | Boutin et al. 2015, 2020 | numbers in the text | targets for the lips and the player |
| Lip parameters | Harrison-Harsley 2018 via Willemsen et al. 2021; Velut et al. 2017; Smyth and Scott 2013 | numbers | starting values |
| Mouthpiece to bell transfer at pp, mf, ff | Beauchamp 2012 | plotted, open access | a target for brassiness |
| Anechoic notes, tenor and bass trombone, pp, mf, ff | University of Iowa Musical Instrument Samples: "may be downloaded and used for any projects, without restrictions" | sound files | the sound's targets, and listening |
| Real-time finite-difference trombone | Willemsen et al. 2021, cppBrass | C++, MIT | to read, and to measure against |

OpenWInD (Inria) and ART compute input impedance and reflection functions from a bore profile. Both are GPL and MPX is MIT, so neither goes into the plugin; they are run offline, and whether the tables they produce may be shipped is to be checked before any are.

## The benchmark

Built first, before any of the instrument, so that every later phase is measured against something fixed. Each target says what is compared, against what, and how close is close enough; where nothing published says, the target is your ear.

**B1. Resonances.** The model's input impedance, computed by driving its mouthpiece with a flow impulse, against the Conn 8H maxima: the first peak near 38 Hz and outside the series; peaks 2 to 7 within 2 per cent of the measured frequencies and within 20 cents of a harmonic series. The model is built on a different instrument's geometry, so this is a plausibility test, not an identity; the exact check is the same geometry computed offline with OpenWInD.

**B2. Where the note sounds.** Played notes sit about 1.1 per cent above the impedance peak (85 per cent of players' samples above it). The lips can bend a note from about 10 to 12 per cent below to about 8 to 9 per cent above its normal frequency (F3 and B flat 3), and the limits fall where the impedance is about 15 per cent of its peak; lipping up past the limit jumps to the next register, lipping down below B flat 2 stops the sound (Boutin et al. 2015, 2020).

**B3. The pedal.** B flat 1 can be played and settles on its own frequency with no resonance under it; the 38 Hz resonance is hard to play (Boutin, Smith and Wolfe 2020; Velut et al. 2017).

**B4. Pressures.** Normal playing of F3 and B flat 3 at a mouth pressure of about 1.3 to 1.4 kPa; the alternating pressure in the mouthpiece about 20 times the mouth's at a low note; mouthpiece levels above 165 dB at fortissimo (Boutin et al. 2015, 2020; Elliott et al. 1982). Each note has a threshold pressure; the form of Fletcher and Tarnopolsky's trumpet law — threshold rising with frequency, then about 15 dB per doubling of pressure, then saturation — is expected to hold, with its constant found from the model.

**B5. Brassiness.** The transfer from mouthpiece to bell, measured on the model at pp, mf and ff: below about 1000 Hz independent of dynamic, above it about 20 dB higher at ff than at mf (Beauchamp 2012); the brassy change between forte and fortissimo (Norman et al. 2010).

**B6. The sound against the Iowa recordings.** For each note and dynamic recorded: the spectral envelope (its rise below the cutoff, the slope above it and how that slope flattens with level), the spectral centroid, the onset time to within 3 dB of the steady level, and the decay at release. Measured from the recordings by a bench written for it, into a table kept in `research/brass/`; the recordings themselves are downloaded, not kept in the repository.

**B7. Joins.** A lip slur takes 80 to 100 ms between steady states, as measured on the horn (Logie et al. 2009); nothing is published for the trombone, so this is provisional.

**B8. Cost.** Real time with margin in a patch of several instruments: no more per voice than a bowed string at the same Fidelity level, measured as the strings are, by the share of real time and the worst block of 256 samples in a real-time run.

**B9. Your ear.** The model's notes set beside the Iowa notes at the same pitch and dynamic, and beside the published demos of the commercial instruments, at a fixed level, one file at a time.

## Phases

### 1. The benchmark — built

`research/brass/trombone-benchmark.md` holds every target with its source, its numbers and what passes. `make brassref` (test/brassref.cpp) reads the twelve Iowa scales from build/iowa-trombone, finds all 99 notes — the threshold between note and silence is the middle of those that find exactly the scale's number of notes, since some scales are digitally silent between notes and some have the room's noise 20 to 30 dB under them — checks each against the note it should be, and writes `research/brass/iowa-tenor-trombone.tsv`. The levels step 13 dB from pp to mf and 8 from mf to ff; the spectral centroid rises 1.55 and 1.85 times; above 1 kHz the spectrum falls about 25 dB an octave at pp and mf and 11 at ff.

### 2. The air column, two ways — built

`make brassbore` (test/brassbore.cpp) measures everything below.

**The calculation in frequency** (`BrassBore`): each section a lossy two-port, multiplied from the bell back to the cup, the radiation at the mouth Silva et al.'s (2009) fit to Levine and Schwinger's unflanged pipe. The wall's losses are the leading boundary-layer terms — the air heavier by 2 sqrt(nu) / (r sqrt(j omega)) and more compressible by 2 (gamma - 1) sqrt(nu / Pr) / (r sqrt(j omega)) — which are the Webster-Lokshin equation's in time, so the timed columns and this are the same physics. It is the reference both timed columns are measured against.

**The geometry.** Smyth and Scott's table alone plays 3 to 6 per cent sharp of the Conn 8H with its upper resonances spread too wide: it has no leadpipe and its mouthpiece was lumped with no length. With a tenor's mouthpiece as tubes — a cup of 5 cubic centimetres, a throat 6.7 mm across and 7 mm long, a backbore widening to 11 mm over 70 mm — and a leadpipe widening to the slide's bore over its first 0.23 m (`BrassBore::tromboneWhole`, typical dimensions, not measured), and the tuning slide pulled out 33 mm, the instrument is 2.746 m from the cup to the bell's mouth, against the Conn's measured 2.74 m, and its first seven resonances are within 25 cents of the Conn's: -11, 0, +6, 0, +8, +23 and +25 cents (B1). For playing in time the cup and the 7 mm throat are lumped, a volume and an inertance, and the backbore stays a tube (`BrassBore::tromboneTimed`): lumped with the throat, its 70 mm went missing and every resonance came out 25 to 65 cents sharp; kept, the timed geometry is within 4 cents of the whole.

**On a grid** (`BrassGrid`): the Webster-Lokshin equation, pressure at points and velocity half way between, the spacing a little over c times the step (a Courant number of 0.999: at exactly 1 it was unstable at 88.2 kHz for one pull of the tuning slide and not another), the half-order derivatives from memory variables at every point — four, fitted by least squares to s to the half between 20 Hz and 0.45 of the rate. The cup, the throat and the first point's half cell are solved together each step: stepped one after another, the throat's inertance against that cell, about 0.14 cubic centimetres, ran away within a few hundred steps. The radiation is an inertance, 0.6133 of the mouth's radius, in parallel with the characteristic resistance, and the calculation it is checked against uses the same. Against the calculation, peaks 2 to 10 within 1.8 cents at 44.1 kHz and 1.4 at 48 and 88.2, the first within 4 cents, the heights within 1 to 4 per cent. Three memory variables give 4.5 cents, two 22. It costs 12 per cent of one core at 44.1 kHz, 14 at 48 and 48 at 88.2, in plain double precision with no vector instructions.

**As delay lines with grids at the ends** (`BrassWaveguide`): two thirds of the trombone — the inner and outer slide tubes, the crook, the gooseneck and the tuning slide's pull — are tubes of one radius each, carried as pairs of delay lines read by third-order Lagrange interpolation; only the backbore and leadpipe (a grid of 38 points at 44.1 kHz) and the tuning slide's cone and the bell (98 points) are grids, which meet the delay lines at ports. The slide's tubes keep their own radii, 6.9, 7.2 and 7.4 mm, and scatter where they meet: made one radius, the resonances moved by up to 30 cents. Each outer tube keeps 2 cm of the inner tube beside it, so that it is never too short to read while the slide is closed; the crook made 7.2 mm instead, to the same end, cost up to 6 cents. The wall's losses over the run of tubes are applied once each way as the wave leaves it, as first-order shelves — each of the half-order fit's high-passes turned into the exponential of itself by a Padé approximant — redesigned when the slide moves. Against the calculation, with the slide closed, 25 cm out and 53 cm out, peaks 2 to 10 within 3 cents, the first within 2 to 8, the heights within 7 per cent. It costs 4.8 per cent of one core at 44.1 kHz and 5.7 at 48, a fifth of the grid's points and two fifths of its computing; and moving the slide is changing two delays.

**The positions**, from the calculation: the slide out 8.3, 17.0, 26.2, 35.8, 45.8 and 56.4 cm for second to seventh, each the extension that lowers the mean of resonances 2 to 8 by a semitone more. Fourth at 26.2 cm lies between Boutin et al.'s measured 22.7 and the estimate of 27.7 from a uniform length.

Not yet measured: the slide moving while the instrument sounds, which needs the lips (phase 3).

### 3. The lips

A one-mass outward-striking valve with Bernoulli and swept flow and a collision term when the lips close, its mass and stiffness following the lip frequency as Elliott and Bowsher found (mass inversely proportional, stiffness in proportion), so that the embouchure is one number; the open area proportional to the opening, or to its square for low notes as Mattéoli et al. (2021) found fits better. Starting values from the published sets, which differ by a factor of about 2.5 in mass per unit area. Measured against B2, B3 and B4 with notes held at fixed pressure and lip frequency. A second degree of freedom (Adachi and Sato 1996) only if one mass cannot meet B2.

### 4. Brassiness

Nonlinear steepening of the outgoing wave along the cylindrical tubing, with the feedback loop left linear, as Hirschberg et al. found admissible: an amplitude-dependent delay in the waveguide's slide (Vergez and Rodet 2000; Msallam et al. 2000), or the nonlinear term in the grid. The steepened wave carries energy above 40 kHz at fortissimo, so this part runs oversampled or band-limited. Measured against B5 and the high end of B6.

### 5. The sound that leaves

The bell's transmission, the radiation from its mouth, and a fixed directivity for a listener in front; measured against B6 and B9.

### 6. The player

The bowed player's architecture reused: notes announced ahead, a planner on its own thread and a playing copy on the audio thread, decisions numbered and handed over as plans, Fidelity levels from full rehearsal to none, diagnostics with the unrehearsed count and REC. What it decides:

- **Position and partial.** Each note's slide position and partial, preferring the position that joins the next note without a slide movement against the partial's direction (the classic natural slur), and alternate positions where they make a phrase smoother.
- **Tables before playing.** For each position and partial, the lip frequency that locks to it and its threshold pressure, found once per instrument by sweeping the lip frequency on the model (the NESS method) or by linear stability analysis on a fitted modal model of the bore (Velut et al. 2017), kept as a table.
- **Rehearsed starts.** The brass risk at a start is the lips locking to the wrong resonance, or late. Candidate starts — lip frequency offset, pressure step and ramp, tongue release — are tried on a copy of the instrument for a few round trips of the bore and the one that locks soonest to the right resonance is played, as the bowed strings rehearse theirs. Copying a waveguide or a grid of a few hundred points is cheap.
- **Articulation.** The tongue as a valve upstream of the lips: pressure built behind it, then released — about a 2 kPa step for a sharp mezzo-forte attack, under 1 kPa with a small pre-blow of about 250 Pa for a soft one (Hruška et al. 2015, trumpet) — and the legato tongue as a brief partial constriction.
- **Joins.** A slide change, a lip slur (the lip frequency glides to the next resonance with a pressure adjustment, 80 to 100 ms), a light tongue across a slide change in the same direction as the partial, or a written glissando with the slide.
- **Dynamics** as a mouth-pressure target per note scaled from its threshold, never as an output gain, so that brightness follows from the physics.
- **Vibrato** by the slide, its rate and depth measured from recordings with a pitch tracker, since none are published.

Measured against B2, B4, B7 and B9 in phrases from the scores in use and from recordings.

### 7. The module

mpxTrombone, laid out as the bowed strings are: one column, every name in capitals in the heading face, MPX in, the ports for playing without a cable, a level lamp and one output, Fidelity and Economy in the right-click menu, help text in the house style.

### 8. Afterwards

The bass trombone (the F and D valves as switched tubes joined at a valve junction, Bilbao 2011), mutes (straight, cup, Harmon, plunger: a changed bell reflection with a cavity resonance, and a filter on what leaves — Campbell, Gilbert and Myers 2021), then the trumpet, the flugelhorn and the horn on the same engine.

## Tests

As for the bowed strings, each phase leaves a bench that measures what it claims: the reference table from the recordings (phase 1); the impedance of each air column against B1 and its cost (2); held notes at fixed pressure and lip frequency against B2 to B4 (3); the transfer at pp, mf and ff against B5 (4); notes against the recordings (5); phrases with the unrehearsed count and REC as the strings have them (6 and 7).

## Open questions

- Whether one lip mass is enough over the trombone's range, or the second degree of freedom the measurements describe is needed.
- How far the two published lip parameter sets, a factor of 2.5 apart, can be reconciled by the benchmark.
- Whether nonlinear propagation applied to the outgoing wave alone is enough, or the reflected wave's steepening matters for the lips at fortissimo.
- The slide's position for each note: Boutin et al. give 22.7 cm for fourth position, a uniform-length estimate 27.7 cm.
- The bell's length in Smyth and Scott, 50.2 cm in one table and 56.7 in the other.
- Whether a moving slide's Doppler shift and volume change are audible; nothing has been measured.
- Whether tables computed with GPL tools may be shipped in an MIT plugin.
- Trombone blowing pressures from pp to ff, brass vibrato and tonguing rates: not published, to be found from the model, from recordings, or by ear.
