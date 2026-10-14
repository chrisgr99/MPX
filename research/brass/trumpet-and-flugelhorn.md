# Trumpet and flugelhorn: literature survey for real-time physical models

Compiled 13 October 2026 for the MPX brass work. This file covers the B-flat trumpet and the B-flat flugelhorn specifically. General brass physics (lip valve, bore, bell, nonlinear propagation, numerical methods), the trombone, the French horn, and the player model with existing implementations are covered by the companion surveys in this folder.

Conventions. A sentence with a citation and no tag reports what that source states. "My arithmetic" marks numbers I computed from cited values. "My inference" marks a conclusion I drew that no source states directly. "Unverified" marks a statement from memory that I could not confirm against a source during this survey. The reference list at the end gives the reading status of every source: read in full, relevant sections read, abstract or search excerpt only, or known only through another source's reference list.

Limits of this survey. Several publishers (Acoustical Society of America, Acta Acustica full text, HAL, Springer) blocked direct download, and the shared web-search allowance ran out before the lip-measurement literature (Yoshikawa 1995, Newton et al. 2008, Stevenson et al. 2009) could be checked. Those items appear with their reading status marked.

## Summary

The B-flat trumpet and the B-flat flugelhorn have the same nominal tube length, about 1.4 m, and the same nominal pitch, but different bore shapes and mouthpieces. The trumpet has a long cylindrical section of about 11.65 mm bore through the valves (Yamaha YTR-8335 specification; 11.66 mm measured by Rose and Holloway 2012) and a 123 mm bell. The flugelhorn has its valves close to the mouthpiece and a bore that expands gradually from the valves to a bell of about 152 mm (Yamaha YFH-631G and YFH-8310Z specifications; Herbert, Myers and Wallace 2019, cited by the Wikipedia flugelhorn article). Its mouthpiece has a deeper, funnel-shaped cup.

Measured input impedance of a B-flat trumpet (Bilbao and Chick 2013, BIAS measurement) has maxima at 83.5, 234.2, 353.1, 469.1, 591.6, 702.7 and 812.5 Hz. Peaks 2 to 7 lie within 18 cents of a harmonic series on concert B-flat 2 (116.5 Hz) when referred to the fourth peak (my arithmetic). The first peak lies about 580 cents below the pedal note and is not used in playing (Chen, Smith and Wolfe 2012). No measured input impedance for a flugelhorn was found.

The trumpet's brassy timbre at fortissimo is caused by cumulative nonlinear steepening of the pressure wave in the narrow cylindrical bore; shock waves leaving a trumpet bell have been photographed (Pandya, Settles and Miller 2003). A bore-profile parameter, the brassiness potential B, ranks instruments by how much nonlinear steepening their bore allows; trumpets have higher B than flugelhorns, with a wide spread inside each family (Pyle 2010; Myers et al. 2012; Campbell, Gilbert and Myers 2021). The mouthpiece also matters: on one trumpet, a flugelhorn mouthpiece gave a lower spectral centre of gravity at all dynamics, and at soft levels the ratio was close to the square root of the ratio of cup volumes (Pyle 2010).

Trumpet-specific physical models exist and some ran in real time decades ago: Adachi and Sato (1995, 1996, two-dimensional lips, reflection function, 8 kHz sampling), Vergez and Rodet (1997 to 2001, one-mass lips, measured reflection function, nonlinear propagation, real time with a MIDI wind controller), and the Edinburgh NESS finite-difference environment (Harrison, Bilbao and Perry 2015), which needs 0.86 s of CPU per second of sound for a 1.3 m one-valve trumpet on a 2013 laptop core at 44.1 kHz. No physical model specific to the flugelhorn was found in the literature; the only flugelhorn physical model found is the commercial SWAM Flugelhorn, which publishes no technical description.

One complete trumpet bore profile is openly downloadable as numbers: the OpenWInD example file Geom_trumpet_conical.txt (GPL-3.0), 1.4187 m from mouthpiece rim to bell, including mouthpiece, leadpipe, a 0.495 m cylinder of 11.64 mm bore, and a 122 mm bell. No flugelhorn bore profile was found in numerical form.

## 1. Bore geometry

### 1.1 B-flat trumpet: length, bore and bell

Total length. Published figures for the open (no valve) air column are 1.38 m for a Smith-Watkins B-flat trumpet (Bilbao and Chick 2013), 1.381 m for the trumpet used in the NESS run-time tests (Harrison, Bilbao and Perry 2015), "approximately 1.4 m, most of which is cylindrical tubing" (Moore 2016), 1.4 m (Rose and Holloway 2012), and 1.4187 m for the OpenWInD example profile, which includes the mouthpiece. The UNSW music acoustics pages describe the B-flat trumpet as about half the length of a tenor trombone and acoustically "rather like a one-half scale model of the trombone" (Wolfe, UNSW brass acoustics page).

Main bore. The bore through the valves and tuning slide is cylindrical. Yamaha gives 11.65 mm (0.459 inch, labelled ML, medium-large) for the YTR-8335 (manufacturer specification page). Rose and Holloway (2012) measured 11.66 mm on the trumpet they modelled and report that the inside diameter stayed constant through the whole tubular section. The Bach Stradivarius 180S37 is also sold with a 0.459 inch bore (dealer listings; the manufacturer page was not read). The OpenWInD profile uses a radius of 5.82 mm, that is 11.64 mm diameter.

Bell. Yamaha gives a bell diameter of 123 mm (4 7/8 inch) for the YTR-8335. The OpenWInD profile ends at a radius of 61 mm, 122 mm diameter. Pyle (2010) uses the Vincent Bach model 37 B-flat bell as his example of a relatively narrow trumpet bell.

Leadpipe and mouthpiece. Moore (2016) states that the leadpipe is approximately 25 cm long in a trumpet and continues the gradual expansion from the mouthpiece backbore to the cylinder diameter, and that most of the bore expansion occurs in the mouthpiece.

The OpenWInD trumpet profile, section by section (read from the file; the file does not state which instrument it was measured from):

| Axial position (m) | Radius (mm) | Section |
|---|---|---|
| 0 to 0.0177 | 8.40 falling to 1.825 | mouthpiece cup to throat (throat diameter 3.65 mm) |
| 0.0177 to 0.0837 | 1.825 rising to 4.20 | backbore |
| 0.0837 to 0.3037 | 4.64 rising to 5.82 | leadpipe |
| 0.3037 to 0.7987 | 5.82 constant | cylindrical section, 0.495 m |
| 0.7987 to about 1.02 | 5.83 to 7.39, with small steps | tuning slide and post-valve tubing |
| about 1.02 to 1.36 | 7.39 rising to 22.5 | gradual expansion into the bell |
| 1.36 to 1.4187 | 22.5 rising to 61.0 | bell flare |

My arithmetic on that profile: the strictly cylindrical part is 0.495 m, 35 percent of the total length; the part whose radius stays within 10 percent of 5.82 mm runs from about 0.17 m to about 0.94 m, about 0.77 m or 54 percent of the total. This agrees in kind with Moore's statement that most of the trumpet is cylindrical tubing, but the figures depend on the instrument.

A second OpenWInD file, Geom_trumpet.txt, describes a simplified trumpet in four parametric pieces: a spline mouthpiece from 8.4 mm to 1.825 mm radius over 17.7 mm, a cone to 5.82 mm radius at 0.3 m, a cylinder to 0.8 m, and a Bessel-horn bell with exponent 0.67 to 61 mm radius at 1.4187 m. A third file (examples/Thibault-Chabassier_JASA2020/simplified-trumpet) uses a 0.716 m cylinder of 6 mm radius and a Bessel bell with exponent 0.7 to 60 mm radius at 1.335 m.

### 1.2 Flugelhorn: bore and bell

The flugelhorn is distinguished from the trumpet and cornet mainly by a wider conical bore that expands over a longer proportion of its tubing; its valves sit close to the mouthpiece, so more of the tubing after the valves expands gradually into the bell (Herbert, Myers and Wallace 2019, The Cambridge Encyclopedia of Brass Instruments, p. 177, as cited in the Wikipedia flugelhorn article; I did not read the encyclopedia itself). Moore (2016) places the flugelhorn in the conical branch of the brass family and notes that it is not completely conical. Rose and Holloway (2012) describe the main geometric differences between their trumpet, cornet and flugelhorn as the mouthpiece cup depth and a flugelhorn bell flare that begins earlier.

Bore size. Yamaha lists the YFH-631G at 11.00 mm (0.433 inch, labelled M) and the YFH-8310Z at 10.5 mm (0.413 inch, labelled S); both have a 151.8 mm (6 inch) bell (manufacturer specification pages). Rose and Holloway (2012) measured a flugelhorn tubing diameter of 10.50 mm, 11 percent narrower than their trumpet, and state that it stayed constant through the tubular section they measured. My inference: the manufacturer bore figure and Rose and Holloway's figure both refer to the narrow tubing at the valves; the expanding part after the valves is the part that makes the flugelhorn conical, and Rose and Holloway's model represents it within their measured "bell" profile, which is published only as a figure.

Valves. The modern flugelhorn is usually pitched in B-flat with three piston valves; rotary-valve flugelhorns are common in central and eastern Europe; some are built with a fourth valve that lowers the pitch by a perfect fourth (Herbert, Myers and Wallace 2019; Klaus 2017; both as cited by Wikipedia). Unverified: many flugelhorns have no main tuning-slide bow in the conical section and are tuned by sliding the mouthpiece receiver; some have a third-valve slide trigger.

