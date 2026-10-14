# French horn: literature survey for a real-time physical model

Scope: the modern double horn in F and B-flat, with notes on the single horn and the descant horn. This is survey (4) of five. The general physics of brass, the trumpet and flugelhorn, the trombone, and the player model are covered in the parallel surveys, so this file concentrates on what is specific to the horn.

How to read the source notes: every claim carries a citation. "Verified" means the text or abstract was read during this survey. "Unverified" means the item is from memory or from a secondary citation that could not be checked. The web search budget for this session ran out before the survey was complete, so some leads could not be followed; they are listed under Open questions.

## 1. Bore geometry

### 1.1 Overall length and layout

The F side of a double horn is about 3.7 m of tubing. Wachter measured an Engelbert Schmid F/B-flat double horn and used an F-side length of 3715.4 mm in his simulations; he states that the B-flat side is about 1 m shorter and pitched a fourth higher (Wachter 2008, verified). Pätynen and Lokki give about 3.75 m for the F horn, citing Rossing (Pätynen and Lokki 2010, verified). Campbell, Gilbert and Myers describe the F horn as having a sounding length of about 4 m (Campbell, Gilbert and Myers 2021, Acoustics Today, verified).

Nominal resonance series, from the UNSW horn page: on the B-flat side the lowest playable resonance is B-flat 2 with a pedal of B-flat 1; on the F side the lowest playable resonance is F2 with a pedal of F1 (UNSW Music Acoustics, brass acoustics page, verified). The pedal of the F side is F1, about 43.7 Hz, and of the B-flat side B-flat 1, about 58.3 Hz (equal temperament, A4 = 440 Hz; arithmetic, not a measurement).

Valves: the horn uses rotary valves. On a full double horn, each of the three finger keys operates two valves, one in the F branch and one in the B-flat branch, lowering the pitch by about 2, 1 and 3 semitones; a fourth valve, worked by the left thumb, is the change valve that switches the air column between the F and B-flat branches (UNSW brass acoustics page and UNSW timpani-horn page, verified). A compensating double horn has full-length valve slides only on the B-flat side and routes the F side through additional short slides (Wikipedia, French horn, secondary). A single horn has three valves and is pitched in F, less often in B-flat; a descant double typically combines B-flat and high F; a triple horn adds a high F (or E-flat) branch an octave above the F horn, selected by a second thumb valve (Wikipedia, French horn, citing Ericson; secondary). The high F branch is therefore about half the F-side length, roughly 1.85 m (arithmetic from the F length).

Bore character: sources describe the proportions differently. The UNSW page describes the horn, like the trumpet and trombone, as having relatively long lengths of narrow cylindrical pipe and a relatively short flared section (verified). Pätynen and Lokki, citing Fletcher and Rossing, describe the horn as having a small cylindrical portion (verified). The widely repeated description of the horn as a long, narrow, mostly conical tube with a cylindrical valve section, a conical leadpipe and a long flaring bell branch is consistent with both, but no primary bore measurement of a modern double horn giving the proportions of cylinder, cone and flare was found in this survey (unverified).

Bore diameter: Wikipedia gives 10.8 to 11.0 mm for the French-type horn, citing Baines 1976 (secondary). Manufacturer figures for modern double horns of about 11.5 to 12 mm (for example 0.468 inch, about 11.9 mm, for the Conn 8D) are from memory (unverified).

Bell: Wachter's detachable bell has an entry (throat) diameter of 65.3 mm at the joint (Wachter 2008, verified). The bell mouth diameter of a modern horn is commonly about 30 to 31 cm (unverified; manufacturer figures not checked). A historic horn in the Edinburgh collection (Julius Altrichter, 1883 to 1885, measured by Arnold Myers) is listed with "bell 164", units unstated, which is not a modern orchestral bell (MIMEd record 119631, verified).

### 1.2 Mouthpiece

The horn mouthpiece is deep and funnel-shaped rather than bowl-shaped. Pyle describes a deep funnel-shaped (horn-like) mouthpiece as encouraging a less steep waveform than a shallow bowl-shaped (trumpet-like) one, which reduces nonlinear spectral enrichment (Pyle 2010, Vienna Talk, verified). The UNSW page states that the enclosed volume of a horn mouthpiece may be approximately conical, and that many players place the mouthpiece asymmetrically, with more of the upper lip inside the rim (verified). Hirano and Kinoshita refer to the horn's thin-rimmed mouthpieces (Hirano and Kinoshita 2026, abstract verified).

One measured horn mouthpiece in the Edinburgh collection (MIMEd accession 0866, 20th century, ex A. Myers): overall length 62.2, cup diameter 16.3, minimum (throat) diameter 4.0, cup depth 24, shank taper from 8.5 to 7.3 over 30.0, cup volume 2527; units are not stated on the record but are consistent with millimetres and cubic millimetres (MIMEd record 17000, verified). Wachter used a mouthpiece rim diameter of 17.5 mm in a simplified cone model (Wachter 2008, verified). Logie et al. built a transparent horn mouthpiece with the shank, cup volume and rim of a Paxman 4C (Logie et al. 2009, verified). Bilbao and Chick used a Paxman-Halstead-Chidell (PHC 22) mouthpiece on a natural horn (Bilbao and Chick 2013, verified).

### 1.3 Leadpipe, valves and change valve

