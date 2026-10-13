# Bowed strings: the viola, sources

Gathered on 11 October 2026 for a viola design to sit beside `violin()` and `cello()` in src/BowedInstrument.cpp. Conventions follow body-sources.md: Q is 1 divided by twice the damping ratio; mobility is the driving-point admittance at the bridge in the bowing direction, in m/(s·N) or dB re 1 m/(s·N); radiativity is sound pressure per newton at the bridge, in Pa/N at 1 m.

Every value is tagged. MEASURED means someone measured it on a viola and the number is in the source as cited; the kind of source is stated each time (peer-reviewed paper, conference paper, manufacturer, retailer, amateur write-up, forum). READ means I took it off a published figure by tracing the figure's pixels, plus or minus about 1 dB and 1 to 2 percent in frequency. DERIVED means calculated from published values. JUDGEMENT means extrapolation or a choice; no viola measurement backs the number directly.

In short: for the strings, manufacturers publish what the model needs. For the body there is one open, measured viola bridge admittance (Maestre, Scavone and Smith 2013, one viola beside one violin on the same rig) and a handful of amateur and maker measurements of the air mode and the B1 modes. No viola damping, radiativity, upper-mode density or upper-mode damping was found in any open source. Sections 2 and 3 therefore start from the violin and scale.

## 1. Strings

### 1.1 Vibrating string length, MEASURED or published

| Source | Kind | Body length | Vibrating string length |
|---|---|---|---|
| Larsen Original viola information sheet, May 2023 [LAR] | manufacturer | not stated | 370 mm, the reference length for its tension figures |
| Thomastik Dominant 141, 4/4 [EMC] | retailer listing | 4/4 | 37.0 cm |
| Kaplan Amo long scale, D'Addario [HIC] | retailer listing | 16 to 16½ in | 15 in, 380 mm |
| Guettler, typical string properties [GUE-T] | table built with values from D'Addario and Corelli/Savarez | not stated | 37.5 cm |
| Pickering 1985 [PK85] | journal (CAS) | not stated | proposes 380 mm as the standard length for quoting viola string tensions |
| Johannsson, viola measurements [JOH-M] | maker's table | small 390 mm; medium 405 to 415 mm; large 420 to 430 mm | 37, 38 and 38 to 39, evidently centimetres; medium body stop 225 mm plus neck stop 150 mm |

For a 16 to 16.5 inch body (406 to 419 mm) the published lengths run from 370 to 380 mm.

### 1.2 Tensions, MEASURED by manufacturers or reported by retailers

| String | Dominant 141 medium [EMC], [EVS] | Larsen Original medium [LAR] | Larsen Original strong [LAR] | Guettler typical range [GUE-T] |
|---|---|---|---|---|
| A4, 440 Hz | 6.0 kgf | 8.2 kgf (steel core) | 8.4 kgf | 60.6 to 100.2 N |
| D4, 293.66 Hz | 5.0 kgf | 4.9 kgf | 5.6 kgf | 47.6 to 60.7 N |
| G3, 196.00 Hz | 4.8 kgf | 5.0 kgf | 5.3 kgf | 47.6 to 60.7 N |
| C3, 130.81 Hz | 4.9 kgf | 5.0 kgf | 5.2 kgf | 47.6 to 60.7 N |
| Set | 20.7 kgf | 23.1 kgf | 24.5 kgf | 203.4 to 282.3 N |

- Dominant: Thomastik's own site refused automated access (HTTP 403), so these figures are from two retailers, Easy Music Center and Electric Violin Shop, who agree string by string. Not first-party. Easy Music Center gives the scale as 37.0 cm.
- Larsen: first-party sheet, "Reference pitch frequency: 440 Hz. Free vibrating length: 370 mm". Core and windings: A solid steel core, stainless flat wound; D, G and C multi-filament nylon core, D aluminium wound, G and C silver wound. The A is offered soft at 7.7 kgf.
- Guettler: his lower three strings share one tension range, 47.6 to 60.7 N, and his linear densities equal that tension divided by (2Lf) squared, so the densities are derived from the tensions and are not an independent measurement.
- Evah Pirazzi: Pirastro's site refused automated access and no per-string figures were found elsewhere.