No numerical flugelhorn bore profile was found. The nearest published data are the Rose and Holloway bell profile figure, the scatter plots of brassiness potential against minimum bore for trumpets, cornets and flugelhorns (Pyle 2010 Figure 6, adapted from Myers et al. 2012), and the Edinburgh bore-measurement collection behind the spectral enrichment parameter of Campbell and Myers (2023), which covers 1370 instruments but is not published as a dataset as far as I could find.

### 1.3 Valves, valve combinations and tuning compromises

Valve intervals. On a trumpet the second valve lowers the pitch by about a semitone, the first by about a whole tone, and the third by about three semitones (Wolfe, UNSW brass page; Moore 2016). Lowering by one semitone requires lengthening the air column by 5.95 percent, and by two semitones by 12.25 percent; for a 140 cm trumpet these are 8.3 cm and 17.2 cm (Moore 2016).

The combination problem. A valve loop sized for use alone is too short when another valve is already down, because each interval is a fixed ratio of the current length. Moore (2016) gives the example that first and second valves together add 25.5 cm while 26.5 cm is needed for three semitones. Makers make the first two loops slightly longer than the single-valve ideal so that single valves are slightly flat and combinations slightly sharp, and the player bends each note toward pitch; the third loop is sized for three semitones from the open instrument and is given a movable slide worked by the ring finger, and many trumpets also have a first-valve slide moved by the thumb (Moore 2016).

My arithmetic for an idealised 1.40 m trumpet whose resonances scale with a single effective length, with each loop exactly in tune when used alone:

| Valves | Interval | Loop length added (cm) | Length needed (cm) | Shortfall (cm) | Resulting error (cents sharp) |
|---|---|---|---|---|---|
| 2 | 1 semitone | 8.3 | 8.3 | 0 | 0 |
| 1 | 2 semitones | 17.1 | 17.1 | 0 | 0 |
| 3, or 1 and 2 | 3 semitones | 26.5 (valve 3) or 25.5 (1 and 2) | 26.5 | 0 or 1.0 | 0 or 11 |
| 2 and 3 | 4 semitones | 34.8 | 36.4 | 1.6 | 16 |
| 1 and 3 | 5 semitones | 43.6 | 46.9 | 3.2 | 30 |
| 1, 2 and 3 | 6 semitones | 52.0 | 58.0 | 6.0 | 54 |

These are the reasons for the first-valve and third-valve slides: the 1 and 3 combination and the 1, 2 and 3 combination are the notes players correct with the third-valve slide (low written D and C sharp) and the first-valve slide (unverified as to which slide is used for which note; the arithmetic shows the size of the correction, about 3 cm and 6 cm). The single-effective-length model is approximate because the bell makes the effective length frequency dependent (my inference).

Seven distinct lengths. Three valves give eight combinations, of which two (third valve alone, and first plus second) give nearly the same length, so a trumpet has seven tube lengths; D'haes and Rodet (2002) tabulate which written notes each length produces in modes 1 to 8 and use the constraint that a player holds one of these seven lengths for each note when estimating control parameters for their model.

Valve losses. A search excerpt of Pyle (2020, Historic Brass Society Journal) states that impedance peaks fingered first and third valves are all lower than their neighbours fingered open, because of energy lost in the added tubing; I did not read the paper.

Valve modelling. Partly open valves and the junction of main, default and bypass tubes are modelled in Bilbao (2011, DAFx) and in the NESS environment (Harrison, Bilbao and Perry 2015), where the default tube is the short straight path through an unpressed valve and the bypass tube is the valve loop. In the NESS example custom instrument the three bypass loops are 130.7, 57.5 and 199.8 mm with 20 mm default tubes (Harrison et al. 2015, Computer Music Journal, Table 2); these are values for a 1 m custom instrument, not trumpet data. OpenWInD (examples/technical/Ex2bis_Brass_Valves.py) specifies valves by deviation point, reconnection point, loop radius and loop length, with a fingering chart that allows half-pressed valves. Moore (2016) notes that valve placement has larger acoustical consequences in instruments that are not mainly cylindrical, because inserting cylindrical tubing interrupts a gradual expansion; this applies to the flugelhorn.

## 2. Mouthpieces

### 2.1 Function

The mouthpiece has a cup, a throat and a backbore. The cup volume acts as an acoustic compliance and the throat as an inertance, and together with the resistance they form a resonance (Moore 2016). Below this resonance the mouthpiece raises the input impedance of the instrument; Moore's simulation shows the mouthpiece raising the impedance substantially between 200 Hz and 1.5 kHz, the normal playing range. The UNSW page describes the cup and constriction as a Helmholtz resonator whose "pop tone" falls with a larger enclosed volume or a narrower constriction, and states that the mouthpiece lowers the frequencies of the highest resonances and strengthens some others. Fletcher and Tarnopolsky (1999) describe the resulting envelope of the input resistance as a broad peak with a Q of about 5, typically at 500 to 1000 Hz, and use it to explain a common envelope in trumpet spectra at low playing levels.

Values. The Bach 1 1/4 C trumpet mouthpiece is stated by the manufacturer to have a resonance of about 800 Hz (Bach 2011, as cited by Rose and Holloway 2012). A course note based on Benade (1976) gives about 850 Hz for a trumpet mouthpiece popping frequency (secondary source, read as search excerpt only). The OpenWInD trumpet profile has a cup entry radius of 8.4 mm (16.8 mm diameter), a throat of 3.65 mm diameter 17.7 mm from the rim, and a backbore reaching 8.4 mm diameter at 83.7 mm. Unverified: the common Bach throat size number 27 is 0.144 inch, 3.66 mm, which matches the OpenWInD throat.

### 2.2 Measured and simulated effects of mouthpiece shape on a trumpet

The Institut für Wiener Klangstil (mdw Vienna) report "Untersuchung zur Beeinflussung des Resonanzverhaltens von Trompeten durch unterschiedliche Mundstücksformen" computed trumpet resonances with different mouthpieces: increasing cup volume lowers all trumpet resonances and decreasing it raises them, by up to 35 cents for typical shapes and up to 180 cents for an extreme cylindrical cup; cup shape at constant volume also has an effect; varying the throat diameter from 3 to 5.4 mm shifts the mouthpiece resonance by up to 80 cents but the trumpet resonances by only up to 10 cents, lowering the first four and raising the higher ones; backbore changes shift intonation by up to 30 cents, leaving the fifth resonance unchanged. Authors and year are not given on the web page.

Moore (2016) states that players who want to play above the normal range often change to a shallower cup, which raises the mouthpiece resonance.

### 2.3 Trumpet versus flugelhorn mouthpiece

Flugelhorn mouthpieces have a deeper, funnel-shaped cup, less conical than a horn mouthpiece, where trumpet and cornet mouthpieces are shaped more like a cup (Baines and Herbert 2001, Grove Music Online, as cited by Wikipedia). Unverified: flugelhorn mouthpieces also have a longer shank with a different taper, so a trumpet mouthpiece does not fit a flugelhorn receiver properly; Rose and Holloway (2012) state that they could not insert the trumpet mouthpiece into their flugelhorn without risk of damage because of the different bore.

Pyle (2010) played one S. E. Shires B-flat trumpet with a normal trumpet mouthpiece and a flugelhorn mouthpiece, both by Denis Wick with identical rims, measuring mouthpiece pressure with a flush transducer and radiated sound 60 cm on axis, with the player masked by loud noise to stop him adjusting timbre. Results, for F4: the power-weighted average frequency of the radiated spectrum was consistently higher with the trumpet mouthpiece; at soft levels the ratio of the two average frequencies was very close to the square root of the ratio of the cup volumes measured from wax castings, which suggests that at low levels the average frequency is set by a resonance of the cup compliance with the inertance seen looking into the instrument; above an arbitrary-reference level of about 45 dB the average frequency rose faster with level, which Pyle attributes to nonlinear distortion becoming dominant; for a given maximum rate of rise of mouthpiece pressure the flugelhorn mouthpiece produced more external sound pressure; and the flugelhorn mouthpiece made the trumpet play progressively flatter as pitch rose, with the F4 to B-flat 4 interval 12 cents smaller than with the trumpet mouthpiece.

Rose and Holloway (2012) modelled trumpet, cornet and flugelhorn by axisymmetric linear finite elements (ANSYS) with measured mouthpiece and bell profiles (Bach 1 1/4 C trumpet, Bach 1 1/2 C flugelhorn, Denis Wick 4B cornet mouthpieces) and compared with recorded F4 at mezzo-forte. They found that the tubing mainly affects the lower harmonics and the mouthpiece mainly affects the mid range, around 700 Hz for trumpet and cornet and 1100 Hz for the flugelhorn; the simulated mouthpiece inlet pressure spectra peaked at about 790 Hz, 660 Hz and 1150 Hz for trumpet, cornet and flugelhorn. A flugelhorn model fitted with the trumpet mouthpiece moved its dominant spectral peak. The paper is a peer-reviewed conference paper with a linear model and no lips; the high flugelhorn figure conflicts with the deeper flugelhorn cup and should be treated with caution (my inference).

## 3. Input impedance, resonances and the harmonic series

### 3.1 Measured trumpet impedance

