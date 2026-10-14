# Trombone benchmark

The targets the modelled trombone is measured against, phase 1 of docs/brass.md. Every number here is published or measured from freely usable recordings; nothing needed an instrument. Sources are in research/brass/trombone.md, physics-and-methods.md and player-and-implementations.md unless given here. Where a target says "your ear", no published figure exists and the judgement is Chris's.

## B1. The air column's resonances

The model's input impedance, computed by driving its mouthpiece with a flow impulse, against the measured maxima of a Conn 8H Artist Symphony tenor trombone with a Denis Wick 5AL mouthpiece, slide in first position, measured with BIAS at 19 to 20 degrees C (Bilbao and Chick 2013, Table I):

| Peak | Measured, Hz | Computed by them, Hz |
|---|---|---|
| 1 | 38.3 | 38.2 |
| 2 | 113.2 | 111.5 |
| 3 | 174.0 | 171.7 |
| 4 | 233.2 | 230.1 |
| 5 | 296.0 | 293.8 |
| 6 | 350.9 | 353.4 |
| 7 | 410.1 | 408.6 |

Peaks 2 to 7 divided by their numbers give 56.6, 58.0, 58.3, 59.2, 58.5 and 58.6 Hz: a harmonic series on about 58.5 Hz (B flat 1 is 58.27 Hz), from which the first peak, at 38 Hz, stands apart.

Pass: the first peak between 33 and 43 Hz and outside the series; peaks 2 to 7 within 2 per cent of the measured frequencies, the instrument tuned as a player tunes it, by its tuning slide, to the Conn's fourth peak. The second peak lies 51 cents under the series of the fourth on the Conn itself, so being near a harmonic series is not a test. The model's geometry is Smyth and Scott's trombone, not the Conn 8H, so this is a test of plausibility; the exact test is the timed columns against the calculation in frequency for the same geometry (BrassBore), within 1 per cent. The UNSW raw impedance file (ZTbn_dry_26_3degC.xls) is a second, independent curve.

Met (docs/brass.md, phase 2): with a tenor mouthpiece and leadpipe as tubes and the tuning slide pulled 33 mm, the model is 2.746 m long against the Conn's 2.74 m and its first seven peaks are -11, 0, +6, 0, +8, +23 and +25 cents from the Conn's; both timed columns are within 3 cents of the calculation.

## B2. Where a note sounds, and how far it bends

Ten trombonists, beginner to professional, B flat 2 to F4, Yamaha YBL 321 (Boutin, Fletcher, Smith and Wolfe 2015; Boutin, Smith and Wolfe 2020):

- The playing frequency is above the nearest impedance peak in 85 per cent of samples (88 per cent for professionals), by 1.1 per cent on average, about 19 cents.
- Bending: F3 from 12.1 per cent below to 8.1 per cent above its normal frequency; B flat 3 from 10.0 per cent below to 8.9 per cent above. Players describe it as about half a semitone up and a whole tone down.
- The bending range is roughly symmetrical about the impedance peak, not about the normal playing frequency, and its limits fall where the impedance has dropped to about 15 per cent of the peak.
- Lipping up past the limit jumps to the next register; lipping down below B flat 2 stops the sound.

Pass: the model's notes at the pressures of B4 sound between 0 and 2.5 per cent above the peak; its bending limits within 3 per cent of those above, by sweeping the lip frequency at a fixed pressure; the two endings as described.