### 1.3 Wave impedance, DERIVED

Since the wave speed is c = 2 L f, the impedance Z = T / c = T / (2 L f), the same as the square root of T times the linear density. 1 kgf = 9.80665 N. L = 0.370 m, the length both manufacturers quote.

Dominant 141 medium:

- C3: T = 4.9 × 9.80665 = 48.05 N; c = 2 × 0.370 × 130.81 = 96.80 m/s; Z = 48.05 / 96.80 = 0.496 kg/s; linear density 5.13 g/m.
- G3: T = 47.07 N; c = 2 × 0.370 × 196.00 = 145.04 m/s; Z = 47.07 / 145.04 = 0.325 kg/s; 2.24 g/m.
- D4: T = 49.03 N; c = 2 × 0.370 × 293.66 = 217.31 m/s; Z = 49.03 / 217.31 = 0.226 kg/s; 1.04 g/m.
- A4: T = 58.84 N; c = 2 × 0.370 × 440.00 = 325.60 m/s; Z = 58.84 / 325.60 = 0.181 kg/s; 0.555 g/m.

Larsen Original medium: C3 49.03 / 96.80 = 0.507; G3 49.03 / 145.04 = 0.338; D4 48.05 / 217.31 = 0.221; A4 80.41 / 325.60 = 0.247 kg/s.

Larsen Original strong: C3 0.527; G3 0.358; D4 0.253; A4 0.253 kg/s.

The same string on a 375 mm length at the same pitch: tension rises by (375/370) squared and Z by 375/370, 1.4 percent. Dominant at 0.375 m: C3 0.503, G3 0.329, D4 0.229, A4 0.183 kg/s.

Checks: every Dominant and Larsen value lies inside Guettler's typical ranges (A4 0.184 to 0.304, D4 0.220 to 0.276, G3 0.324 to 0.413, C3 0.485 to 0.619 kg/s) except the Dominant A at 0.181, just under his lower bound. The violin's Dominant strings measured by Pickering (sources.md) are G3 0.344, D4 0.225, A4 0.194 kg/s: at the pitches the two instruments share, the viola's Dominant strings have about the same impedance as the violin's.

### 1.4 Direct measurements of viola string impedance or linear density

None found. Pickering 1985 measured violin strings only. Guettler's table, as noted, derives density from tension.

### 1.5 Recommendation

- Length: 0.375 m. JUDGEMENT, the middle of the published 370 to 380 mm for a 16 to 16.5 inch body.
- Strings, Dominant medium: C3 0.50, G3 0.33, D4 0.23, A4 0.18 kg/s. DERIVED from retailer-reported tensions; the 1.4 percent length difference is inside the rounding.
- Alternative, Larsen Original medium, first-party: C3 0.51, G3 0.34, D4 0.22, A4 0.25 kg/s. The steel-core Larsen A is a third heavier than the Dominant A.

## 2. Body: the signature modes

### 2.1 Looked for and not found

- Bissinger: no paper on the conventional viola was found. His octet work (Bissinger 2003 [B03]; 2005 and 2006 meeting abstracts [B05], [B06a]) covers the Hutchins and Schelleng instruments, whose alto is a 20-inch vertical viola, not a conventional one. His 1992 paper "Semiempirical relationships for A0 and A1 cavity mode frequencies for bowed string instruments of the violin, viola and cello family" (CAS Journal 2(1), 8 to 12) is not online; not read.
- Rossing, The Science of String Instruments (2010): there is no viola chapter. The table of contents was checked [SSI]: chapter 13 Violin (Curtin and Rossing), 14 Cello, 15 Double Bass, 16 Bows, Strings and Bowing (Guettler), 18 The Hutchins and Schelleng Violin Octet After 50 Years (Bissinger). Chapter 13 was not read for passing mentions of the viola.
- Curtin, Schleske, Jansson, Woodhouse: nothing on viola modes in their open writing. A search of the Euphonics site for "viola" returns no data; Jansson's book covers the violin and the guitar; strad3d.org hosts no viola papers.
- Gough 2021 [G21]: JASA paper whose abstract says it shows internal-cavity, radiated-sound and bridge-admittance measurements on a viola. Paywalled; not read.
- ObieAlto project: 25 violas built to one outline for the 2017 Oberlin workshop, with radiation measured by impact hammer and twelve microphones [HAM]. The ISMA 2019 paper (Fritz, Fraisse and Dubois) sits behind a bot check on HAL; the project page shows the curves only as images, with no numbers.
- Hutchins 1964 [H64] and Hutchins and Saunders 1971 [HS71] meeting abstracts on conventional violas: no numbers in the abstracts. Hutchins and Saunders state that a viola scaled in proportion to the violin would need a 21-inch body, against the 15 to 17.5 inches actually made.
- Peyton and Hoffman 2024 [PH24]: meeting abstract on viola top-plate modes against string tension; no numbers.
- The damping (Q) of any viola signature mode: none found.
- The radiativity of any viola mode: none found.