Bilbao and Chick (2013) measured the input impedance of a Smith-Watkins B-flat trumpet with a Kelly "Screamer" mouthpiece using the BIAS system in an anechoic room at 19 to 20 degrees C, measured its bore with rod gauges, calipers and external diameters (estimated accuracy plus or minus 5 percent), and compared the measurement with a finite-difference simulation at 88.2 kHz. They note that the Screamer is a high-register mouthpiece with a very small cup volume, so the upper peaks are not those of a standard set-up. Measured maxima (their Table I, open valves):

| Peak | Measured (Hz) | Simulated (Hz) | Deviation from n times 116.54 Hz (cents, my arithmetic) | Deviation from n times peak 4 divided by 4 (cents, my arithmetic) |
|---|---|---|---|---|
| 1 | 83.5 | 83.0 | minus 577 | minus 588 |
| 2 | 234.2 | 233.0 | plus 8 | minus 3 |
| 3 | 353.1 | 351.5 | plus 17 | plus 6 |
| 4 | 469.1 | 469.7 | plus 11 | 0 |
| 5 | 591.6 | 588.3 | plus 26 | plus 15 |
| 6 | 702.7 | 699.6 | plus 9 | minus 2 |
| 7 | 812.5 | 809.8 | minus 7 | minus 18 |

They also list the minima: 122.5, 261.1, 379.1, 505.6, 631.1, 754.9 and 878.8 Hz. Frequencies matched the simulation to under 1 percent and amplitudes to within about 1 dB. The bore profile is published only as a figure (their Figure 7); Harrison-Harsley (2018, thesis, Figures 4.15 and 4.16) shows the same profile and impedance to 2 kHz, with peak sharpness decreasing above the twelfth resonance.

Chen, Smith and Wolfe (2012) measured the input impedance of a King 600 trumpet in the plane of the mouthpiece rim with the UNSW three-microphone system and state that, for a B-flat trumpet with no valves, the second and higher peaks fall close to a harmonic series on concert B-flat 2, while the first resonance is well below B-flat 2 and is not normally used. The UNSW brass page shows a measured trumpet impedance spectrum as an image (brass/brassZ/trumpet_Bb3.gif), not as a data file.

Older measurements: Backus (1976) published input impedance curves for the brass instruments; Caussé, Kergomard and Lurton (1984) compared measured brass impedances with numerical models; Elliott, Bowsher and Watkinson (1982) measured input and transfer responses of brass instruments. I read none of these three; they are cited here from other papers' reference lists and search excerpts.

### 3.2 Calculated trumpet impedance

Adachi and Sato (1995 report; 1996 JASA) calculated the impedance of a real B-flat trumpet from its dimensions. Their Table II gives peak frequencies and magnitudes normalised by the characteristic impedance: mode 1, 87 Hz, 43.7; mode 2, 232 Hz, 35.2; mode 3, 341 Hz, 46.4; mode 4, 457 Hz, 55.7; mode 5, 572 Hz, 48.1; mode 6, 686 Hz, 51.6; mode 7, 800 Hz, 44.5; mode 8, 913 Hz, 28.5. My arithmetic: referred to the fourth peak, modes 3 to 8 lie within 9 cents of a harmonic series and mode 2 is 26 cents high; the whole set is about 35 cents below concert pitch at A 440.

Kaburagi et al. (2011) modelled a Yamaha YTR-2320E student trumpet with mouthpiece as concatenated truncated cones and report peaks at 234, 345, 463, 580, 695, 811 and 924 Hz for modes 2 to 8; they omit the first resonance because it is not used in playing. The bore radius is plotted in their Figure 5A.

### 3.3 How the series becomes harmonic, and the pedal note

A cylinder closed at the lips would have resonances in the odd ratios 1, 3, 5. The bell raises the lower resonances most, because long wavelengths are reflected earlier in the flare, and the mouthpiece lowers the upper ones; the result is a series close to 2, 3, 4, 5 and so on, with a first resonance far below the harmonic position (Moore 2016; Wolfe, UNSW page; Rose and Holloway 2012). Moore describes the result as a harmonic series with a detuned fundamental.

Pedal note. The note at the harmonic-series fundamental (written C3, sounding concert B-flat 2, 116.5 Hz on a B-flat trumpet) can be played but does not correspond to a resonance; its spectrum has very little power at the fundamental (Wolfe, UNSW page). The first impedance peak is at 83.5 Hz (Bilbao and Chick 2013, measured) or 87 Hz (Adachi and Sato, calculated), which my arithmetic puts about 5 to 6 semitones below the pedal. Unverified from the original: Benade (1976) explains the pedal as a "privileged frequency" sustained because its harmonics coincide with resonances 2, 3, 4 and higher. A HAL paper titled "Numerical simulation of the production of pedal notes in brass instruments" (hal-01086749, search excerpt only, authors not confirmed) states that trombonists play the pedal near 62 Hz around a minimum of the input impedance, and that instruments with long cylindrical sections have their first peak strongly shifted downwards. My inference for modelling: a lip model coupled to the full measured impedance should be able to produce the pedal with the lip frequency set near 116 Hz; a model that only represents the playing resonances by a few modes from mode 2 upward would also need the higher modes present to support it.

### 3.4 Playing frequency versus resonance frequency

Caussé, Eveno, Gilbert and Petiot (ICA 2013) and the journal version (Eveno and co-authors 2014, Acta Acustica united with Acustica 100; full author list unverified) measured three trumpets differing only in leadpipe; four players played regimes 2 to 6 on four fingerings at three dynamics, three times each, 2160 notes. A change in a resonance frequency produced a change in the same direction in the playing frequency for most regimes; the player had a limited influence on the overall intonation; dynamic level had only a weak effect on pitch; and Wogram's "sum function" did not predict playing frequency better than the impedance itself. Abstract only; the full texts were blocked.

Chen, Smith and Wolfe (2012) found that trumpeters do not systematically tune vocal-tract resonances: tract impedance peaks were usually much smaller than the bore peaks over the measured range, and they attribute the difference from saxophonists to the trumpeter's greater control of the lip natural frequency. All players said the probe stopped them reaching their highest notes, so tract tuning above that range is not excluded.

### 3.5 Flugelhorn impedance

No measured or calculated flugelhorn input impedance was found. A search excerpt from a source I could not identify with certainty (possibly Pyle, Historic Brass Society Journal 1991) states that the lowest air-column resonances of brass instruments usually lie below the playing pitches, more so for instruments with a high percentage of cylindrical tubing, such as the natural trumpet, than for the flugelhorn and euphonium (not read; author not confirmed). My inference: a flugelhorn's first resonance therefore lies closer to the pedal position than a trumpet's, and its peak heights should fall off differently because the wider conical section has lower wall losses per unit length and a lower characteristic impedance; this needs to be checked against a computed impedance from a real flugelhorn profile.

## 4. Timbre and spectra

### 4.1 Blowing pressure, level and spectral envelope (trumpet)

Fletcher and Tarnopolsky (1999) measured blowing pressure against radiated level for a professional (in an anechoic room) and two experienced amateurs on standard B-flat trumpets. Findings: a threshold pressure roughly proportional to the note frequency; above threshold the level rises about 15 dB per doubling of pressure, then saturates at about 3 dB per doubling; the professional reached about 25 kPa, above normal systolic blood pressure of about 18 kPa; the two amateurs used up to about 7 and 15 kPa; maximum A-weighted level about 110 dB at 1 m for high notes. Bouhuys (1965), as summarised by Fletcher and Tarnopolsky, measured mouth pressures of about 4 to 13 kPa for high notes from pianissimo to fortissimo, and overall efficiencies of 0.01 to 0.03 percent for low notes and 0.03 to 1 percent for high notes.

Spectral envelope. For the note C4 played softly and loudly, Fletcher and Tarnopolsky describe three regions: a rise of about 6 dB per octave below about 1000 Hz, a nearly constant slope from 1000 Hz to an upper transition at 2 to 3 kHz that moves up with level, and a fall of about 30 dB per octave above it at low level and about 10 dB per octave at high level. Across notes and levels they find a basic low-level envelope peaking near 1000 Hz, set by the mouthpiece resonance and the bell radiation, on top of which a second mechanism extends the spectrum upward as level rises, attributed to nonlinear steepening in the cylindrical bore. They quote Luce and Clark (1967) for the average trumpet spectrum: below the bell radiation cut-off, typically about 1000 Hz, radiated power rises 2 to 4 dB per octave; above it, it falls 15 to 25 dB per octave; the lower slope increases and the upper slope decreases with intensity. They also state that typically less than 10 percent of the lip-generated power is radiated, and that the radiation efficiency rises 6 dB per octave up to the cut-off of about 1 kHz and is flat above it.

Vergez and Rodet (2001) measured with an artificial mouth with water-filled latex lips on a B-flat trumpet (fourth mode, written C5) at pianissimo, forte and fortissimo, and show that spectral enrichment with rising level is much larger at the bell than in the mouthpiece. Rose and Holloway (2012) found that at mezzo-forte F4 the dominant harmonic is the third for trumpet and cornet and the first for the flugelhorn, and that their linear model failed to predict the strong trumpet third harmonic.

A recent anechoic dataset: de la Torre, de la Prida and Azpicueta-Ruiz (Forum Acusticum 2026, manuscript not reviewed) recorded 13 professional trumpeters on three trumpets (including a Bach Stradivarius 43 and a Yamaha 6335) at piano and forte, and computed level, spectral centroid, energy above 2 kHz, loudness and sharpness; the mean spectral centroid of the group with a "modern" background was about 330 Hz higher at forte and about 93 Hz higher at piano than the classical group.

### 4.2 Brassiness: shock waves in the cylindrical bore