No horn-specific primary measurements of leadpipe taper, rotary valve geometry or change-valve geometry were found. Widholm studied the effect of valve mechanism on the microstructure of slurs (Widholm 1997, Proc. Institute of Acoustics 19(5), 407 to 412, cited by Logie et al. 2009; unverified). The NESS brass environment models valves as time-varying junctions between a main tube and bypass tubes and includes a horn with three valves among its test instruments (Harrison et al. 2015, verified). Bilbao's valve model is in "Modelling of brass instrument valves", DAFx 2011 (title seen in the DAFx archive listing; content not read).

## 2. Playing high in the harmonic series

### 2.1 Why the horn sits high

The horn is played mostly on resonances 2 to 16 of the F side, an octave higher in mode number than the other brass, which mostly use resonances up to about 8. Watts summarises Backus to this effect (Watts 2009, citing Backus 1977, The Acoustical Foundations of Music, verified as a citation in Watts). Because the F-side resonances are close to a harmonic series on about 43.7 Hz, the interval between neighbouring resonances shrinks as the mode number rises: between resonances 8 and 9 it is 204 cents, between 12 and 13 it is 139 cents, and between 16 and 17 it is 105 cents (arithmetic from a harmonic series). Written c'' to c''' on the F horn (sounding F4 to F5, about 349 to 698 Hz) corresponds to resonances 8 to 16.

Dell et al. note that horn players do not generally play above about 700 Hz (sounding F5) but are often asked to play between 450 and 550 Hz (A4 to D5) (Dell et al. 2010, verified).

### 2.2 Measured input impedance

Dell, James, Davidson and Wolfe measured a Yamaha 668 double horn with a Marcinkiewicz No. 7 mouthpiece using the UNSW three-microphone, two-calibration method, from 0 to 1000 Hz, on both F and B-flat sides, with three gelatine casts of players' hands, four practice mutes and representative fingerings. Peak magnitudes on the open F and B-flat sides reach about 50 to 55 MPa s per cubic metre. Repeat measurements agreed within 0.1 dB and 0.4 Hz rms over peaks 1 to 20 (Dell et al. 2010, verified, open access).

The UNSW group's own horn impedance spectra (B-flat horn with and without the hand, and F horn) were measured by Jer-Ming Chen and are shown as figures on the UNSW brass acoustics page; they come from Chen's 2009 PhD thesis (UNSW brass acoustics page, verified; thesis not read). The page notes that the horn's overall impedance is higher than the trombone's because of its narrower bore and the mouthpiece constriction.

Watts measured a Conn 8D double horn from about 30 Hz to 1 kHz, open F and open B-flat, with and without a synthetic hand, and tabulated peak frequencies against equal temperament (Watts 2009, Phys498POM report, verified; the table is an image and was not transcribed).

Wachter measured the Schmid double horn with BIAS for all valve combinations (open, 1, 2, 3, 12, 13, 23, 123), F and B-flat sides, with hand, without hand, without the bell, with a stopping mute and with a wooden mute; the plots are in his appendix. He reports impedance peaks up to about 60 megohm (acoustic) and notes that the B-flat side has fewer and taller peaks than the F side because of its smaller ratio of tube length to bell diameter (Wachter 2008, verified, open access, German).

Bilbao and Chick give numeric peak and trough frequencies for a natural horn (Meinl and Lauber/Paxman copy of a Huschauer horn, Vienna about 1770, crooked in D, about 4.51 m), measured with BIAS at 20 degrees C. First seven maxima (measured, Hz): 23.2, 64.8, 103.7, 144.7, 181.9, 220.3, 256.2. Minima: 41.1, 78.2, 119.1, 159.2, 197.9, 235.8, 271.8 (Bilbao and Chick 2013, Table I, verified from the authors' manuscript).

Backus 1976 measured input impedance curves for the brass instruments, including the horn with a plasticene lump standing in for the hand and a rubber stopper for the stopped horn (Backus 1976, cited by Dell and by Campbell et al.; Watts reproduces his horn figure; original not read).

### 2.3 Accuracy, cracked and split notes

The UNSW page states that the B-flat side is often preferred in the high range because its peaks are further apart, which makes it less likely that the player selects the wrong peak or cracks a note by involving two neighbouring peaks (verified). Campbell, Gilbert and Myers, citing Yoshikawa and Nobara 2017, state that the stronger high-frequency resonances produced by the hand extend the range of pitches that can be sounded securely and reduce the risk of cracking or mis-pitching a high note (Acoustics Today 2021, verified). Logie et al. measured lip-slur transients on two horns with two players: the lips take 80 to 100 ms to move from one stable regime to another, during which lip oscillation of reduced amplitude passes through intermediate frequencies, some near and some not near intermediate resonances; one player's sound did not break, the other's nearly did (Logie et al. 2009, verified). Chen, Smith and Wolfe showed that timpani strokes near the bell can disturb the amplitude, periodicity and frequency of the pressure at the lips, because the horn transmits sound from bell to mouthpiece with gains of at least 20 dB near playing resonances, up to about 26 dB when the timpani is tuned near a horn resonance (Chen, Smith and Wolfe 2014, abstract verified). An earlier paper on horn intonation, Chick, Lumb and Campbell 2004 (ISMA Nara, pp. 9 to 12), is cited by Dell et al. but was not located (unverified).

## 3. The hand in the bell

### 3.1 Normal hand position: effect on resonances

Without the hand, impedance peaks above 450 Hz are greatly reduced and disappear entirely above 750 Hz (Dell et al. 2010, summarising Backus 1976, Benade 1976 and Widholm 1988; verified as a statement in Dell). With the hand, the high peaks are taller and the cutoff region moves up; the differences grow near and above the bell cutoff, which Dell et al. put at around 500 Hz. Below that, reflection occurs in narrower parts of the bore before the wave reaches the hand, so the hand has little effect (Dell et al. 2010, verified).

Different hands: three casts of different players' hands produced reproducible differences of up to 0.6 to 0.8 dB and 0.6 to 1.3 Hz rms over peaks 1 to 20. On the open B-flat horn, amplitude differences between hands appear above the third resonance (175 Hz) and are larger above the eighth (460 Hz). For written C5 and above, the peaks lie flat of equal temperament for all hands; the hand whose peaks were flattest was also judged flattest by the seven professional players (Dell et al. 2010, verified).

Pitch: the hand lowers the resonance frequencies, most strongly at high mode numbers. Watts found that the hand lowered almost every resonance, most on the B-flat side, that the 11th resonance moved by nearly a semitone toward a diatonic pitch, and that the peak-to-trough height of almost every B-flat-side resonance increased (Watts 2009, verified). Wachter found the same direction on the F side and treats the hand as reducing the open area of the bell, analogous to a smaller tone hole, which lengthens the effective air column (Wachter 2008, verified). Ebihara and Yoshikawa report that the hand lowers the peaks above the 6th by about 20 cents and raises their magnitudes by about 6 megohm above the cutoff of about 500 Hz (Ebihara and Yoshikawa 2013; this figure was seen in a search snippet of the paper and was not checked in the full text).

Practice mutes, by comparison, lower the low resonances, raise the high ones, and shift frequencies so that the series becomes less harmonic; one mute made resonances 6 to 17 flatter than the others (Dell et al. 2010, verified).

### 3.2 Effect on the radiated sound

The hand increases reflection at high frequencies, so less high-frequency energy is radiated and the sound is darker and less loud; bells-up playing gives a loud, bright sound (UNSW brass acoustics page, verified). Campbell, Gilbert and Myers describe the hand as an internal mute that increases the fraction of sound energy reflected back into the tube, an effect that grows with frequency (Acoustics Today 2021, verified). Pätynen and Lokki attribute a change in the vertical radiation pattern between 1.25 and 4 kHz to the hand in the bell (see section 4).

### 3.3 Hand-stopping

Pedagogy and the long dispute: horn players are taught to finger a semitone lower when fully stopping on the F horn. Whether stopping raises or lowers the pitch has been argued for about 250 years.

Measurements and models:

- Pyle (1964) modelled stopping as truncating the horn near the hand and terminating it with a lumped resonator, computed the natural frequencies of the coupled system, and concluded that both the "rising" and "falling" views are partly justified depending on whether the resonator is counted as part of the horn (Pyle 1964, JASA supplement abstract; author and venue verified through Crossref, content from a search-result summary).
- Backus (1968) found, with a mute that behaves like hand-stopping, that each resonance is pulled down to about a semitone above the unstopped frequency of the next lower resonance (Backus 1968, JASA supplement abstract; author and venue verified through Crossref, content from a search-result summary).
- Wachter (2008) measured the Schmid horn with a stopping mute. Stopped F-horn resonance maxima, open fingering (Hz): 25, 48, 89, 138, 182, 225, 270, 312, 355, 400, 442, 489, 530, 580. A new low peak appears at about 55 Hz (it grows out of the open horn's second resonance at about 85 Hz as the mute is inserted), and the stopping mute alone has a single resonance at 106 Hz. Withdrawing the mute in 1 mm steps moved all peaks continuously, with no jump; the played note flips between open and stopped behaviour between 2 and 3 mm of withdrawal. He concludes that the resonances move down, and the perceived upward jump happens because the lips cannot sustain the weakened resonance and move to the next one up. On the B-flat side stopped notes lie between a semitone and a whole tone from the open note and cannot be corrected by transposition, so players stop on the F side. A simplified model reproduced the stopped impedance: the horn without its bell, terminated by a cylinder 30 mm long and 5 mm in diameter, matched the stopped peaks within 3 Hz up to the 14th maximum. He also states that the pitch jumps when the hand closes about 95 percent of the bell opening (Wachter 2008, verified).
- Watts (2009) measured a real player's hand in situ on a horn. Stopping attenuated the second resonance and lowered all peaks; the shift was more than 100 cents in the low range, about 100 cents in the middle range and small in the upper range (Watts 2009, REU thesis "Spectral Analysis of the French Horn and the Hand-in-Bell Effect", verified).
- Pyle (2024) measured the acoustic round-trip time with an earphone and microphone in the mouthpiece. As the hand closes, the round-trip time increases (pitch falls); near full closure an earlier reflection appears and dominates (pitch rises). Both views can be considered correct (Pyle 2024, JASA supplement abstract, verified through Crossref).
- Campbell, Gilbert and Myers describe the hand in the stopped position as an internal transposing mute that gives the F horn a new set of natural notes a semitone above the open set (Acoustics Today 2021, verified).

The stopped timbre ("brassy", "metallic", "penetrating"): Ebihara and Yoshikawa first concluded, in a non-refereed 2011 report, that wall vibration produced the stopped tone (Ebihara and Yoshikawa 2011, IPSJ SIG report, abstract verified). Their 2013 JASA paper revised this: calculations of the stopped-horn transmission function and measurements of radiated pressure and bell wall vibration show that the metallic quality comes from harmonics extending above 10 kHz, produced by a rapidly corrugating waveform; excitation and immobilisation experiments did not support a link between wall vibration and radiation; mouthpiece pressure measurements indicate that the corrugations form through nonlinear propagation along the bore (Ebihara and Yoshikawa 2013, abstract verified via Europe PMC). In that paper the hand is modelled as a thin plate with an orifice at the hand position, the orifice area equal to the gap between hand and bell wall, and this reproduces Dell et al.'s measurements (search snippet, full text not read). A companion paper, "Nonlinear wall vibration and wave steepening contributing to tonal metallicness and brassiness in a horn", appeared in Proceedings of Meetings on Acoustics in 2013 (Crossref record verified; not read).

Half-stopping and echo horn: a partially closed hand lowers the pitch by up to about a semitone with a muted sound; no quantitative measurements of intermediate hand positions other than Wachter's mute-withdrawal series (2008) and Pyle's round-trip study (2024) were found. Yoshikawa and Nobara (2017), "Acoustical modeling of mutes for brass instruments", in Schneider (ed.), Studies in Musical Acoustics and Psychoacoustics, Springer, pp. 143 to 186, is cited for the hand's effect on high resonances and is the most likely single source for a hand and mute model (reference verified in Acoustics Today; chapter not read).

## 4. Radiation

Direction: the bell points backward and to the player's right, close to the player's body, with the hand in the bell. Pätynen and Lokki recorded a double horn in an anechoic room with 22 microphones at about 2 m, tones A2 to A4. Radiation is close to omnidirectional below about 250 Hz in the lateral plane and up to 500 Hz in the median plane, with a bias to the player's right below 1 kHz; above 500 Hz the bell direction dominates; above 1 kHz radiation is confined to the bell direction. The strongest direction up to 1 kHz and above 4 kHz is to the far right at low elevation; between 1.25 and 4 kHz the maximum moves to mid elevation, which the authors attribute to the hand reducing radiation to higher elevations. The played note does not change the pattern above 630 Hz, and dynamics do not change directivity (Pätynen and Lokki 2010, verified, open access). Their earlier recording documentation gives the horn's main radiation direction as their microphone 8, azimuth 144 degrees, elevation 11 degrees (Aalto anechoic recordings page, via search summary, unverified). Comparison data: Meyer, Acoustics and the Performance of Music; Otondo and Rindel 2004 (Acta Acustica united with Acustica 90, 1178 to 1184); Martin 1942 (JASA 13, 309 to 313) (all cited by Pätynen and Lokki; not read).

Spectral slope: above 800 Hz, the average power spectrum of the horn falls at about 21 dB per octave in piano, 13 in forte and 9 in fortissimo; the 12 dB per octave difference between piano and fortissimo was the largest of the brass recorded. Luce and Clark gave a roll-off of 15 dB per octave for the horn (Pätynen and Lokki 2010, citing Luce and Clark 1967, JASA 42, 1232 to 1243; verified as cited).

What the listener hears: because the bell faces away from the audience, the heard sound in a hall depends on reflections from surfaces behind and beside the player. No primary study quantifying this for the horn was read in this survey (unverified). The bell also works in reverse as an impedance-matching receiver (Chen, Smith and Wolfe 2014).

## 5. Brassiness and nonlinear propagation (cuivré)

At high levels the horn's sound brightens strongly because of nonlinear wave steepening in the bore, the same mechanism as in the trumpet and trombone. Horn players call the very loud, rough sound cuivré. The relevant measures:

- Brassiness potential B: a bore-geometry parameter between 0 and 1 that measures how much a bore's flare reduces nonlinear steepening relative to a cylinder of the same effective length; it was proposed by Pyle and introduced by Gilbert, Campbell, Myers and Pyle at ISMA 2007; the 2012 JASA paper validated it against spectral centroid in crescendos and showed a trade-off between bore size and B (Gilbert et al. 2007, HAL hal-00475561; Myers et al. 2012; verified at abstract level). The value of B for a modern double horn was not found in accessible text.
- Player control: Norman et al. 2010 studied player control of brassiness at intermediate dynamic levels across brass families (Acta Acustica united with Acustica 96(4), 614 to 621; citation verified, content not read). Logie 2013 compared viscothermal losses and nonlinear effects in cylindrical tubes of different diameters against a weakly nonlinear model (thesis abstract verified).
- Mouthpiece: the horn's deep funnel cup gives a less steep mouthpiece waveform and therefore less enrichment than a bowl cup at the same level (Pyle 2010, verified).
- Stopped horn: the metallic stopped sound is attributed to nonlinear propagation producing harmonics above 10 kHz (Ebihara and Yoshikawa 2013, verified at abstract level).
- Measured enrichment: the 12 dB per octave change in slope from piano to fortissimo (Pätynen and Lokki 2010, verified).

## 6. Lip parameters and playing behaviour measured on horn players

- Lip motion direction: in the horn and trumpet, the lips behave as an outward-striking valve in the lowest mode and as an upward-striking (sliding) valve in higher modes; the author links the change to the growth of Bernoulli pressure and opening area with blowing pressure (Yoshikawa 1995, JASA 97(3), 1929 to 1939, abstract verified). This matters for the horn because it plays mostly in high modes.
- Lip waves and tissue elasticity in horn players: Yoshikawa and Muto 2003, Acta Acustica united with Acustica 89, 145 to 162 (citation verified in other papers' reference lists; full text not read). This is the most horn-specific source of lip mechanical parameters found, and it should be obtained.
- Lip opening and mouthpiece pressure during slurs: high-speed video at 5000 frames per second through a transparent Paxman 4C-style mouthpiece; lip open area peaks of the order of 10 to 20 square millimetres and mouthpiece pressure swings of the order of 1 to 3 kPa (read from the figure axes); slur transition 80 to 100 ms; octave slurs D3 to D4 (resonances 4 to 8) and fourth slurs C3 to F3 (resonances 3 to 4 of the open F horn); no notable difference between a narrow-bore Boosey and Hawkes horn and a wide-bore Conn horn (Logie et al. 2009, verified).
- Mouthpiece force: eleven professional players on a force-sensing mouthpiece; force rises with pitch and dynamic from about 5 N at F2 to about 40 N at F5 (mf), with 16 to 73 N between players at F5; the margin above the minimum force needed to sound is 35 to 57 percent, larger than in trumpet players (Hirano and Kinoshita 2026, abstract verified). This can stand in for an "embouchure pressure" control that scales lip stiffness with pitch.
- Orofacial muscle activity in horn players: Hirano, Kudo, Ohtsuki and Kinoshita 2013, Motor Control 17(3), 256 to 272 (citation verified, not read).
- General lip mechanics (not horn-specific): human lips show at least two mechanical modes with outward and inward striking behaviour and lower Q than artificial lips (Newton 2009 thesis; Newton, Campbell and Gilbert 2008), covered by survey (5).

Blowing pressures for the horn were not found in accessible primary sources during this survey (open question).

## 7. Physical models of the horn and real-time implementations

- Frequency-domain transmission-line models: Kausel's "Optimierer" (Vienna), based on Mapes-Riordan's conical and cylindrical transmission-line elements with viscothermal and radiation losses, fitted to BIAS measurements with a Rosenbrock search. Wachter used it to fit the Schmid F horn up to about 500 Hz and to build the simplified stopped model (Wachter 2008, verified; Kausel 2001, "Optimization of brasswind instruments and its application in bore reconstruction", Journal of New Music Research, cited by others, not read).
- FDTD of the whole bore: Bilbao and Chick simulated a natural horn in D (4.51 m) at 88.2 kHz as a single variable-section tube with time-domain viscothermal and radiation losses. Peak frequencies matched measurement mostly within 1 percent and amplitudes within about 1 dB, with a systematic small flattening for the horn attributed to temperature. The first horn resonance (about 23 Hz) was poorly modelled, which the authors attribute to the viscothermal loss filter at very low frequency; a frequency-domain calculation was better below about 50 Hz. 1D plane-wave models deviate at high frequency (Bilbao and Chick 2013, verified).
- Playable FDTD with lips and valves: the NESS brass environment (Harrison, Bilbao, Perry and Wishart 2015) includes a lip model, time-varying valves, a graphical bore editor with default trumpet, trombone and horn profiles, and a score format. Measured cost at 44.1 kHz on an Intel Core i5-4300U, AVX-vectorised C: trumpet 1.3 m with 1 valve, 0.857 s per second of sound (faster than real time); horn 4.5 m with 3 valves, 3.63 s per second of sound (about 3.6 times slower than real time) (Harrison et al. 2015, verified). The thesis behind it adds a nonlinear propagation comparison and partially open valves (Harrison-Harsley 2018, abstract verified).
- Stopped-horn simulation: Ebihara and Yoshikawa computed transmission functions of a stopped-horn model and compared with measurement (2011 report, 2013 JASA; abstracts verified).
- Commercial real-time: Audio Modeling's SWAM Horns and Tubas offers real-time control of pipe length, breath pressure, lip tension, a hand inside the bell for horns, mute distance and an additional valve for pedal notes; minimum CPU listed is a 1.6 GHz Core 2 Duo (Best Service product page, verified; vendor claims, no technical publication). No realism study of horn physical models against recordings was found.

Cost estimate for MPX (arithmetic, not from a source): with c about 345 m per second, the round trip of a 3.7 m F horn is about 21 ms, about 1030 samples at 48 kHz. A digital waveguide with a lumped bell reflectance filter costs little more than the delay lines and filters; an FDTD grid at the Courant limit has a few hundred cells at audio rates (Harrison et al. 2015), and the 2015 timing above suggests a single horn voice is within reach of a modern CPU but a polyphonic section is not, without a cheaper method.

## 8. Practical data a modeller needs

Bore profiles:

- Natural horn in D, 4.51 m, with PHC 22 mouthpiece: radius against axial position plotted in Bilbao and Chick 2013, Fig. 7 (log radius axis); the numbers would need to be digitised or requested from the authors.
- Modern Engelbert Schmid F/B-flat double horn, F side with mouthpiece, open fingering: bore plot in Wachter 2008, Fig. 88 (open access PDF, phaidra.kug.ac.at); string and caliper measurement, then adjusted by optimisation against BIAS impedance; numbers must be digitised.
- NESS brass environment default horn profile (Harrison et al. 2015). The code and examples were published at www2.ph.ed.ac.uk/~s0916351/CMJBrass2014.html (link in the paper; not checked for availability).
- Edinburgh Musical Instrument Museums (MIMEd) records with Arnold Myers's measurements for historic horns and mouthpieces (single dimensions, not full profiles).
- Acoustic pulse reflectometry of horn crooks: Kemp, Chick, Campbell and Myers, "Measurement techniques and the playability of terminal horn crooks" (St Andrews research portal listing; year and venue unverified).

Input impedance (all graphical unless stated):

- Dell et al. 2010: Yamaha 668 double horn, F and B-flat, hand and no hand, practice mutes, 0 to 1000 Hz (open access).
- UNSW brass acoustics page (Chen 2009 thesis): B-flat horn with and without hand, F horn.
- Wachter 2008: Schmid double horn, all valve combinations, F and B-flat, hand, no hand, no bell, stopping mute, wooden mute; tabulated stopped-horn peaks (open access).
- Watts 2009: Conn 8D, open F and B-flat, hand and no hand, stopped (open access reports).
- Bilbao and Chick 2013, Table I: numeric maxima and minima for the natural horn (open manuscript on the NESS site).
- Backus 1976: brass impedance curves including the horn with plasticene hand and stopper (JASA, subscription).

Radiation and spectra:

- Pätynen and Lokki 2010 (open access) and the Aalto anechoic recordings of symphonic music, which include horn parts.
- Luce and Clark 1967 for steady-state brass spectra.

Lips and player:

- Yoshikawa 1995; Yoshikawa and Muto 2003; Logie et al. 2009 (open access); Hirano and Kinoshita 2026.

## What we would need to build it, and where to get it

1. A bore profile of a modern double horn, F and B-flat branches, with mouthpiece, leadpipe, valve section, change valve and bell, at 1 to 5 mm resolution near the mouthpiece and coarser elsewhere. Best available start: digitise Wachter 2008 Fig. 88 (modern Schmid F side) and Bilbao and Chick 2013 Fig. 7 (natural horn). Better: ask John Chick, Arnold Myers or Murray Campbell (Edinburgh) or Wilfried Kausel (Vienna) for tabulated profiles, or obtain the NESS default horn file. The B-flat branch can be approximated by removing about 1 m of the F-side cylindrical section, then tuning against Dell et al.'s B-flat impedance.
2. A target impedance for validation: Dell et al. 2010 (F and B-flat, hand and no hand) for shape, Bilbao and Chick 2013 Table I for numeric peak positions on a horn of similar length, and Wachter 2008 for valve combinations and the stopped case.
3. A bell termination that depends on hand position. Practical options, in order of cost: (a) interpolate between three bell reflectance filters fitted to measured no-hand, normal-hand and stopped impedance (Dell, Wachter); (b) Ebihara and Yoshikawa's thin plate with an orifice at the hand position, with orifice area as the hand control (needs the 2013 full text); (c) Wachter's simplified stopped model (bell removed, short narrow cylinder at the end) as the fully stopped limit; (d) Pyle's truncation plus lumped resonator. The model must reproduce: lower and taller resonances above about 450 Hz with the hand, peak loss above about 750 Hz without it, the new low peak near 55 Hz and the continuous downward drift of peaks when stopped, and the inability of the lips to hold the weakened resonance near full closure.
4. Radiation: a high-pass bell transmission filter whose complement is the reflectance, darkened further by the hand, and a fixed directivity tilt for the backward bell (Pätynen and Lokki). In Rack, a mono output is enough; a second "room" output with the direct high frequencies attenuated would imitate the audience position.
5. Nonlinear propagation for fortissimo and stopped tones: a waveshaping or variable-delay steepening stage in the narrow part of the bore, strong enough to generate harmonics above 10 kHz in the stopped case, which implies running the bore at 2x oversampling or more at 44.1 or 48 kHz.
6. Lip model: a two-mode lip (outward striking in the lowest mode, upward striking in higher modes per Yoshikawa 1995) with small open area (order 10 to 20 square millimetres) and mouthpiece pressure swings of order 1 to 3 kPa (Logie et al. 2009). Lip stiffness and mouthpiece force should rise with target pitch (Hirano and Kinoshita 2026). Slur transitions should take 80 to 100 ms (Logie et al. 2009). Lip parameters should come from Yoshikawa and Muto 2003 and from survey (5).
7. Valves: rotary valves as switched or crossfaded bypass lengths (Harrison et al. 2015); the change valve as a switch between two complete bore descriptions sharing the leadpipe and bell.
8. Validation recordings: the Aalto anechoic horn recordings for timbre at several dynamics; published slopes (21, 13 and 9 dB per octave above 800 Hz for piano, forte and fortissimo).

## Open questions

- No tabulated, open bore profile of a modern double horn was found. This is the main gap.
- The value of the brassiness potential B for a modern horn was not found in accessible text (Myers et al. 2012 and Campbell, Gilbert and Myers 2021 likely contain it).
- Yoshikawa and Muto 2003 (horn lip elasticity) and Yoshikawa and Nobara 2017 (mute and hand modelling) were not read; both are likely primary inputs.
- Ebihara and Yoshikawa 2013 full text (hand as plate with orifice, numbers for hand-induced shifts) was not read.
- Typical blowing pressures and lip open areas across the horn's range, and how they differ from the trumpet, were not found for the horn.
- Whether a hand-position continuum from open through half-stopped to stopped can be represented by a single orifice-area parameter, and whether that reproduces Pyle's 2024 round-trip observation (later reflection, then an earlier dominant reflection), is untested.
- No published listening test of horn physical models against recordings was found; realism claims for SWAM are vendor statements.
- Chick, Lumb and Campbell 2004 (ISMA Nara, horn intonation) was cited but not located.
- Manufacturer bore and bell dimensions (Conn 8D, Yamaha 667 and 668, Alexander 103) were not verified because the web search budget ran out.
- How much of the horn's heard sound in a hall is reflected rather than direct has not been quantified here.
- The Logie 2013 thesis (super high notes, lip motion in transitions) exceeded the fetch size limit and was not read.

## References

Backus, J. (1968). Resonances in the brass instruments and the hand stopping of horns. J. Acoust. Soc. Am. 44(1 Suppl.), 367. doi 10.1121/1.1970432. (Abstract only; content from secondary summary.)

Backus, J. (1976). Input impedance curves for the brass instruments. J. Acoust. Soc. Am. 60(2), 470 to 480. doi 10.1121/1.381104. (Not read.)

Backus, J. (1977). The Acoustical Foundations of Music, 2nd ed. Norton, New York. (Cited by Watts.)

Benade, A. H. (1973). The physics of brasses. Scientific American 229(1), 24 to 35. (Cited by Campbell et al. 2021.)

Benade, A. H. (1976). Fundamentals of Musical Acoustics. Oxford University Press. (Cited by Dell et al.)

Bilbao, S., and Chick, J. (2013). Finite difference time domain simulation for the brass instrument bore. J. Acoust. Soc. Am. 134(5), 3860 to 3871. doi 10.1121/1.4822479. Author manuscript: https://www.ness.music.ed.ac.uk/wp-content/uploads/2013/08/bilbao_jasa_13_r2_double.pdf (open).

Campbell, M., Gilbert, J., and Myers, A. (2021). The Science of Brass Instruments. Springer (ASA Press), Modern Acoustics and Signal Processing. doi 10.1007/978-3-030-55686-0. Chapter 4, After the Lips: Acoustic Resonances and Radiation, pp. 101 to 216; Chapter 6, Shocks and Surprises: Refining the Elementary Model, pp. 271 to 334. (Not read; subscription.)

Campbell, M., Gilbert, J., and Myers, A. (2021). "Put a sock in it!" Mutes for musical horns. Acoustics Today 17(2), 13 to 22. doi 10.1121/AT.2021.17.2.13. (Open.)

Campbell, M., Myers, A., and Gilbert, J. (2025). Sounding Brass. Springer. doi 10.1007/978-3-031-86735-4. Chapter 5, Horns, doi 10.1007/978-3-031-86735-4_5. (Not read.)

Chen, J.-M. (2009). Vocal Tract Interactions in Woodwind Performance. PhD thesis, UNSW. (Source of the UNSW horn impedance figures; not read.)

Chen, J.-M., Smith, J., and Wolfe, J. (2014). The effect of nearby timpani strokes on horn playing. J. Acoust. Soc. Am. 135(1), 472 to 478. doi 10.1121/1.4829533. Summary page: https://newt.phys.unsw.edu.au/jw/timpani-horn/timpani-horn.html

Chick, J., Lumb, C., and Campbell, M. (2004). Passive acoustic characteristics and intonation problems of modern orchestral horns. Proc. ISMA 2004, Nara, 9 to 12. (Cited by Dell et al.; not located.)

Dell, N., James, R., Davidson, J., and Wolfe, J. (2010). The effect of hand and mute on the impedance spectra of the horn. Proc. International Symposium on Music Acoustics (ISMA 2010), Sydney and Katoomba. https://www.phys.unsw.edu.au/jw/reprints/DellISMA2010.pdf (open).

Dickens, P., Smith, J., and Wolfe, J. (2007). Improved precision in measurements of acoustic impedance spectra using resonance-free calibration loads and controlled error distribution. J. Acoust. Soc. Am. 121(3), 1471 to 1481. (Measurement method used by Dell and Chen.)

Ebihara, T., and Yoshikawa, S. (2011). Numerical simulations on the hand-stopping and measurements of the associated wall vibration in a natural horn (in Japanese). IPSJ SIG Technical Report 2011-MUS-89(3), 1 to 5. https://ipsj.ixsq.nii.ac.jp/records/72684

Ebihara, T., and Yoshikawa, S. (2013). Nonlinear effects contributing to hand-stopping tones in a horn. J. Acoust. Soc. Am. 133(5), 3094 to 3106. doi 10.1121/1.4798669.

Ebihara, T., and Yoshikawa, S. (2013). Nonlinear wall vibration and wave steepening contributing to tonal metallicness and brassiness in a horn. Proceedings of Meetings on Acoustics, 035071. doi 10.1121/1.4799607. (Not read.)

Gilbert, J., Campbell, D. M., Myers, A., and Pyle, R. W. (2007). Differences between brass instruments arising from variations in brassiness due to nonlinear propagation. Proc. ISMA 2007, Barcelona. HAL hal-00475561. (Author list partly unverified.)

Harrison, R. L., Bilbao, S., Perry, J., and Wishart, T. (2015). An environment for physical modeling of articulated brass instruments. Computer Music Journal 39(4), 80 to 95. doi 10.1162/COMJ_a_00332. Open copy: https://www.pure.ed.ac.uk/ws/files/21585566/Harrison_etal_CMJ_2015_An_Articulated_Brass_Instrument_Synthesis.pdf

Harrison-Harsley, R. L. (2018). Physical modelling of brass instruments using finite-difference time-domain methods. PhD thesis, University of Edinburgh. https://era.ed.ac.uk/handle/1842/31272

Hirano, T., and Kinoshita, H. (2026). Mouthpiece lip-pressing force and its component minimum and margin forces during French horn playing. J. Acoust. Soc. Am. 159(3), 1987 to 1998. doi 10.1121/10.0042982.

Hirano, T., Kudo, K., Ohtsuki, T., and Kinoshita, H. (2013). Orofacial muscular activity and related skin movement during the preparatory and sustained phases of tone production on the French horn. Motor Control 17(3), 256 to 272. doi 10.1123/mcj.17.3.256.

Kausel, W. (2001). Optimization of brasswind instruments and its application in bore reconstruction. Journal of New Music Research 30(1). (Cited by others; not read; pages unverified.)

Logie, S. M., Stevenson, S., Apostoli, A. G., Chick, J. P., and Campbell, D. M. (2009). Transient behaviour in the motion of the brass player's lips during a lip-slur. NAG/DAGA 2009, Rotterdam, 889 to 892. https://www.pub.dega-akustik.de/NAG_DAGA_2009/data/articles/000332.pdf (open).

Logie, S. M. (2013). Acoustical study of the playing characteristics of brass wind instruments. PhD thesis, University of Edinburgh. https://era.ed.ac.uk/handle/1842/7617

Luce, D., and Clark, M. (1967). Physical correlates of brass-instrument tones. J. Acoust. Soc. Am. 42, 1232 to 1243. (Cited by Pätynen and Lokki.)

Mapes-Riordan, D. (1993). Horn modeling with conical and cylindrical transmission-line elements. J. Audio Eng. Soc. 41(6), 471 to 481. (Cited by Wachter.)

Martin, D. W. (1942). Directivity and the acoustic spectra of brass wind instruments. J. Acoust. Soc. Am. 13, 309 to 313. (Cited by Pätynen and Lokki.)

Meyer, J. Acoustics and the Performance of Music. (Horn directivity; cited by Pätynen and Lokki and by Wachter; not read.)

Myers, A., Pyle, R. W., Gilbert, J., Campbell, D. M., Chick, J. P., and Logie, S. (2012). Effects of nonlinear sound propagation on the characteristic timbres of brass instruments. J. Acoust. Soc. Am. 131(1), 678 to 688. doi 10.1121/1.3651093.

Newton, M. J. (2009). Experimental mechanical and fluid mechanical investigations of the brass instrument lip-reed and the human vocal folds. PhD thesis, University of Edinburgh. https://era.ed.ac.uk/handle/1842/3140

Norman, L., Chick, J. P., Campbell, D. M., Myers, A., and Gilbert, J. (2010). Player control of "brassiness" at intermediate dynamic levels in brass instruments. Acta Acustica united with Acustica 96(4), 614 to 621. doi 10.3813/AAA.918316.

Otondo, F., and Rindel, J. H. (2004). The influence of the directivity of musical instruments in a room. Acta Acustica united with Acustica 90, 1178 to 1184. (Cited by Pätynen and Lokki.)

Pätynen, J., and Lokki, T. (2010). Directivities of symphony orchestra instruments. Acta Acustica united with Acustica 96, 138 to 167. doi 10.3813/AAA.918265. https://users.aalto.fi/~ktlokki/Publs/patynen_aaua_2010.pdf (open).

Payne, I. W. (1968). Observations on the stopped notes of the French horn. Music and Letters 49(2), 145 to 154. doi 10.1093/ml/xlix.2.145. (Not read.)

Pyle, R. W. (1964). Pitch change of the stopped French horn. J. Acoust. Soc. Am. 36(5 Suppl.), 1025. doi 10.1121/1.2143243. (Abstract only.)

Pyle, R. W. (1975). Effective length of horns. J. Acoust. Soc. Am. 57(6), 1309 to 1317. (Cited by Wachter.)

Pyle, R. W. (1990). Factitious tones and hand-stopping. The Horn Call 21(1), 36 to 43. (Cited by Wachter.)

Pyle, R. (2010). The influence of mouthpiece cup shape on "brassiness". Proc. Second Vienna Talk, Vienna. https://viennatalk.mdw.ac.at/papers/Pap_02_15_Pyle.pdf (open).

Pyle, R. (2024). Pitch change of the stopped French horn. J. Acoust. Soc. Am. 155(3 Suppl.), A109. doi 10.1121/10.0026978. (Abstract only.)

Audio Modeling. SWAM Horns and Tubas, product page at Best Service: https://www.bestservice.com/swam_horns_and_tubas.html (vendor claims.)

UNSW Music Acoustics. Brass instrument (lip reed) acoustics: an introduction. https://newt.phys.unsw.edu.au/jw/brassacoustics.html

Wachter, S. (2008). Untersuchungen zum Stopfen bei Hörnern (Investigations of stopping in horns). Diploma thesis, Institut für Wiener Klangstil, University of Music and Performing Arts Vienna, and IEM Graz. https://phaidra.kug.ac.at/download/o:11041 (open, German, 153 pages).

Watts, A. (2009). Acoustical investigations of the French horn and the effects of the hand in the bell. Phys498POM report, University of Illinois. https://courses.physics.illinois.edu/phys406/sp2017/Student_Projects/Spring09/Adam_Watts/A_Watts_Phys498POM_Spring09_Final_Report.pdf (open).

Watts, A. (2009). Spectral analysis of the French horn and the hand-in-bell effect. REU thesis, University of Illinois. https://courses.physics.illinois.edu/phys406/sp2017/NSF_REU_Reports/2009_reu/Adam_Watts/adamwatts_thesis.pdf (open).

Widholm, G. (1988). Klangliche Besonderheiten der Hörner zur Zeit Mozarts im Verhältnis zu modernen Hörnern. Mozart-Jahrbuch 1987/88, 153 to 179. (Cited by Dell et al.)

Widholm, G. (2005). The Vienna horn: a historic relict successfully used by top orchestras of the 21st century. Forum Acusticum, Budapest. (Cited by Wachter and Logie.)

Yoshikawa, S. (1995). Acoustical behavior of brass player's lips. J. Acoust. Soc. Am. 97(3), 1929 to 1939. doi 10.1121/1.412066.

Yoshikawa, S., and Muto, Y. (2003). Lip-wave generation in horn players and the estimation of lip-tissue elasticity. Acta Acustica united with Acustica 89, 145 to 162. (Not read.)

Yoshikawa, S., and Nobara, N. (2017). Acoustical modeling of mutes for brass instruments. In A. Schneider (ed.), Studies in Musical Acoustics and Psychoacoustics, Current Research in Systematic Musicology 4, Springer, 143 to 186. (Not read.)