### 2.2 MEASURED frequencies, Hz

| Source | Kind | Instrument | A0 | CBR | B1− | B1+ | A1 |
|---|---|---|---|---|---|---|---|
| Maestre, Scavone and Smith 2013 [MSS13] | conference paper; impact hammer and laser vibrometer at the bridge; READ from Fig. 4 | one "decent quality" viola, McGill; body length not stated | 244 | not identified | 359, with a second peak at 386 | 448 | – |
| Coffey 2013 [JC13] | amateur write-up; loudspeaker, microphone inside the body, stepped sine to 1 Hz | 15½ in / 16¼ in / 16½ in / Tertis model | 244 / 220 / 228 / 208 | – | – | – | 390 (16½ in, ±5 Hz) |
| Powell 2012 [MP12] | student research report; piezo drive, pressure and velocity microphones | Eastman VA200, 16 in | about 220; 224 in the holography | – | – | – | – |
| Maestronet 2018 [MN18] | forum; makers' tap tests at the bridge; mode labels by D. Noon | 402 mm body | 231 | 321 | 374 / 385 | 453 | – |
| Maestronet 2018 [MN18] | as above | 41.6 cm body | 210 | – | 342 | not seen | – |
| Maestronet 2018 [MN18] | spectrum of a recorded Chaconne, not a tap; labels by "Emilg" | 40.7 cm body | 228 | 296 | 385 | 442 | – |
| Johannsson [JOH-A] | maker's web page, approximate | – | about 230 ("B flat") | – | about 350 ("F") | about 415 to 440 ("A flat to A") | 330 to 360 ("E to F sharp"), which he calls a main air resonance |
| Hutchins 1962 [H62], as quoted in [JC13] | magazine review | violas in general | the two main resonances lie 3 or 4 semitones above the two middle open strings: 233 to 247 Hz (DERIVED) | – | main wood 349 to 370 Hz (DERIVED) | – | – |

- Coffey's violin on the same rig: A0 271 Hz. Powell's other peaks, unlabelled: 328, 560, 1078 and 1504 Hz; at 328 Hz the back's two sides move in opposite phase.
- My labels for the Maestre peaks are JUDGEMENT, made by matching the pattern of the violin measured alongside it.

### 2.3 One viola beside one violin on the same rig, READ

Maestre, Scavone and Smith measured a violin, a viola and a cello the same way: each held by clamps at the neck, the violin and viola resting on packaging foam at the lower end, strings damped with rubber bands, hammer and laser vibrometer at the bridge's edge. They say the peaks below 100 Hz belong to the holding structure. Fig. 4 gives magnitude in dB without naming the reference; the violin's B1 peaks read −16.2 and −15.1 dB against −15.5 and −14.5 dB re 1 m/(s·N) for the violin in Woodhouse 2005, so the reference is very probably 1 m/(s·N). Bowing-direction (horizontal) curves, traced:

| Mode | Violin | Viola | Viola ÷ violin, frequency | Semitones lower | Peak level, viola minus violin | At equal Q, from peak height times bandwidth |
|---|---|---|---|---|---|---|
| A0 | 281 Hz, −25.7 dB | 244 Hz, −31.9 dB | 0.87 | 2.5 | −6.2 dB | −5.6 dB |
| B1− | 440 Hz, −16.2 dB | 359 Hz, −25.5 dB | 0.82 | 3.5 | −9.3 dB | −7.8 dB |
| B1+ | 509 Hz, −15.1 dB | 448 Hz, −22.7 dB | 0.88 | 2.2 | −7.6 dB | −6.2 dB |
| bridge hill | 2.20 kHz, −10.2 dB | 2.16 kHz, −14.3 dB | 0.98 | 0.3 | −4.1 dB | – |

- The figure cannot give Q. Read the same way, the violin's B1 modes come out at Q 12 to 19 against Bissinger's measured 41 to 43; the clamped mounting and the plot's resolution widen the peaks. Only the comparison between the two instruments is used here.
- The last column is DERIVED: for a single mode the peak mobility times 2π times the half-power bandwidth equals 1 divided by the effective mass, so this ratio is the mobility difference the two would show at the same Q.

Coffey 2013, amateur, swept sine at the bridge and one external microphone: with both f-holes open, a large viola's A0 reached 50 to 60 percent of the amplitude of its peak near 440 Hz (−6 to −4.4 dB); a violin's reached 90 to 110 percent (about 0 dB).

### 2.4 How far the viola's modes sit below the violin's, DERIVED and JUDGEMENT

Proposed frequencies, the medians of the measured values (JUDGEMENT, 16 to 16.5 inch violas):

| Mode | Viola | Range seen | Violin model | Ratio | Semitones lower |
|---|---|---|---|---|---|
| A0 | 228 Hz | 208 to 245 | 275 | 0.83 | 3.2 |
| CBR | 310 Hz | 296 to 328, weakly identified | 407 | 0.76 | 4.7 |
| B1− | 370 Hz | 342 to 386 | 475 | 0.78 | 4.3 |
| A1 | 390 Hz | one measurement; 1.71 × 228 = 390 by the violin's A1/A0 ratio | 469 | 0.83 | 3.2 |
| B1+ | 448 Hz | 442 to 453 (Johannsson 415 to 440) | 541 | 0.83 | 3.3 |

- In short, a viola's signature modes sit about 3 to 4.5 semitones (a factor of 0.76 to 0.83) below a violin's; on the one same-rig pair, 2.2 to 3.5 semitones. Its strings sit 7 semitones below the violin's, so relative to its own strings the viola's body is 3 to 4 semitones higher, as Hutchins said.
- Bissinger's account of Schelleng's octet scaling [B06a] puts A0 at 1.5 times and the B1 pair at 2.25 times the lowest open string. For a viola that is 196 and 294 Hz; conventional violas sit well above both.
- Flat-plate scaling check, JUDGEMENT: plate mode frequencies go as thickness over length squared. A 410 mm body against a 356 mm (14 inch) violin is 1.15 times longer, which at equal thickness gives 0.75 times the frequency, close to the CBR and B1− ratios. A0 falls less.

### 2.5 Damping, JUDGEMENT