Mechanism. Beauchamp (1969) proposed that nonlinear propagation in the air column contributes to spectral enrichment in trombones; Hirschberg, Gilbert, Msallam and Wijnands (1996) gave the first conclusive measurement, showing a nearly shock-like pressure front at the end of a trombone slide at fortissimo, and Gilbert and Petiot (1997) confirmed it, with upper harmonics extending beyond 40 kHz (Campbell and Myers 2023, review). Pandya, Settles and Miller (2003) photographed shock waves leaving a trumpet bell with a large schlieren system, mainly for loud high notes; the shocks were weak, near the visibility limit at 118 to 124 dB(A) peak, at the note frequency, and the authors attribute them to cumulative nonlinear propagation inside the bore.

Shock distance. For a sinusoidal input in a lossless cylinder, the distance to shock formation equals 2 gamma times atmospheric pressure times sound speed, divided by gamma plus 1 times the maximum rate of rise of the input pressure (Hirschberg et al. 1996, as given by Harrison-Harsley 2018, equation 7.1). My arithmetic, with gamma 1.4, 101.3 kPa and 343 m/s: a shock forms within 1.0 m of cylinder when the mouthpiece pressure rises at about 41 million Pa per second, and within 0.5 m at about 81 million Pa per second. For a 466 Hz sine the first figure corresponds to a pressure amplitude of about 14 kPa (my arithmetic); real mouthpiece waveforms have a steeper rise than a sine of the same amplitude, which is why the rate of rise and not the amplitude is used.

Why the flugelhorn is less brassy. As a wave travels into an expanding bore its particle velocity falls, so the nonlinear steepening per metre falls (Pyle 2010). Pyle defines the brassiness potential B as the ratio of the length of a cylinder that gives the same total nonlinear distortion to the equivalent cone length of the instrument, the length of a complete cone whose lowest resonance equals the instrument's nominal fundamental. Written out from his equation 4 (my restatement): B equals the integral, from the input to the bell, of the input diameter divided by the local diameter, divided by the equivalent cone length. B lies between 0 and 1; it is higher for narrow instruments such as the trumpet and trombone and lower for wide instruments such as the flugelhorn and euphonium (Pyle 2010). Myers et al. (2012) tested B against spectral centroid with players, loudspeaker excitation and simulations, found a trade-off between bore size and B, and proposed plotting instruments in the plane of these two quantities. Pyle's Figure 6 plots B from 0.5 to 0.8 against minimum bore from 5 to 12 mm for trumpets, cornets and flugelhorns; I could not read individual values from it. Campbell, Gilbert and Myers (2021) report that trumpets and flugelhorns occupy distinct regions, with the bright trumpets at higher B than the mellow flugelhorns, a large range within each family, and a correlation with minimum bore diameter that is clearest among the trumpets, which suggests that trumpet timbre depends also on absolute bore size (search excerpt only).

My arithmetic on the OpenWInD trumpet profile, integrating from the start of the leadpipe at 83.7 mm to the bell, with an equivalent cone length of 1.472 m (343 m/s divided by twice 116.54 Hz): B is 0.56 with an input diameter of 8.40 mm (end of backbore) and 0.61 with 9.28 mm (start of leadpipe). Both are inside the range plotted by Pyle.

Radial scale. B does not change when all diameters are scaled. Campbell and Myers (2023) describe a newer spectral enrichment parameter E that adds the input diameter, from an assumption of constant input acoustic power, with the input plane taken 20 mm into the tube to allow for the mouthpiece shank. With a constant of 88 mm chosen for taxonomy, E for 1370 measured instruments ranges from 1.9 to 9.8, and musical subclasses form distinct but overlapping clusters. The equations are images in the PDF and did not survive text extraction; the paper is open access (CC BY 3.0).

Moore (2016) states that conical instruments such as the cornet, flugelhorn and tuba cannot produce the classic brassy sound of cylindrical instruments. The B and E work shows the difference as one of degree along a continuous scale, not presence or absence (my inference from the cited B and E results).

Player control. Norman, Chick, Campbell, Myers and Gilbert (2010, Acta Acustica united with Acustica 96, 614 to 621) studied player control of brassiness at intermediate dynamic levels (title only; not read). Pyle (2010) masked his player's hearing because skilled players change timbre deliberately.

Nonlinear propagation also affects the transient. In a time-domain model of a 1.4 m, 7 mm radius cylinder with a one-mass lip model, Berjamin, Lombard, Vergez and Cottanceau (2015 preprint) found that including nonlinear propagation shortened the attack (0.18 s against 0.35 s) and raised the playing frequency by up to 45 Hz early in the attack, with only 3 cents difference once steady.

## 5. Lips and player parameters for trumpet playing

### 5.1 How the lips behave

Lip valves support oscillation only in a narrow band near their own natural frequency, unlike woodwind reeds, so the brass player must set the lip natural frequency precisely (Chen, Smith and Wolfe 2012, citing Fletcher and Rossing). Adachi and Sato (1996) modelled the lips with two degrees of freedom (swinging and stretching) and found oscillations on both sides of the impedance peaks in modes 1 to 3 and only below the peaks in modes 4 and higher, consistent with a transition from outward-striking behaviour at mode 2 to lateral (sliding-door) behaviour at higher modes, which they compare with simultaneous measurements of mouthpiece pressure and lip motion. Fréour, Masuda, Cochelin and Vergez (2023) state that a one-degree-of-freedom outward-striking lip model tends to play sharp, and moved to a two-degree-of-freedom model to match measured pitch on two trumpets.

Fletcher and Tarnopolsky (1999) give a simple trumpet lip theory: threshold mouth pressure equals a constant K times the playing frequency, with K about 7 Pa per Hz for the professional and 8 and 4 Pa per Hz for the amateurs, explained by lip vibrating mass falling as tension rises (from Elliott and Bowsher 1982); the lip opening is roughly sinusoidal with amplitude about equal to the mean opening; its slope with pressure is about 0.1 micrometre per Pa for a high note; the opening saturates at not much more than 1 mm; and the opening area scales with the opening to the power 1.5, with a coefficient of about 0.05 square-root metre. They take the input resistance at a resonance as of order 100 million Pa s per cubic metre.

### 5.2 Parameter values used in trumpet models

| Source | Lip model | Parameter values | Pressure | Sample rate |
|---|---|---|---|---|
| Adachi and Sato 1995/1996, Table I | 2 degrees of freedom | mass equals 1.5 divided by the product of (2 pi) squared and the lip frequency, in kg; stiffness equals 1.5 times lip frequency N/m; Q 3.0 open, 0.5 closed; lip breadth 7 mm; thickness 2 mm; mouthpiece entry area 2.3 square cm; lip frequency 60 to 700 Hz | 2.0 to 5.0 kPa for modes 1 to 6 at moderate level | 8 kHz, checked at 48 kHz |
| Kaburagi et al. 2011 (Adachi and Sato lips) | 2 degrees of freedom | lip frequency 220 to 1000 Hz | 3.0 kPa at 220 to 260 Hz rising to 16 kPa at 1000 Hz (their Table I) | 64 kHz |
| Harrison-Harsley 2018, Table 6.1 (NESS) | 1 degree of freedom, outward | effective area 1.46e-5 square m; mass 5.37e-5 kg; damping 5; rest opening 0.29 mm; width 10 mm; lip frequency 400 to 1000 Hz | 2.5 to 5.0 kPa | 44.1 or 50 kHz |
| OpenWInD default LIPS | 1 degree of freedom, outward | opening 0.1 mm; mass 8e-5 kg; area 4e-5 square m; 382 Hz; damping 0.3 times angular frequency; width 8 mm | 5.5 kPa | user set |
| OpenWInD LIPS_SCALED, commented as from Fréour et al. JASA 2020 | 1 degree of freedom, outward, dimensionless | zeta 0.16; kappa 0.04; Q 3.33; angular frequency 2400 per s (382 Hz); closing pressure 1152 Pa | gamma 4.8 | user set |
| Berjamin et al. 2015 (cylinder, not a trumpet) | 1 mass | mass 1.78e-4 kg; stiffness 1278.8 N/m; width 10 mm; area 1 square cm; rest opening 0.5 mm | 20 kPa | variable step, resampled to 44.1 kHz |
| D'haes and Rodet 2002 (IRCAM jMax model) | 1 mass with collision | controls: mouth pressure, tube-length parameter, lip damping, lip resonance frequency | 5000 Pa in their example | 32 kHz |

Adachi and Sato's Table III gives, for the optimum oscillation in each mode, impedance peak, lip eigenfrequency and sounding frequency: mode 2, 232, 250, 236 Hz; mode 3, 341, 370, 342 Hz; mode 4, 457, 490, 453 Hz; mode 5, 572, 580, 563 Hz; mode 6, 686, 680, 673 Hz. In their model the sounding frequency is below the lip eigenfrequency in every mode, above the impedance peak in modes 2 and 3 and below it in modes 4 to 6. Harrison-Harsley (2018) states that the NESS trumpet values were found by trial and error, and maps the region of lip frequency (400 to 1100 Hz) and mouth pressure (2 to 6 kPa) in which the Smith-Watkins trumpet bore gives a note that settles within 0.07 s (his Figure 6.12). Berjamin et al. state that their values came from publications and trial and error until self-oscillation occurred.