Measured (docs/brass.md, phase 3, Adachi and Sato's hinged lips): with the lips at 0.98 of the resonance F3 and B flat 3 sound 2.4 and 2.6 per cent over it; F3 bends from 17.7 per cent under to 10.5 over, B flat 3 from 14.5 under to 4.6 over. Wider down and narrower up than players; the offset is the player's to set.

## B3. The pedal note

B flat 1 (58.3 Hz) has no impedance peak under it and is played because its second and higher harmonics fall on the second and higher resonances; the 38 Hz resonance is hard to play (Boutin, Smith and Wolfe 2020). A one-mass outward-striking lip on a modal air column predicts the pedal by linear stability analysis (Velut et al. 2017); on a bass trombone its playing frequency is about a semitone under its threshold frequency, and an open area proportional to the square of the opening fits low notes better (Mattéoli et al. 2021).

Pass: B flat 1 sustains at a steady pitch within 20 cents of 58.3 Hz; nothing steady at 38 Hz at any lip frequency.

Measured: steady notes either side of B flat 1, 56.8 Hz with the lips at 55 Hz and 59.7 with them at 60, starting at 520 to 600 Pa.

## B4. Pressures

- Mouth pressure about 1.3 kPa across the lips in normal playing of F3 and B flat 3, 1.4 kPa in the example low note (Boutin et al. 2015; Boutin, Smith and Wolfe 2020). Boutin et al. also give the alternating pressure in the mouthpiece as about 20 times the alternating pressure in the mouth: a comparison with the vocal tract, which is not modelled, and so not a target until it is.
- Mouthpiece pressure levels above 165 dB at fortissimo, about 3.6 kPa rms (Elliott, Bowsher and Watkinson 1982, as quoted by Beauchamp 2012).
- Each note has a threshold pressure. On the trumpet it rises with the note's frequency (about 4 to 8 Pa a hertz), the level then rises about 15 dB for each doubling of pressure and saturates at about 3 dB a doubling (Fletcher and Tarnopolsky 1999). No trombone table from pp to ff has been published; the form of the law is expected to hold and its constant is found from the model.

Pass: F3 and B flat 3 at mf from mouth pressures within 30 per cent of 1.3 kPa; a threshold rising with frequency; the level against pressure in the three regions of the trumpet law.

Measured: thresholds 500 Pa on B flat 2, 800 on F3, 1060 on B flat 3, rising to 2240 on the eighth resonance; the level against pressure is not yet measured.

## B5. Brassiness

- Mouthpiece to bell, 2 m on axis, at pp, mf and ff: below the bell cutoff of about 1000 Hz the transfer hardly depends on the dynamic; above it, harmonics are typically 20 dB stronger relative to the mouthpiece at ff than at mf (Beauchamp 2012).
- The change to a brassy sound comes between forte and fortissimo, and some players move it at a constant dynamic with the embouchure (Norman et al. 2010). The lips' open area hardly changes as it does (Stevenson et al. 2009).
- In the recordings of B6, from mf to ff the slope of the spectrum above 1 kHz flattens from about minus 25 to minus 11 dB an octave, the share of the sound above 1 kHz rises by about 4 dB, and the spectral centroid nearly doubles.

Pass: the model's transfer within 3 dB of independent of dynamic below 800 Hz, and 15 to 25 dB higher above 1.5 kHz at ff than at mf; and B6.

## B6. The sound, against recordings

The University of Iowa Musical Instrument Samples, tenor trombone, recorded before 2012: chromatic scales from E2 to C5 at pp, mf and ff, one Neumann KM 84 in an anechoic room, 16 bits at 44.1 kHz. The page says they "may be downloaded and used for any projects, without restrictions". They are not kept in the repository; test/brassref.cpp says where they are. `make brassref` reads all twelve scales, finds every note (99), checks each against the note the scale should have there, and writes `research/brass/iowa-tenor-trombone.tsv`: for each note its pitch and its deviation in cents; its level over the middle half, in decibels of full scale; its onset, from 30 dB under that level to within 3 dB of it; its release, the reverse; the levels of its harmonics to 10 kHz against the strongest; its spectral centroid, amplitude-weighted over the harmonics; the slopes of its spectral envelope under and over 1 kHz, by least squares on decibels against octaves; and its share over 1 kHz.

Medians by register and dynamic:

| register | dynamic | level, dBFS | onset, ms | centroid, Hz | centroid over pitch | slope under 1 kHz, dB an octave | slope over 1 kHz, dB an octave | share over 1 kHz, dB |
|---|---|---|---|---|---|---|---|---|
| E2 to B2 | pp | -52.2 | 106 | 590 | 5.67 | -0.4 | -21.3 | -18.6 |
| E2 to B2 | mf | -39.4 | 56 | 877 | 8.44 | +2.7 | -28.2 | -8.7 |
| E2 to B2 | ff | -32.0 | 43 | 1526 | 15.39 | +2.7 | -12.2 | -5.2 |
| C3 to B3 | pp | -48.9 | 126 | 602 | 3.40 | -1.8 | -24.5 | -17.1 |
| C3 to B3 | mf | -35.9 | 108 | 973 | 5.38 | +2.0 | -25.3 | -7.2 |
| C3 to B3 | ff | -27.5 | 61 | 1927 | 11.04 | +2.6 | -9.9 | -3.3 |
| C4 to C5 | pp | -42.7 | 104 | 676 | 1.82 | -1.2 | -29.5 | -15.6 |
| C4 to C5 | mf | -31.8 | 188 | 1039 | 2.71 | +2.9 | -23.3 | -6.4 |
| C4 to C5 | ff | -24.1 | 142 | 1926 | 5.20 | +3.2 | -11.3 | -3.2 |
| all | pp | -48.3 | 106 | 628 | 2.73 | -1.2 | -25.6 | -17.1 |
| all | mf | -34.5 | 122 | 982 | 4.89 | +2.3 | -25.4 | -7.6 |
| all | ff | -26.1 | 72 | 1839 | 9.47 | +2.7 | -10.9 | -3.9 |

Over the 33 notes recorded at all three dynamics, the medians of the steps are: level 13.1 dB from pp to mf and 7.8 dB from mf to ff; centroid 1.55 times from pp to mf and 1.85 times from mf to ff; share over 1 kHz 9.0 dB from pp to mf and 3.6 dB from mf to ff.

The shape agrees with Luce and Clark's averages for brass, as summarised by Fletcher and Tarnopolsky: rising 2 to 4 dB an octave under a cutoff near 1 kHz, falling 15 to 25 dB an octave over it, the fall flattening as the playing gets louder.

What the recordings can and cannot say:

- One player, one instrument of unstated make, one microphone at an unstated place; an anechoic room, so no reverberation in the releases.
- The levels are the recording's, not sound pressure. The collection uses one microphone and, as far as it says, one gain, so the differences between dynamics are meaningful and the absolute figures are not. A model's output is compared by its differences between dynamics and its spectral shape, not its level.
- The quiet scales are digitally silent between notes and the louder low scales have the room's noise about 20 to 30 dB under the notes; three onsets and four releases could not be measured and are left blank.
- Some releases of 6 to 30 ms in the low mf and ff scales look edited rather than played; releases are not a target.
- Scales, not music: single notes from silence, tongued, each about 1.5 to 2 s long.

Pass: for each note and dynamic in the table, the model's centroid over pitch within 25 per cent, its slope over 1 kHz within 4 dB an octave, its share over 1 kHz within 3 dB, and its steps between dynamics within 3 dB in level and within 25 per cent in centroid, with the dynamics' mouth pressures set by B4. Onsets within a factor of 1.5, once the player chooses attacks.

## B7. Joins

A lip slur on the horn takes 80 to 100 ms between steady states, regardless of interval or player, and the new oscillation builds only after a round trip of the bore; a starting transient takes about half as long (Logie et al. 2009). Nothing is published for the trombone; this is provisional. Upward and downward slurs differ (CFA 2010).

Pass: lip slurs between steady states within 60 to 120 ms; your ear for slide legato and glissando.

## B8. Cost

Measured as the bowed strings are, by `make stringbench`'s equivalent in a real-time run: the share of real time a module's process() takes and its worst block of 256 samples. Reference: a bowed string at Fidelity level 4 takes about 7 to 17 per cent on the bench machine.

Pass: no more than a bowed string at the same Fidelity level, and no block over 100 per cent of its length.

## B9. Your ear

The model's notes set beside the Iowa notes at the same pitch and dynamic, and beside the published demos of the commercial instruments, at a fixed level, one file at a time.