Nothing measured. Use the violin's: A0 Q 20, B1− Q 41, B1+ Q 43, A1 Q 50 (Bissinger 2012's fourteen-violin averages), CBR Q 40 (the violin model's own unverified figure).

### 2.6 Mobility for the model, READ and JUDGEMENT

The violin model's peak mobilities, lowered by the same-rig differences at equal Q (section 2.3), so they go with the violin's Q values:

- A0: 0.022 lowered 5.6 dB, 0.012 m/(s·N).
- B1−: 0.17 lowered 7.8 dB, 0.07.
- B1+: 0.19 lowered 6.2 dB, 0.09.
- CBR: no viola reading; 0.08 lowered by the 6.5 dB average of the others, 0.04. JUDGEMENT.

As read, at the rig's own lower Q: A0 0.025, B1− 0.053, B1+ 0.073 m/(s·N).

In effective mass, the viola's B1 modes are about twice as heavy at the bridge as the violin's. Schelleng 1963 [S63] shows from dimensional scaling why the viola and cello are more prone to a wolf note than the violin.

### 2.7 Radiativity, JUDGEMENT

Nothing measured on any viola. Proposal: scale the violin model's radiativities by the same factor as the mobility, which assumes the same radiation per unit of bridge mobility. For a simple source, the viola's larger area (1.33 times, +2.5 dB) and lower frequency (0.8 times, −1.9 dB) roughly cancel.

- B1−: 1.212 lowered 7.8 dB, 0.49 Pa/N.
- B1+: 1.140 lowered 6.2 dB, 0.56.
- CBR: 0.121 lowered 6.5 dB, 0.06.
- A1: kept in the violin's proportion to the B1 modes, 0.17.
- A0: 0.444 lowered 5.6 dB is 0.23. Coffey's measurement puts a viola's A0 about 5 dB further below its main wood peak than a violin's, which gives 0.13. Suggested 0.13, to be settled by ear between 0.13 and 0.23.

## 3. Body: the upper modes

### 3.1 MEASURED, one viola beside one violin, READ

From the same Maestre, Scavone and Smith figure, bowing direction: the median of each band's top envelope.

| Band | Violin, dB | Viola, dB | Viola minus violin |
|---|---|---|---|
| 600 to 800 Hz | −35.9 | −37.1 | −1.2 |
| 800 to 1000 Hz | −27.0 | −30.1 | −3.1 |
| 1.0 to 1.4 kHz | −27.4 | −27.4 | 0 |
| 1.4 to 2.0 kHz | −22.9 | −25.2 | −2.3 |
| 2.0 to 2.8 kHz | −16.3 | −18.3 | −2.0 |
| 2.8 to 4.0 kHz | −23.9 | −28.5 | −4.6 |
| 4.0 to 5.6 kHz | −26.8 | −32.6 | −5.8 |
| 5.6 to 8.0 kHz | −30.5 | −36.9 | −6.4 |

- Bridge hill: the viola's at 2.16 kHz (−14.3 dB), the violin's at 2.20 kHz (−10.2 dB). In this pair the viola's hill sits where the violin's does, about 4 dB lower.
- From 0.6 to 2.8 kHz the viola sits 0 to 3 dB below the violin; above 2.8 kHz, 4.5 to 6.5 dB below, so its response falls away faster above the hill.
- Caveats: one instrument of each. The authors discarded data above 6 kHz because of measurement artefacts. Coherence on the viola was near 1 from about 30 Hz to 8 kHz.
- Viola bridge, maker's table [JOH-M]: height 38 to 40 mm, feet 45 to 47 mm wide. No viola bridge rocking frequency was found; no viola bridge appears in Rodgers and Masino 1990.

### 3.2 Not found

A viola's radiativity curve at any frequency; its modal density; the damping of its upper modes; its critical frequency; its bridge rocking frequency.

### 3.3 Setting the upper modes, JUDGEMENT

- Where they start: 500 Hz, just above B1+ at 448, as the violin's start at 630 Hz sits above its B1+ at 541.
- Density: 0.025 modes per hertz, against the violin model's 0.02. A plate's modal density goes as its area over its thickness; the viola's plates have 1.33 times the area, and Johannsson's medium-viola thicknesses (top 2.4 to 3.2 mm, back 2.3 to 4.5 mm, edges excluded) are close to a violin's, so 1.2 to 1.33 times the violin's.
- Loss: 0.02 (Q 50), as the violin.
- Critical frequency: it depends on plate thickness and wood, not on area, so about the violin's 3.9 kHz.
- Radiativity: the violin's Bissinger curve at its own frequencies (the hill does not move), lowered by the band differences in 3.1, about 2 dB up to 2.8 kHz, 4.6 dB to 4 kHz, 5.8 dB to 5.6 kHz, 6.4 dB above. In the model's terms: 500 and 625 Hz −12.5, 875 −9.1, 1125 −8.6, 1375 −9.3, 1625 −8.0, 1875 −5.9, 2125 −4.6, 2375 −3.0, 2625 −4.6, 2875 −7.8, 3125 −9.4, 3375 −8.9, 3625 −8.9, 3875 −9.4, 7750 −19.7, 10000 −22.8 dB re 1 Pa/N at 1 m.
- Dense mobility: the violin's curve lowered the same way: 500 −26, 1000 −26, 1500 −22, 2400 −18, 3000 −23, 5000 −36, 10000 −47 dB re 1 m/(s·N).
- No separate bridge filter, as for the violin: the hill is already in the curve.

## 4. Playing

### 4.1 MEASURED

- Vibrato, MacLeod 2008 [M08], forty high school and university violin and viola players. Only the abstract was read; the open copy at UNC Greensboro did not respond. Violists vibrate slightly slower and narrower than violinists; the abstract gives no figure for that difference. High pitches 0.32 Hz faster and about 26 cents wider than low pitches; forte about 4 cents wider than piano.
- Fletcher, Blackham and Geertsen 1965 [FBG65] found vibrato in both frequency and intensity in viola tones; numbers not read.
- Bowing, Schoonderwaldt 2009 [S09]: bow velocity, bow force and bow-bridge distance measured with motion capture and a force sensor, on violin and viola, détaché notes of 0.2, 2 and 4 s at pp, mf and f on all four strings. Schoonderwaldt 2010 [S10] identifies three of the sessions as viola sessions. The JASA paper is paywalled and the KTH thesis sits behind a bot check, so no viola figures were read. The abstract says the players adapted the bowing parameters to the physical properties of the string and the instrument. [S10] is open but gives no viola means; it notes that the force sensor shortened the usable hair by about 1 cm.
- Bow hair length, viola, two museum bows: Edinburgh MIMEd 0336, "Bow for viola", possibly 19th century, hair 631 mm (measured from the ferrule, under tension), stick 704 mm, 59 g [ED]; Birmingham Conservatoire 10.17, John Dodd (1752 to 1839), overall 725 mm, hair 632 mm, 63 g [BCU]. Violin, for comparison: a normal violin bow is about 740 to 750 mm, with hair of 650 mm [GUE-B].
- Not found: a modern viola bow's hair length; any viola bow force, bow speed or bow-bridge distance figure; violists' absolute vibrato rate and width; vibrato onset.

### 4.2 JUDGEMENT

- Hair to draw: 0.63 m, from the two measured viola bows, against the violin's 0.65.
- Bow position as a fraction of the string: as the violin. Askenfelt's piano at 42 mm on a 328 mm string becomes 48 mm on 375 mm; his forte at 18 to 20 mm becomes 21 to 23 mm.
- Force in multiples of the string's impedance and bow speeds of 0.15 to 0.4 m/s: as the violin. Bow-force limits scale with impedance, so expressing force in impedances carries over to the heavier strings.
- Vibrato: between the violin's 5.8 Hz and 20 cents and the cello's 5.5 Hz and 15 cents, for example 5.7 Hz and 18 cents. Only the direction is measured, not the size.

## 5. Recommended set

| Element | Value | Tag and source |
|---|---|---|
| String length | 0.375 m (0.370 to 0.380) | published lengths [LAR], [EMC], [HIC], [GUE-T], [PK85]; the choice is JUDGEMENT |
| C3 130.81 Hz | 0.50 kg/s | DERIVED from Dominant tension [EMC], [EVS] |
| G3 196.00 Hz | 0.33 kg/s | DERIVED, as above |
| D4 293.66 Hz | 0.23 kg/s | DERIVED, as above |
| A4 440.00 Hz | 0.18 kg/s (Larsen steel A 0.25) | DERIVED, as above; Larsen [LAR] |
| A0 | 228 Hz, Q 20, mobility 0.012, radiativity 0.13 (to 0.23) | frequency MEASURED, median of [MSS13], [JC13], [MP12], [MN18]; Q JUDGEMENT; mobility READ and scaled [MSS13]; radiativity JUDGEMENT |
| CBR | 310 Hz, Q 40, mobility 0.04, radiativity 0.06 | frequency MEASURED, forum only [MN18]; the rest JUDGEMENT |
| B1− | 370 Hz, Q 41, mobility 0.07, radiativity 0.49 | frequency MEASURED [MSS13], [MN18], [JOH-A]; mobility READ and scaled; Q and radiativity JUDGEMENT |
| A1 | 390 Hz, Q 50, mobility 0, radiativity 0.17 | frequency MEASURED, one viola [JC13]; the rest JUDGEMENT |
| B1+ | 448 Hz, Q 43, mobility 0.09, radiativity 0.56 | frequency MEASURED [MSS13], [MN18]; mobility READ and scaled; Q and radiativity JUDGEMENT |
| Upper modes | from 500 Hz, 0.025 per Hz, loss 0.02 | JUDGEMENT, plate scaling from the violin |
| Upper radiativity and mobility | the violin's curves lowered about 2 dB to 2.8 kHz and 4.6 to 6.4 dB above | shape READ from one viola–violin pair [MSS13]; applying it is JUDGEMENT |
| Bridge hill | about 2.2 kHz, inside the curve as for the violin | MEASURED, one viola [MSS13] |
| Hair to draw | 0.63 m | MEASURED on two historical viola bows [ED], [BCU] |
| Vibrato | about 5.7 Hz, 18 cents | JUDGEMENT, direction from [M08] |

## References

- **[B03]** Bissinger G (2003). Modal analysis of a violin octet. J. Acoust. Soc. Am. 113, 2105–2113. doi 10.1121/1.1555614. Abstract only.
- **[B05]** Bissinger G (2005). The violin octet and bowed string instrument design. J. Acoust. Soc. Am. 117, 2589 (meeting abstract). doi 10.1121/1.4777236.
- **[B06a]** Bissinger G (2006). Violin octet scaling revisited. J. Acoust. Soc. Am. 119, 3258 (meeting abstract). doi 10.1121/1.4786088.
- **[BCU]** Birmingham Conservatoire historic instrument collection, 10.17, viola bow by John Dodd. https://www.bcu.ac.uk/conservatoire/research/hic/the-collection/string-bow/10-17
- **[ED]** University of Edinburgh, Musical Instrument Museums Edinburgh, 0336, Bow for viola. https://collections.ed.ac.uk/mimed/record/17343
- **[EMC]** Easy Music Center, Thomastik 141 Dominant viola set, product listing (retailer). https://easymusiccenter.com/products/thomastik-141-4-4
- **[EVS]** Electric Violin Shop, Thomastik Dominant 141 viola string set, medium, product listing (retailer). https://electricviolinshop.com/apps/aeo/ai/md/default/products/thomastik-dominant-141-viola-string-set-medium-tension-with-ball-end.md
- **[FBG65]** Fletcher H, Blackham ED, Geertsen ON (1965). Quality of violin, viola, 'cello, and bass-viol tones. I. J. Acoust. Soc. Am. 37, 851–863. doi 10.1121/1.1909462. Abstract only.
- **[G21]** Gough C (2021). Acoustic characterisation of string instruments by internal cavity measurements. J. Acoust. Soc. Am. 150, 1922–1933. doi 10.1121/10.0006205. Abstract only.
- **[GUE-B]** Guettler K (undated). Some physical properties of the modern violin bow. Manuscript. https://knutsacoustics.com/files/some-bow-properties-cism.pdf
- **[GUE-T]** Guettler K (undated). Some typical properties of bowed strings (table; values contributed by Fan Tao of D'Addario and Michel Simane of Corelli/Savarez). https://knutsacoustics.com/files/Typical-string-properties.pdf
- **[H62]** Hutchins CM (1962). The physics of violins. Scientific American 207(5), 78–93. doi 10.1038/scientificamerican1162-78. Not read directly; quoted in [JC13].
- **[H64]** Hutchins CM (1964). Comparison of the acoustical and constructional parameters of the conventional 16 to 17-in. viola and the new 20-in. vertical viola. J. Acoust. Soc. Am. 36, 1025 (meeting abstract). doi 10.1121/1.2143245.
- **[HAM]** Hampel und Riebesehl Geigenbau, The Obialto project (web page). https://hampel-geigenbau.de/en/plus/obialto
- **[HIC]** Hickeys Music, Kaplan Amo KA414LM viola C string, product listing (retailer). https://www.hickeys.com/music/strings/viola/accessories/strings/long_scale_viola_singles/products/sku116625-kaplan-amo-ka414lm-viola-c-stringtungstensilver-wound-.php
- **[HS71]** Hutchins CM, Saunders FA (1971). Variations in tonal characteristics and construction of the viola. J. Acoust. Soc. Am. 50, 128–129 (meeting abstract). doi 10.1121/1.1977579.
- **[JC13]** Coffey J (2013). The air cavity, f-holes and Helmholtz resonance of a violin or viola. Self-published. https://www.lisafea.com/pdf/JMC-Helmholtz_Resonance--Re-ct2.pdf
- **[JOH-A]** Johannsson H. Acoustics (maker's web page). https://hansjohannsson.com/rapidweaver%20export/rapidweaver%20export/ac.php
- **[JOH-M]** Johannsson H. Viola measurements in mm (maker's web page). https://hansjohannsson.com/resources/mesviola.html
- **[LAR]** Larsen Strings A/S (2023). Larsen Strings for Viola: Larsen Original. Information sheet, revision May 2023. https://larsenstrings.com/wp-content/uploads/2023/08/larsen-original-viola-2023.pdf
- **[M08]** MacLeod RB (2008). Influences of dynamic level and pitch register on the vibrato rates and widths of violin and viola players. J. Res. Music Educ. 56(1), 43–54. doi 10.1177/0022429408323070. Abstract only; open copy listed at http://libres.uncg.edu/ir/uncg/f/R_MacLeod_Influences_2008.pdf, unreachable on 11 October 2026.
- **[MN18]** Maestronet Forums, The Pegbox, "Frequency analysis violas", 23 to 27 December 2018. https://maestronet.com/forum/index.php?%2Ftopic%2F341662-frequency-analysis-violas%2F=
- **[MP12]** Powell M (2012). Acoustic analysis of the viola. NSF REU report, Department of Physics, University of Illinois (advisor S. Errede). https://courses.physics.illinois.edu/phys406/sp2017/NSF_REU_Reports/2012_reu/Meredith_Powell/Meredith_Powell_Final_Paper.pdf
- **[MSS13]** Maestre E, Scavone GP, Smith JO (2013). Digital modeling of bridge driving-point admittances from measurements on violin-family instruments. Proc. Stockholm Music Acoustics Conference 2013, 101–108. https://mtg.upf.edu/system/files/publications/smac_adm_2013_v3.pdf, also http://hdl.handle.net/10230/23667
- **[PH24]** Peyton CP, Hoffman KR (2024). The dependence of viola top plate mode frequencies and shape on string tension. J. Acoust. Soc. Am. 155, A60 (meeting abstract). doi 10.1121/10.0026796.
- **[PK85]** Pickering NC (1985). Physical properties of violin strings. J. Catgut Acoust. Soc. 44, 6–8. https://knutsacoustics.com/files/pickering-violin-string-properties.pdf
- **[S09]** Schoonderwaldt E (2009). The player and the bowed string: coordination of bowing parameters in violin and viola performance. J. Acoust. Soc. Am. 126(5), 2709–2720. doi 10.1121/1.3203209. Abstract only.
- **[S10]** Schoonderwaldt E (2010). On the use of skewness in violin bowing: should the bow be straight or not? Acta Acustica united with Acustica 96(4), 593–602. doi 10.3813/AAA.918314.
- **[S63]** Schelleng JC (1963). The violin as a circuit. J. Acoust. Soc. Am. 35, 326–338. doi 10.1121/1.1918462. Abstract only.
- **[SSI]** Rossing TD, ed. (2010). The Science of String Instruments. Springer. doi 10.1007/978-1-4419-7110-4. Table of contents: https://digitale-objekte.hbz-nrw.de/storage/2012/12/06/file_124/4838858.pdf

Violin and cello sources cited in passing (Bissinger 2012, Woodhouse 2005, Askenfelt 1989, Pickering's violin Dominant set) are listed in body-sources.md and sources.md.