Fitted parameters. Fréour, Guillot, Masuda, Usa, Tominaga, Tohgi, Vergez and Cochelin (2020, JASA 148, 748 to 758) applied numerical continuation to a brass model to compare trumpets; Fréour, Guillot, Masuda, Vergez and Cochelin (2022, Acta Acustica 6, article 9, open access) identified lip parameters by constrained continuation, with constraints from measurements on a trumpet player; Fréour et al. (2023) used a two-degree-of-freedom lip model and constraints from one player's crescendo on B-flat 4 (about 466 Hz) on two trumpets, relaxing a dimensionless lip-opening parameter zeta and the outward lip natural frequency. Along the crescendo, zeta varied by 0.267 and 0.094 and the lip natural frequency by 31.0 Hz and 25.6 Hz on the two trumpets; above 1.6 kPa the lip frequency varied by 5.7 Hz and 14.9 Hz, and the player reported more difficulty holding pitch on the trumpet with the larger figure. I read only the 2023 paper; the 2020 and 2022 papers were blocked.

Lip motion observations. Hruška, Švejda and Guštar (2015, Vienna Talk) filmed trumpet lip opening through a glass mouthpiece at 12000 frames per second with mouth pressure, on F4 and B-flat 4, and report initial mouth-pressure steps of under 1 kPa in one attack type and about 2 kPa in another; areas were not converted to square millimetres. Further lip studies listed in reference lists but not read: Yoshikawa (1995), Copley and Strong (1996, trombone), Richards (2003, Edinburgh thesis on an artificial mouth), Chick, Bromage and Campbell (2005), Kemp, Bilbao, McMaster and Smith (2013, wave separation in a trumpet under playing conditions, compared with finite-difference simulation).

### 5.3 Range

Written and sounding. The B-flat trumpet and flugelhorn are transposing instruments sounding a major second below written pitch: written C5 sounds B-flat 4, 466 Hz (Chen, Smith and Wolfe 2012). The lowest open note in normal use is written C4, sounding B-flat 3, 233 Hz; the pedal written C3 may also be playable (Wolfe, UNSW page). With all three valves the lowest normal note is written F-sharp 3, sounding E3, about 165 Hz (my arithmetic: six semitones below written C4). The top is player dependent; Chen, Smith and Wolfe describe written G6, sounding F6 at 1397 Hz, as a very high note, and their high-register specialists played above the range measurable with the probe in place. Unverified: orchestral writing rarely goes above written C6 (sounding B-flat 5, 932 Hz). Rose and Holloway (2012) state that trumpet, cornet and flugelhorn have the same tubing length and tessitura. A flugelhorn fourth valve, lowering a perfect fourth, extends the bottom to written C-sharp 3 when combined with the other three (my arithmetic, ignoring compensation). Unverified: flugelhorn parts usually stay below about written G5 to C6 because the upper register is less secure.

## 6. Physical models of the trumpet and flugelhorn

### 6.1 Reflection-function and waveguide models

Elliott and Bowsher (1982) set out the lip, flow and bore model from which most later trumpet models are derived (cited by Vergez and Rodet 2001; not read).

Adachi and Sato (1995, JASA 97, 3850 to 3861; 1996, JASA 99, 1200 to 1209). The bore is represented by a causal reflection function computed from an impedance calculated from a real trumpet's dimensions; the lips by the two-degree-of-freedom model above; forward Euler at 8 kHz. They obtained stable self-excited tones in modes 1 to 6 and a "loose-lipping" tone at 166 Hz, and compare simulated lip trajectories with measured ones.

Vergez and Rodet, IRCAM (ISMA 1997; ICMC 1997; Acustica 2000; ICMC 2000; Journal of Signal Processing 2000; ICMC 2001; Vergez thesis 2000). One mass with spring and damper; Bernoulli flow; bore by a time-domain reflection function derived from an input impedance measured in an anechoic room. Improvements: time-varying stiffness and damping to represent progressive closure; a lip length that depends on the opening through a hyperbolic tangent, so lips close from the corners, which removes a discontinuity in the flow derivative and much of an unwanted high-frequency component in the sound; a flow law that blends Poiseuille flow at small openings with Bernoulli flow at large openings, with two player-adjustable parameters that change the softness of the sound; and a nonlinear propagation algorithm derived from the simple-wave (lossless Burgers) equation with a constraint that allows shocks (patented). Simulating the whole bore as nonlinear cylinders needed too many cylinders for real time, so they used a hybrid: the inner dynamics from the measured reflection function and the radiated sound computed through the nonlinear propagation algorithm, which they describe as perceptually convincing and cheaper. Real-time control used a Yamaha WX7 or WX5 MIDI wind controller and three foot pedals for mouth pressure, lip stiffness, damping and valve positions, and a sensor-equipped real trumpet (the "meta-trumpet") measuring valve positions, mouthpiece pressure and mouthpiece pressing force as a proxy for lip tension. They report "very realistic sonic results" and that a composer (M. Lanza) used it in a 2001 piece. Hélie, Vergez, Lévine and Rodet (ICMC 1999) and D'haes and Rodet (ICMC 2001; DAFx 2002) inverted the model to estimate control parameters from recordings; D'haes and Rodet found that constraining the tube length to the seven valve lengths gave much better resynthesis than letting it vary continuously. Their DAFx 2002 implementation reduces the measured reflection function to a direct mouthpiece response, a run of zeros for the cylinder delay, and a second reflection, with the zero count set by the tube-length parameter. Vergez and Tisserand (2006, Lecture Notes in Computer Science 3902, 24 to 33) describe "The Brass project, from physical models to virtual musical instruments: playability issues" (title only). Unverified: this project is the origin of the Arturia Brass software instrument.

Federico and Borin (ICMC 1997) "Synthesis of the trumpet tone based on physical models" (title only, from Vergez and Rodet's references).

Cook (ICMC 1991, pp. 297 to 299) "TBone: an interactive waveguide brass instrument synthesis workbench for the NeXT machine" (title only), and Cook (ICMC 1992) on a meta-wind-instrument model and controller. The Synthesis ToolKit Brass class is described as "a simple brass instrument waveguide model, a la Cook (TBone, HosePlayer)" with controls for lip tension, slide length, vibrato and volume (STK documentation). The Faust physmodels library provides brassLips, brassModel and ready-to-use brass_ui functions, a generic brass model without pistons that the documentation describes as hard to control with poor parameters; there is no trumpet-specific function.

### 6.2 Finite-difference models

Bilbao and Chick (2013) developed an interleaved finite-difference scheme for the bore with viscothermal losses (fractional-derivative approximation of order 20) and radiation, validated on trumpet, trombone and horn impedances (Section 3.1). Harrison, Bilbao and Perry (DAFx 2015) and Harrison, Bilbao, Perry and Wishart (Computer Music Journal, 2015, DOI 10.1162/COMJ_a_00332) built the NESS valved-brass environment on it, with one-degree-of-freedom outward lips, partially open valves, and user-defined bores. Run times for 1 s of sound at 44.1 kHz on an Intel Core i5-4300U:

| Instrument | MATLAB (s) | C (s) | C with AVX vectorisation (s) |
|---|---|---|---|
| Trumpet, 1.3 m, 1 valve | 14.02 | 1.97 | 0.857 |
| Horn, 4.5 m, 3 valves | 27.89 | 6.12 | 3.63 |
| Custom, 6 m, 3 valves | not given | 19.22 | 7.95 |

GPU execution was 4.7 times slower than serial C for this one-dimensional problem. The dominant cost is the fractional-derivative loss filters. Short default tubes force a coarse grid locally, limiting bandwidth in those sections to about 16 kHz. The authors state that trumpet-sized instruments run in real time on modern laptops, and list nonlinear propagation as future work because shocks need new numerical methods. Harrison-Harsley (2018, thesis) adds a three-dimensional bell embedding that matched measured trumpet peaks above the fifth resonance to within 1 percent, against about 2 percent for a lumped radiation impedance, at more than 200 times the cost (1271.6 s against 5.7 s for 1 s at 100 kHz), and compares three nonlinear propagation models. Online files (instrument and score files, binaries) were at www2.ph.ed.ac.uk/~s0916351/ and the NESS site; the NESS brass page now offers sound examples (trumpet with moving valves, trumpet phrase, 10 m trumpet) and a link to releases (I did not check whether the binaries and instrument files still download).

Berjamin, Lombard, Vergez and Cottanceau (arXiv 1511.04247, 2015; later published in Acta Acustica united with Acustica, unverified details) coupled a one-mass lip model to the Menguy-Gilbert nonlinear propagation model with viscothermal memory variables and a total-variation-diminishing scheme; in Scilab it took 18 minutes per second of sound on a 2011 Core i7 laptop.

### 6.3 Frequency-domain and optimisation models

Gilbert, Menguy and Campbell (2008, JASA 123, 1854 to 1857) give a frequency-domain tool based on a generalised Burgers equation in non-uniform ducts for predicting brassiness from bore profiles. Tournemenne, Petiot, Talgorn, Gilbert and Kokkolaras (2019, JASA according to an aggregator; venue unverified) optimised a B-flat trumpet bore using harmonic-balance sound simulation with varied embouchure parameters and a mesh adaptive direct search; a later study by the same group led to a leadpipe prototype manufactured by Yamaha (search excerpt only). Tournemenne and Chabassier (2019, Acta Acustica united with Acustica, DOI 10.3813/AAA.919364) compared a one-dimensional finite-element method with the transfer-matrix method for wind-instrument impedance, using a lossy trumpet as the realistic case; their scripts are in OpenWInD.

OpenWInD (INRIA, GPL-3.0, gitlab.inria.fr/openwind/openwind) computes input impedance by one-dimensional finite elements, runs time-domain simulations with energy-consistent finite differences coupled to a reed or lip model, and reconstructs bores. It ships the trumpet geometries in Section 1.1 and a valve example.

The Acoustic Research Tool (ART, artool.sourceforge.net, GPL-3 or later) is an impedance and time-domain simulation framework started by Braden (2006 Edinburgh thesis) with Kausel and Geyer (mdw Vienna); Kausel (2001, Journal of New Music Research 30, 69 to 82) reconstructed a virtual trumpet bore with up to 100 design variables from simulated impedance by optimisation.

### 6.4 Commercial instruments

Audio Modeling's SWAM Solo Brass bundle lists Trumpet in B-flat, Trumpet in C, Piccolo Trumpet, Flugelhorn and Flugelhorn in E-flat, among others, and states that the instruments are "powered by physical modeling"; the page gives no technical description or CPU figures. Unverified: Yamaha's VL1 (1994) included trumpet voices from its waveguide-based Virtual Acoustic synthesis; Arturia Brass used the IRCAM trumpet model; Sample Modeling's "The Trumpet" is a sample-based instrument with modelled transitions rather than a physical model.

### 6.5 Flugelhorn models

No published physical model specific to the flugelhorn was found. Rose and Holloway (2012) is the only study found that models a flugelhorn's geometry acoustically, and it is linear and without lips.

## 7. Published and downloadable data

| Data | Instrument | Form | Where | Licence or access |
|---|---|---|---|---|
| Full bore profile including mouthpiece, 1.4187 m | B-flat trumpet (origin not stated) | text file of position and radius pairs | OpenWInD examples/frequential/Geom_trumpet_conical.txt | GPL-3.0 |
| Parametric simplified trumpet bores | trumpet | text files | OpenWInD examples/frequential/Geom_trumpet.txt, examples/Thibault-Chabassier_JASA2020/simplified-trumpet | GPL-3.0 |
| Default and fitted lip parameters | trumpet | Python dictionaries | OpenWInD openwind/technical/default_excitator_parameters.py | GPL-3.0 |
| Measured impedance extrema, 7 maxima and 7 minima | Smith-Watkins B-flat trumpet with Kelly Screamer | table | Bilbao and Chick 2013, Table I (author PDF on the NESS site) | free author copy |
| Measured bore and impedance curve | same trumpet | figures only | Bilbao and Chick 2013 Figures 7 and 9; Harrison-Harsley 2018 Figures 4.15 and 4.16 | free |
| Calculated impedance peaks and magnitudes, modes 1 to 8 | real B-flat trumpet | table | Adachi and Sato 1995 ATR report TR-H-0177, Table II | free |
| Calculated impedance peaks, modes 2 to 8, and bore radius figure | Yamaha YTR-2320E | text and figure | Kaburagi et al. 2011 (Kyushu University repository copy) | free |
| Measured impedance curve | B-flat trumpet | image | UNSW brass page, brass/brassZ/trumpet_Bb3.gif | free |
| Bell and mouthpiece profiles, bore diameters | trumpet, cornet, flugelhorn | figures and text | Rose and Holloway 2012 | free |
| Bore, bell and mouthpiece model specifications | Yamaha trumpets and flugelhorns | manufacturer tables | usa.yamaha.com product specification pages | free |
| Brassiness potential and spectral enrichment of many instruments | trumpets, flugelhorns and others | scatter plots | Pyle 2010; Myers et al. 2012; Campbell et al. 2021; Campbell and Myers 2023 | plots only |
| Trumpet sound examples from the NESS model | trumpet | MP3 and Ogg | ness.music.ed.ac.uk brass page | free |

## What we would need to build these two, and where to get it

1. A trumpet bore profile with the valve loops. The OpenWInD Geom_trumpet_conical.txt file gives a complete open-valve profile that can be used at once. Valve loop positions and lengths are not in it; the loop lengths can be set from the interval arithmetic in Section 1.3 (about 8.3, 17.1 and 26.5 cm for a 1.40 m instrument, with the first two made slightly long), placed in the cylindrical section between 0.30 and 0.80 m, and checked by computing the impedance of each of the seven valve combinations. A measured instrument would be better; Bilbao and Chick's Smith-Watkins profile exists only as a figure, so a request to the Edinburgh group (John Chick, Murray Campbell) for the data file is the direct route.

2. A flugelhorn bore profile. None is published as numbers. Options, in order of preference: request measured flugelhorn profiles from Arnold Myers or Murray Campbell (the brassiness and spectral enrichment studies include flugelhorns); measure an instrument (rod gauges, calipers and external diameters as in Bilbao and Chick, or acoustic pulse reflectometry); or construct a provisional profile from published facts: a short leadpipe and valve block at about 10.5 to 11 mm, followed by a gradual conical expansion over most of the remaining length into a 152 mm bell, adjusted until its computed impedance peaks fall on a harmonic series on B-flat 2 and its brassiness potential is below the trumpet's. The last option gives a plausible instrument, not a measured one.

3. Mouthpiece geometries. The OpenWInD file gives a trumpet mouthpiece (16.8 mm cup entry, 3.65 mm throat, backbore over 66 mm). A flugelhorn mouthpiece needs a deeper funnel cup with larger volume; Pyle's result that soft-level brightness scales with the square root of cup volume ratio gives a target, and the mdw study gives the expected intonation shifts (tens of cents). Measured cup volumes and profiles of a standard flugelhorn mouthpiece were not found and would have to be measured or requested.

4. Validation targets for the linear bore. For the trumpet: Bilbao and Chick's measured extrema (Section 3.1), Adachi and Sato's and Kaburagi's calculated peaks, and the alignment of modes 2 to 8 within about 15 to 20 cents of a harmonic series. A computed impedance from the chosen profile (OpenWInD or ART can produce it) should be checked against these before the lip model is attached. For the flugelhorn there is no external target; the check would be harmonic alignment and the qualitative expectation of a first resonance closer to the pedal (Section 3.5).

5. Bore representation for real time. Published options with reported cost: a reflection function or impulse response convolved per sample (Adachi and Sato; Vergez and Rodet, with the D'haes simplification to two reflections and a delay); finite differences on the full bore with fractional-derivative losses (NESS, 0.86 s per second of trumpet on one 2013 laptop core with AVX, so close to real time for one voice without nonlinear propagation); or a modal fit of the impedance (used in the Fréour continuation work, not timed). The MPX bowed strings already run waveguide and modal code; a modal or reflection-function bore with a valve-dependent set of seven precomputed responses is the cheapest route consistent with D'haes and Rodet's seven-length constraint (my inference). Smooth valve transitions need either crossfading between precomputed responses or a junction model such as Bilbao's.

6. Lip model and parameter ranges. A one-mass outward-striking model is the common starting point (Vergez and Rodet; NESS; OpenWInD), with the known tendency to play sharp (Fréour et al. 2023); Adachi and Sato's two-degree-of-freedom model reproduces the change of behaviour between low and high modes. Parameter ranges are in Section 5.2: lip frequency roughly 60 to 1000 Hz across the range, mouth pressure from about 1 kPa (soft, low) to 25 kPa (maximum measured), threshold roughly 4 to 8 Pa per Hz times the playing frequency, lip opening saturating near 1 mm. Vergez and Rodet's closure refinements (opening-dependent lip length, blended Poiseuille and Bernoulli flow) address audible high-frequency artefacts at closure and are cheap.

7. Nonlinear propagation for trumpet brassiness. Needed for the trumpet at forte and above; much weaker for the flugelhorn. Published real-time route: Vergez and Rodet's hybrid, in which the linear bore model sets the oscillation and a nonlinear simple-wave stage shapes the radiated sound; full in-bore nonlinear schemes (Berjamin et al.; Harrison-Harsley) are far from real time in the published implementations. Targets: the Fletcher and Tarnopolsky spectral slopes and their shift with level, Pyle's average-frequency curves, and the shock threshold arithmetic in Section 4.2.

8. Radiation. A lumped radiation impedance at the bell is adequate for the playing resonances; Harrison-Harsley shows it shifts peaks above the fifth by about 2 percent compared with a three-dimensional bell. The radiated spectrum needs the bell's high-pass transfer, rising about 6 dB per octave to a cut-off near 1 kHz for the trumpet (Fletcher and Tarnopolsky); the flugelhorn's larger bell will have a different cut-off, which would come from the computed transfer function of its profile.

9. Player control mapping. Mouth pressure, lip frequency (tension), lip damping and valve state are the controls in every published model; Vergez and Rodet used breath pressure and lip-tension proxies from a wind controller and a sensor trumpet. Note selection should pick a valve combination and a lip frequency near the target mode (Adachi and Sato's Table III gives lip frequency 1 to 8 percent above the sounding frequency in their model), consistent with D'haes and Rodet's constraint.

## Open questions

1. Flugelhorn impedance. No measured or computed flugelhorn input impedance was found. Where do its first resonance and its peak heights lie relative to the trumpet's, and how much does the conical section change the alignment of the upper modes?

2. Flugelhorn bore data. Are the Edinburgh flugelhorn bore profiles behind Myers et al. (2012) and Campbell and Myers (2023) available on request, and which models do they cover?

3. Trumpet versus flugelhorn mouthpiece in numbers. What are the cup volumes, throat diameters and backbore profiles of a standard trumpet and a standard flugelhorn mouthpiece, and how much of the flugelhorn's darker sound comes from the mouthpiece rather than the bore? Pyle (2010) and Rose and Holloway (2012) suggest the mouthpiece share is large; neither separates the two on a flugelhorn.

4. Does the square-root relation between cup volume and soft-level brightness (Pyle 2010) hold across mouthpieces, so that it can be used to set a model's mouthpiece without measurement?

5. Lip parameters by register on a real trumpet. The published sets are either trial-and-error simulation values or fitted for one note (B-flat 4) on one player. Fréour et al. (2022) contains constrained fits for a trumpet player; its parameter values were not read here because the full text was blocked.

6. Valve losses and valve-section geometry. How large are the extra losses and the impedance changes caused by valve bends and ports (Pyle 2020 excerpt; Bilbao and Chick note higher curvature through the valve section), and does a real-time model need them?

7. Is nonlinear propagation needed for the flugelhorn at all, given its lower brassiness potential, or is a static output shaping sufficient? A check would compare simulated spectral centroid against level for both bores with the same lip model.

8. Pedal notes and lip bends. How well do one-mass and two-mass lip models reproduce the trumpet pedal and bent notes, which depend on the higher resonances and on lip control away from a resonance?

9. Wall vibration. Moore (2016) states that an effect of bell vibration on sound has been demonstrated; its size for trumpet and flugelhorn bells, and whether it matters for synthesis, was not investigated here.

10. Real-time cost in MPX. The NESS figure (0.86 s per second, one voice, 2013 laptop core, no nonlinear propagation) bounds finite-difference cost; the cost of a reflection-function or modal bore with a lip model and a nonlinear output stage in our C++ needs to be measured.

## References

Reading status in brackets.

Adachi, S. and Sato, M. (1995). Time-domain simulation of sound production in the brass instrument. Journal of the Acoustical Society of America 97(6), 3850 to 3861. [Known through Harrison-Harsley 2018 reference list.]

Adachi, S. and Sato, M. (1995). Trumpet sound simulation using a two-dimensional lip vibration model. ATR Human Information Processing Research Laboratories technical report TR-H-0177, 11 December 1995. https://results.atr.jp/35th/techreport/TR-H-0177/TR-H-0177.pdf [Relevant sections and tables read.] Published as: Adachi, S. and Sato, M. (1996). Journal of the Acoustical Society of America 99(2), 1200 to 1209. DOI 10.1121/1.414601. [Abstract only; values taken from the report.]

Backus, J. (1976). Input impedance curves for the brass instruments. Journal of the Acoustical Society of America 60, 470 to 480. DOI 10.1121/1.381104. [Search excerpt only.]

Backus, J. and Hundley, T. C. (1971). Harmonic generation in the trumpet. Journal of the Acoustical Society of America 49(2), 509 to 519. [Known through Harrison-Harsley 2018.]

Beauchamp, J. W. (1969). Nonlinear characteristics of brass tones. Journal of the Acoustical Society of America 46, 98. [Known through Campbell and Myers 2023.]

Benade, A. H. (1976). Fundamentals of Musical Acoustics. Oxford University Press. [Not read; cited through Fletcher and Tarnopolsky 1999.]

Berjamin, H., Lombard, B., Vergez, C. and Cottanceau, E. (2015). Time-domain numerical modeling of brass instruments including nonlinear wave propagation, viscothermal losses, and lips vibration. arXiv 1511.04247. https://arxiv.org/pdf/1511.04247 [Relevant sections read.]

Bilbao, S. (2011). Modelling of brass instrument valves. Proceedings of DAFx 2011. [Title only, through Harrison et al. 2015.]

Bilbao, S. and Chick, J. (2013). Finite difference time domain simulation for the brass instrument bore. Journal of the Acoustical Society of America 134(5), 3860 to 3871. Author copy: https://www.ness.music.ed.ac.uk/wp-content/uploads/2013/08/bilbao_jasa_13_r2_double.pdf [Sections IV and V and Table I read.]

Bouhuys, A. (1965). Sound-power production in wind instruments. Journal of the Acoustical Society of America 37, 453 to 456. [Known through Fletcher and Tarnopolsky 1999.]

Braden, A. C. P. (2006). Bore optimisation and impedance modelling of brass musical instruments. PhD thesis, University of Edinburgh. https://era.ed.ac.uk/handle/1842/12591 [Record only.]

Campbell, D. M., Gilbert, J. and Myers, A. (2021). The Science of Brass Instruments. Springer, Cham. ISBN 978-3-030-55686-0, DOI 10.1007/978-3-030-55686-0. [Not read; access blocked.]

Campbell, M., Gilbert, J. and Myers, A. (2021). What's the difference? Predicting and characterising timbral distinctions between families of brass instruments. Proceedings of the Institute of Acoustics 43 Pt 1. [Search excerpts only; PDF link returned 404.]

Campbell, M. and Myers, A. (2023). The contributions of Joël Gilbert to the understanding of "brassiness". Forum Acusticum 2023, Turin, 353 to 357. DOI 10.61782/fa.2023.0481. https://dael.euracoustics.org/confs/fa2023/data/articles/000481.pdf [Read in full; equations lost in extraction.]

Caussé, R., Eveno, P., Gilbert, J. and Petiot, J.-F. (2013). What can we deduce from measured resonance frequencies of trumpets concerning playing frequencies. ICA 2013, Montreal. HAL hal-00951809. [Abstract only.]

Caussé, R., Kergomard, J. and Lurton, X. (1984). Input impedance of brass musical instruments: comparison between experiment and numerical models. Journal of the Acoustical Society of America 75, 241 to 254. DOI 10.1121/1.390402. [Search excerpt only.]

Chen, J.-M., Smith, J. and Wolfe, J. (2012). Do trumpet players tune resonances of the vocal tract? Journal of the Acoustical Society of America 131(1), 722 to 727. DOI 10.1121/1.3651241. https://www.phys.unsw.edu.au/jw/reprints/TrumpetTract.pdf [Methods and results sections read.]

Cook, P. R. (1991). TBone: an interactive waveguide brass instrument synthesis workbench for the NeXT machine. Proceedings of ICMC 1991, Montreal, 297 to 299. [Bibliographic record only.]

Cook, P. R. (1992). A meta-wind-instrument physical model, and a meta-controller for real time performance control. Proceedings of ICMC 1992, San Jose, 273 to 276. [Known through Vergez and Rodet 2001.]

Copley, D. C. and Strong, W. J. (1996). A stroboscopic study of lip vibrations in a trombone. Journal of the Acoustical Society of America 99, 1219 to 1226. [Known through Fletcher and Tarnopolsky 1999.]

de la Torre, J. L., de la Prida, D. and Azpicueta-Ruiz, L. A. (2026). Measurement and analysis of acoustic indicators across trumpet players specialised in different musical styles. Forum Acusticum 2026. https://dael.euracoustics.org/confs/fa2026/data/articles/000836.pdf [Results section skimmed.]

D'haes, W. and Rodet, X. (2002). Physical constraints for the control of a physical model of a trumpet. Proceedings of DAFx-02, Hamburg, 157 to 162. https://www.dafx.de/paper-archive/2002/DAFX02_DHaes_Rodet_trumpet_model.pdf [Sections 1 to 3 read.]

Elliott, S. J. and Bowsher, J. M. (1982). Regeneration in brass wind instruments. Journal of Sound and Vibration 83(2), 181 to 217. [Known through Vergez and Rodet 2001 and Fletcher and Tarnopolsky 1999.]

Elliott, S. J., Bowsher, J. M. and Watkinson, P. (1982). Input and transfer response of brass wind instruments. Journal of the Acoustical Society of America 72, 1747 to 1760. [Known through Fletcher and Tarnopolsky 1999.]

Eveno, P., Petiot, J.-F., Gilbert, J., Kieffer, B. and Caussé, R. (2014). The relationship between bore resonance frequencies and playing frequencies in trumpets. Acta Acustica united with Acustica 100(2). [Abstract only; author list from memory, unverified.]

Federico, R. D. and Borin, G. (1997). Synthesis of the trumpet tone based on physical models. Proceedings of ICMC 1997, Thessaloniki. [Known through Vergez and Rodet 2001.]

Fletcher, N. H. and Tarnopolsky, A. (1999). Blowing pressure, power, and spectrum in trumpet playing. Journal of the Acoustical Society of America 105(2), 874 to 881. https://www.phys.unsw.edu.au/music/people/publications/Fletcheretal1999.pdf [Read in full.]

Fréour, V., Guillot, L., Masuda, H., Usa, S., Tominaga, E., Tohgi, Y., Vergez, C. and Cochelin, B. (2020). Numerical continuation of a physical model of brass instruments: application to trumpet comparisons. Journal of the Acoustical Society of America 148(2), 748 to 758. DOI 10.1121/10.0001603. [Bibliographic record only; parameter values known through the OpenWInD source comment.]

Fréour, V., Guillot, L., Masuda, H., Vergez, C. and Cochelin, B. (2022). Parameter identification of a physical model of brass instruments by constrained continuation. Acta Acustica 6, 9. DOI 10.1051/aacus/2022004. [Abstract only; open access but download blocked.]

Fréour, V., Masuda, H., Cochelin, B. and Vergez, C. (2023). Identification of lip-parameters associated to different trumpets using constrained continuation. Forum Acusticum 2023, Turin, 383 to 385. DOI 10.61782/fa.2023.0279. https://dael.euracoustics.org/confs/fa2023/data/articles/000279.pdf [Read in full.]

Gilbert, J., Campbell, D. M., Myers, A. and Pyle, R. W. (2007). Differences between brass instruments arising from variations in brassiness due to non-linear propagation. Proceedings of ISMA 2007, Barcelona. [Known through Campbell and Myers 2023.]

Gilbert, J., Menguy, L. and Campbell, M. (2008). A simulation tool for brassiness studies. Journal of the Acoustical Society of America 123(4), 1854 to 1857. [Known through Harrison-Harsley 2018 and Berjamin et al. 2015.]

Gilbert, J. and Petiot, J.-F. (1997). Brass instruments: some theoretical and experimental results. Proceedings of the Institute of Acoustics 19(5), 391 to 400 (ISMA 1997). [Known through Campbell and Myers 2023.]

Harrison, R. L., Bilbao, S. and Perry, J. (2015). An algorithm for a valved brass instrument synthesis environment using finite-difference time-domain methods with performance optimisation. Proceedings of DAFx-15, Trondheim. https://www.ntnu.edu/documents/1001201110/1266017954/DAFx-15_submission_55.pdf/053b889b-1a15-4232-823b-13e3b3b4490f [Lip model, run times and conclusions read.]

Harrison, R. L., Bilbao, S., Perry, J. and Wishart, T. (2015). An environment for physical modeling of articulated brass instruments. Computer Music Journal, DOI 10.1162/COMJ_a_00332 (volume and issue given inconsistently by secondary sources; unverified). Accepted manuscript: https://www.pure.ed.ac.uk/ws/files/21585566/Harrison_etal_CMJ_2015_An_Articulated_Brass_Instrument_Synthesis.pdf [Tables 1 to 3 and conclusions read.]

Harrison-Harsley, R. L. (2018). Physical modelling of brass instruments using finite-difference time-domain methods. PhD thesis, University of Edinburgh. https://era.ed.ac.uk/handle/1842/31272 [Sections 4.3, 6.2 and 7.1 read.]

Hélie, T., Vergez, C., Lévine, J. and Rodet, X. (1999). Inversion of a physical model of trumpet. Proceedings of ICMC 1999, Beijing. [Known through Vergez and Rodet 2001.]

Herbert, T., Myers, A. and Wallace, J. (eds) (2019). The Cambridge Encyclopedia of Brass Instruments. Cambridge University Press. [Not read; cited through the Wikipedia article "Flugelhorn", read 13 October 2026.]

Hirschberg, A., Gilbert, J., Msallam, R. and Wijnands, A. P. J. (1996). Shock waves in trombones. Journal of the Acoustical Society of America 99(3), 1754 to 1758. [Known through Campbell and Myers 2023 and Harrison-Harsley 2018.]

Hruška, V., Švejda, M. and Guštar, M. (2015). Transients of trumpet tone: basic links between perception and measurements of lips opening surface and pressure in player's mouth. Proceedings of the Third Vienna Talk on Music Acoustics. [Skimmed.]

Kaburagi, T., Yamada, N., Fukui, T. and Minamiya, E. (2011). A methodological and preliminary study on the acoustic effect of a trumpet player's vocal tract. Journal of the Acoustical Society of America 130(1), 536 to 545. Repository copy: https://hdl.handle.net/2324/25703 [Simulation section read.]

Kausel, W. (2001). Optimization of brasswind instruments and its application in bore reconstruction. Journal of New Music Research 30, 69 to 82. [Search excerpt only.]

Kemp, J., Bilbao, S., McMaster, J. and Smith, R. (2013). Wave separation in the trumpet under playing conditions and comparison with time domain finite difference simulation. Journal of the Acoustical Society of America 134(2), 1395 to 1406. [Known through Harrison-Harsley 2018.]

Luce, D. and Clark, M. (1967). Physical correlates of brass-instrument tones. Journal of the Acoustical Society of America 42, 1232 onward (end page printed as 1343 in Fletcher and Tarnopolsky's list, probably 1243; unverified). [Known through Fletcher and Tarnopolsky 1999.]

Moore, T. R. (2016). The acoustics of brass musical instruments. Acoustics Today 12(4), 30 to 37. https://acousticstoday.org/wp-content/uploads/2021/07/Brass.pdf [Read in full.]

Myers, A., Pyle, R. W., Gilbert, J., Campbell, D. M., Chick, J. P. and Logie, S. (2012). Effects of nonlinear sound propagation on the characteristic timbres of brass instruments. Journal of the Acoustical Society of America 131(1), 678 to 688. DOI 10.1121/1.3651093. [Abstract only; full text blocked.]

Norman, L., Chick, J. P., Campbell, D. M., Myers, A. and Gilbert, J. (2010). Player control of "brassiness" at intermediate dynamic levels in brass instruments. Acta Acustica united with Acustica 96(4), 614 to 621. DOI 10.3813/AAA.918316. [Known through de la Torre et al. 2026.]

OpenWInD (INRIA). Open Wind Instrument Design, Python library, GPL-3.0. https://openwind.inria.fr and https://gitlab.inria.fr/openwind/openwind [Example geometry files and default excitator parameters read.]

Pandya, B. H., Settles, G. S. and Miller, J. D. (2003). Schlieren imaging of shock waves from a trumpet. Journal of the Acoustical Society of America 114(6), 3363 to 3367. [Abstract only.]

Pyle, R. W. (2010). The influence of mouthpiece cup shape on "brassiness". Proceedings of the Second Vienna Talk, 117 to 120. https://viennatalk.mdw.ac.at/papers/Pap_02_15_Pyle.pdf [Read in full.]

Pyle, R. W. (2020). Article in the Historic Brass Society Journal 2020 on the Bach trumpet. [Search excerpt only; title not confirmed.]

Rose, N. and Holloway, D. (2012). Finite element modeling of brass musical instruments. Proceedings of Acoustics 2012, Fremantle, Australian Acoustical Society. https://logosfoundation.org/instrum_gwr/bug/p60.pdf [Read in full.]

Synthesis ToolKit (STK), class Brass, documentation. https://ccrma.stanford.edu/software/stk/classstk_1_1Brass.html [Read.]

Faust libraries, physmodels.lib, brass functions. https://faustlibraries.grame.fr/libs/physmodels/ [Brass section read.]

Audio Modeling, SWAM Solo Brass bundle product page. https://audiomodeling.com/bundles/swam-solo-brass [Read.]

Tournemenne, R. and Chabassier, J. (2019). A comparison of a one-dimensional finite element method and the transfer matrix method for the computation of wind music instrument impedance. Acta Acustica united with Acustica, DOI 10.3813/AAA.919364. [Abstract only.]

Tournemenne, R., Petiot, J.-F., Talgorn, B., Gilbert, J. and Kokkolaras, M. (2019). Sound simulation-based design optimization of brass wind instruments. Journal of the Acoustical Society of America (venue from an aggregator; unverified). HAL hal-01963668. [Abstract only.]

Institut für Wiener Klangstil, mdw Vienna. Untersuchung zur Beeinflussung des Resonanzverhaltens von Trompeten durch unterschiedliche Mundstücksformen. Web report, https://www.mdw.ac.at/iwk/?PageId=87 [Read; authors and year not given.]

Vergez, C. (2000). Trompette et trompettiste: un système dynamique non linéaire analysé, modélisé et simulé dans un contexte musical. PhD thesis, Université Paris 6. [Known through Vergez and Rodet 2001.]

Vergez, C. and Rodet, X. (1997). Model of the trumpet functioning: real time simulation and experiments with an artificial mouth. Proceedings of ISMA 1997, Edinburgh. HAL hal-01105466. [Abstract only.]

Vergez, C. and Rodet, X. (1997). Comparison of real trumpet playing, latex model of lips and computer model. Proceedings of ICMC 1997, Thessaloniki, 180 to 187. HAL hal-01105512. [Abstract only.]

Vergez, C. and Rodet, X. (2000). Dynamical systems and physical models of trumpet-like instruments: analytical study and asymptotical properties. Acta Acustica united with Acustica 86, 147 to 162. [Known through Harrison-Harsley 2018.]

Vergez, C. and Rodet, X. (2000). New algorithm for nonlinear propagation of a sound wave: application to a physical model of a trumpet. Journal of Signal Processing 4(1), 79 to 87. HAL hal-01105576. [Known through Vergez and Rodet 2001.]

Vergez, C. and Rodet, X. (2001). Trumpet and trumpet player: model and simulation in a musical context. Proceedings of ICMC 2001, Havana. https://dept-info.labri.fr/~mrobine/TMJV/4.Synthese_Realiste/biblio/ICMC01VergezRodet.pdf [Read in full.]

Vergez, C. and Tisserand, P. (2006). The Brass project, from physical models to virtual musical instruments: playability issues. In Kronland-Martinet, Voinier and Ystad (eds), Computer Music Modeling and Retrieval, Lecture Notes in Computer Science 3902, Springer, 24 to 33. [Known through Harrison-Harsley 2018.]

Wolfe, J. Brass instrument (lip reed) acoustics: an introduction. UNSW music acoustics web page. https://newt.phys.unsw.edu.au/jw/brassacoustics.html [Read 13 October 2026.]

Yamaha Corporation. Specification pages for YTR-8335 trumpet, YFH-631G and YFH-8310Z flugelhorns. https://usa.yamaha.com/products/musical_instruments/winds/ [Read 13 October 2026.]

Yoshikawa, S. (1995). Acoustical behavior of brass player's lips. Journal of the Acoustical Society of America 97, 1929 to 1939. [Known through Fletcher and Tarnopolsky 1999.]
