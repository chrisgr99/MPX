# Trombone: geometry, acoustics and physical models (literature survey)

Compiled 13 October 2026 as part 3 of the brass survey for MPX (tenor trombone mainly, with notes on the bass trombone and the F attachment). Parts 1, 2, 4 and 5 cover the general physics of brass, trumpet and flugelhorn, French horn, and the player model with existing implementations; this part repeats general material only where the trombone adds something specific.

Each statement is either stated by a cited source or marked as my inference or my arithmetic. The reference list at the end gives the reading status of every source: read in full, read in part, abstract only, or known only through another source's reference list or a search summary. Statements from memory that I could not confirm are marked unverified. The shared web-search budget for this session ran out before the survey was complete; the gaps this left are listed under Open questions.

## Summary

The tenor trombone is a B flat instrument whose air column is about 2.6 to 2.75 m long with the slide closed (two measured instruments) and about 1.06 m longer with the slide fully out. Most of that length is cylindrical tubing of 11.4 to 13.9 mm bore in the slide, followed by a short gooseneck and tuning slide and a flared bell of about 0.5 m length and 0.19 to 0.22 m mouth diameter. The slide adds tubing in two parallel legs, so a slide displacement adds twice its length to the air column. Seven positions each lower the pitch by one semitone; the spacing between positions grows geometrically.

The best single open dataset for a model is the geometry table in Smyth and Scott (2011): lengths and radii of each tube section plus a fitted Bessel-horn bell. Willemsen and colleagues (2021) used this table for a real-time finite-difference trombone, and their C++ code is on GitHub under the MIT licence. The best published impedance data for a named tenor trombone are the measured and computed impedance extrema for a Conn 8H with a Denis Wick 5AL mouthpiece in Bilbao and Chick (2013); the UNSW group (Boutin, Fletcher, Smith, Wolfe, 2013 to 2020) published impedance curves for a Yamaha instrument in first and fourth positions together with lip motion, flows and pressures measured on ten players.

The trombone is the instrument in which shock-wave formation was first measured (Hirschberg, Gilbert, Msallam and Wijnands 1996). At fortissimo the wave leaving the mouthpiece steepens during its travel through the long cylindrical slide until it is close to a shock at the end of the slide, and the radiated spectrum gains strong upper harmonics. The same authors concluded that a synthesis model can ignore the nonlinearity in the feedback loop that sets up the oscillation and apply it only to the sound on its way to the listener. Later work quantified the dependence on bore shape and radial scale (brassiness parameter B, then spectral enrichment parameter E) and simulated the propagation in the frequency domain and the time domain.

Varying the slide continuously is simple in a digital waveguide (change the delay length) and difficult in a finite-difference scheme (the number of grid points has to change). For waveguides the published problems are the transients of recursive (allpass) fractional-delay interpolators when the delay changes, and the pitch shift of interpolated delay lines during rapid changes. For finite-difference schemes the dynamic-grid method of Willemsen, Bilbao, Ducceschi and Serafin (2021, 2022) adds and removes grid points at sub-audio rates; the authors report no audible artefacts in informal listening, but no formal test has been published.

Players sound slightly above the bore resonance (on average 1.1 percent above the impedance peak, Boutin et al. 2015) and can bend a note continuously from about a tone below to about half a semitone above its normal pitch (Boutin et al. 2020a). The pedal note B flat 1 does not coincide with any resonance: its second and higher harmonics fall near the second and higher resonances. A one-degree-of-freedom outward-striking lip model coupled to a modal or finite-difference bore reproduces the natural notes and the pedal note; published parameter sets for that model are listed in the section on lips.

## 1. Bore geometry

### 1.1 Sections and dimensions

A trombone air column consists of the mouthpiece, the leadpipe inside the inner slide, the two inner slide tubes, the two outer slide tubes that telescope over them, the slide crook (bow) joining the outer tubes, the gooseneck, the tuning slide and the bell section. Bilbao and Chick (2013) give the overall tube length of a Conn 8H tenor trombone in first position as approximately 2.74 m, measured with the mouthpiece in place (stated).

Smyth and Scott (2011, Table 2) give the following dimensions for the trombone they measured. The make and model are not named in the parts of the paper I read. Willemsen et al. (2021, Table 1) reproduce the same table, with the bell length 50.2 cm.

| Section | Length (cm) | Radius (cm) |
|---|---|---|
| Inner slide, top | 70.8 | 0.69 |
| Outer slide, top, extended part | 53 | 0.72 |
| Slide crook | 17.7 | 0.74 |
| Outer slide, bottom, extended part | 53 | 0.72 |
| Inner slide, bottom | 71.1 | 0.69 |
| Gooseneck | 24.1 | 0.71 |
| Tuning slide | 25.4 | 0.75 rising to 1.07 |
| Bell flare | 50.2 (Table 1) or 56.7 (Table 2) | 1.0 rising to 10.8 |

The bell length differs between the two tables of the same paper: 0.502 m in Table 1 (the Bessel-horn fit) and 56.7 cm in Table 2. Willemsen et al. use 0.502 m. Smyth and Scott state effective bore lengths of 209.1 cm retracted and 315.1 cm extended; by my arithmetic these are the sums of the non-bell sections (70.8 + 17.7 + 71.1 + 24.1 + 25.4 = 209.1 cm, plus 2 x 53 = 106 cm when extended), so the bell is excluded from those figures. Willemsen et al. give the total tube length range as 2.593 to 3.653 m, which is the same sum with the 50.2 cm bell added. The ratio 3.653 / 2.593 is 1.409, which corresponds to 5.9 equal-tempered semitones (my arithmetic); the nominal range of the slide is six semitones (a tritone).

Smyth and Scott fit the bell with a Bessel horn, radius a(x) = b (x + x0) to the power minus gamma, with x measured from the bell mouth. Their parameters (Table 1): bell length 0.502 m, mouth radius 0.108 m, small-end radius 0.01 m, flare constant gamma 0.7, x0 = 0.0174 m, b = 0.0063. They model the bell in one dimension as eight conical frustums from this profile.

Msallam and Dubois (1999, revised 2002) treat the slide for nonlinear-propagation purposes as a one-dimensional pipe about 1.5 m long with radius 7 mm (stated). The Smyth slide radii (6.9 mm inner, 7.2 mm outer) correspond to bore diameters of 13.8 and 14.4 mm, which by my arithmetic is close to the 0.547 inch (13.9 mm) large-bore tenor size.

Typical bore and bell sizes, from Wikipedia (secondary source, not checked against makers' specifications): tenor slide bores from 0.450 inch (11.4 mm, small bore) to 0.547 inch (13.9 mm, large or orchestral bore), with 0.500, 0.508, 0.525 and 0.547 inch common; dual-bore slides have a slightly larger bore in the second leg, for example 0.547 to 0.562 inch. Tenor bells are 7 to 9 inches (18 to 23 cm) in diameter, most between 7.5 and 8.5 inches. Bass trombone slides are 0.562 inch (14.3 mm), dual-bore 0.562 to 0.578 inch, with bells of 9.5 to 10.5 inches.

Campbell and Myers (2023) note that the mouthpiece shank typically penetrates about 20 mm into the leadpipe, and they take the input plane of the instrument 20 mm from the receiver for bore-based calculations (stated).

### 1.2 The seven positions

Each slide position lowers every resonance by about one equal-tempered semitone, so first to seventh position spans six semitones (Wikipedia; stated as standard practice). Because each semitone multiplies the effective length by 2 to the power 1/12 (1.0595), the extra tubing needed grows geometrically and the gaps between positions widen toward seventh position.

By my arithmetic, using an effective acoustic length in first position of 2.93 m (the speed of sound 343 m/s divided by twice 58.5 Hz, the mean spacing of the measured Conn 8H resonances in Section 3.1), the slide displacements from first position are about 8.7 cm (second), 18.0 cm (third), 27.7 cm (fourth), 38.1 cm (fifth), 49.1 cm (sixth) and 60.7 cm (seventh). These figures assume a uniform effective length for all resonances; the real effective length differs between resonances because of the bell and the mouthpiece, which is one reason players adjust positions slightly for different partials (my inference; not checked against a measurement).

Boutin et al. (2015) state that in their experiment fourth position was the slide extended by 22.7 cm from first position, on a Yamaha YBL 321 with the tuning slide 18 mm out. This is smaller than my estimate of 27.7 cm; the source of the difference is not resolved here (see Open questions).

### 1.3 How slide extension changes the resonances

Extending the slide lengthens only the cylindrical part of the bore. In a waveguide model with the bell reflection and transmission held fixed, only the propagation delay and the wall loss of the cylindrical section change (Smyth and Scott 2011, who make the delay length and the loss filter parametric in tube length and radius and keep the measured bell filters fixed). The resonance frequencies all fall by close to the same ratio, and the proportion of narrow cylindrical tubing in the bore rises, which raises the potential for nonlinear spectral enrichment (Myers, Campbell, Gilbert and Pyle 2010 address this for chromatic instruments: a slide or valve instrument is an assembly of tubes with different sounding lengths and bore profiles, whose timbres must stay consistent across a scale).

Boutin et al. (2015) measured the bore impedance in first and fourth positions and found that players played above the nearest impedance peak in both. The UNSW group also found that the magnitudes and quality factors of the impedance peaks fall with playing time while the instrument fills with warm, humid, CO2-rich breath, and that the resonance frequencies first fall (CO2) and then rise slightly above the ambient value (temperature and humidity) (Boutin, Smith and Wolfe 2020b).

### 1.4 F attachment and bass trombone valves

The F attachment is a valve, operated by the left thumb, that switches in additional tubing to lower the instrument from B flat to F, a perfect fourth. A fourth requires one third more length (ratio 4/3, compared with 1.3348 for five equal-tempered semitones, my arithmetic). Wikipedia states that the attachment adds about 0.9 m (3 feet) of tubing, that with it engaged the positions are about one third longer so only six fit on the slide, reaching C2, and that pulling the attachment tuning slide to E gives B natural 1. It names the rotary valve as the most common type, with the Thayer axial-flow valve and the Hagmann valve on professional instruments, and gives an attachment tubing bore of 0.562 inch against 0.547 inch for an orchestral slide (secondary source).

Bass trombones add a second valve, either dependent (used together with the F valve to give D or E flat) or independent (G flat alone, D together) (Wikipedia, secondary). Mattéoli et al. (2021) used the measured input impedance of an A. Courtois Legend AC502 bass trombone in their stability study.

For modelling the attachment, the finite-difference valve model of Bilbao (2011, DAFx) handles time-varying valves with a bypass and a valve tube joined at a junction, and it was extended into the NESS brass environment (Harrison, Bilbao, Perry and Wishart 2015). I found no paper specific to the acoustics of the trombone F-attachment valve (rotary, Thayer or Hagmann) within the search budget; see Open questions.

## 2. The slide as a continuously variable length

### 2.1 Digital waveguides and delay lines

In a waveguide model the slide is a delay line whose length is changed during playing. Willemsen et al. (2021) note that this was used in real-time synthesis for simplified bore profiles, citing Cook (2002).

Cook's TBone (ICMC 1991) was an interactive waveguide brass synthesis workbench on the NeXT computer; I have only its bibliographic record. The Synthesis ToolKit class Brass, by Cook and Scavone and described in its source as "a la Cook (TBone, HosePlayer)", shows the structure of that family of models (source code read). It uses one allpass-interpolated delay line (class DelayA) for the whole bore, a two-pole resonant filter as the lip (resonance coefficient 0.997, tuned to the lip frequency), the square of the filtered pressure difference as the lip opening, clipped at 1, a scattering junction that mixes mouth pressure and bore pressure in proportion to that opening, a reflection coefficient of 0.85 on the returning bore pressure, and a DC blocker. The slide control scales the delay between 0.5 and 1.5 times the value set for the note. There is no bell filter, no wall-loss filter and no nonlinear propagation.

Smyth and Scott (EURASIP 2011, SMC 2011, ICA 2013) build a more detailed waveguide trombone: a delay of M samples for the cylindrical bore, a parametric wall-loss filter from Abel, Smyth and Smith (2003) that depends on tube length and radius and can change in real time, measured bell reflection and transmission filters, a lumped mouthpiece and a generalized pressure-controlled valve configured as an outward-striking lip. Changing the slide changes M and the loss filter; the bell filters stay fixed. Their papers do not describe the interpolation used when M is fractional or varying, in the parts I read.

Known problems with time-varying fractional delays, from the general waveguide literature:

- Allpass (Thiran) fractional-delay interpolators are recursive, so an abrupt change in the delay produces a transient. A transient-elimination scheme for time-varying allpass fractional-delay filters in waveguide models was presented at ICMC 1995 (title confirmed; the author is probably Välimäki but I did not confirm it), and Välimäki and Laakso (1998, IEEE Transactions on Signal Processing) published a general method for suppressing transients in variable recursive digital filters. The STK Brass class uses an allpass-interpolated delay for the slide, so it is subject to this problem (my inference from the code).
- FIR Lagrange interpolators have no internal state and therefore no switching transient, but they low-pass the signal for fractional delays near one half (general property; from memory, not re-verified here).
- Rocchesso (2000) describes the fractionally addressed delay line, which uses a single fractional pointer for reading and writing. A linear change in the length of an interpolated FIR delay line produces an immediate Doppler pitch shift, while the fractionally addressed line produces a steady pitch shift after a transient (from a search summary of the related DAFx 1998 paper; abstract of the 2000 paper read).

A physical point that the papers I read do not address: in a real slide the length changes at the slide speed, so the travelling waves see a moving boundary. A slide moving at speed v shifts the frequency of a reflected wave by a fraction of order v/c, which for 1 m/s is about 0.3 percent or 5 cents (my arithmetic, not from a source). Moving the slide also changes the enclosed air volume and draws or pushes a small steady flow. I found no study that measured either effect in a trombone.

### 2.2 Finite-difference schemes with a changing number of grid points

In a finite-difference time-domain (FDTD) scheme the grid spacing is tied to the time step by the Courant condition, so a tube whose length changes needs a changing number of grid points. Willemsen, Bilbao, Ducceschi and Serafin (DAFx 2021, companion paper) list the earlier options: a fixed grid chosen for a conservative stability condition (which introduces numerical dispersion and band-limiting), or full-grid interpolation between configurations (which needs very high sample rates to avoid audible artefacts and low-pass effects, and was therefore offline only).

Their dynamic-grid method, applied to the trombone in the second DAFx 2021 paper, works as follows (read in full):

- The air column is the linear lossless Webster system in pressure and particle velocity, on interleaved grids, with an unflanged-pipe radiation condition (Levine and Schwinger, discretised after Silva et al. 2009). Viscothermal losses and nonlinear propagation are left out.
- The tube is split into two subsystems in the middle of the slide crook. The left one runs from the mouthpiece and the right one ends at the bell. As the length changes, both subsystem lengths change by half the length change, and the right-hand grid moves with the bell end.
- When the total length is not an integer number of grid spacings, a virtual grid point beyond each inner boundary is computed by quadratic Lagrange interpolation, using the fractional part of the number of intervals. When a grid point is added, its value is computed by cubic Lagrange interpolation; when one is removed, it is dropped. Additions and removals alternate between the left and right subsystems.
- The velocity field of the right subsystem drifts because it has no boundary condition at the joint; the drift does not affect the pressure output, and an offset is applied when a velocity point is added. A damped spring-like correction term pulls the two inner-boundary pressures together just before a point is removed.
- The rate of length change is limited to one twentieth of a grid spacing per sample, so a grid point can be added or removed at most every 20 samples. At 44.1 kHz the whole slide range can still be traversed in about 0.06 s, and the authors report that the control does not feel delayed.
- The Courant number is set to 0.999 instead of 1, which the authors say tolerates much faster length changes before instability, with no perceptual cost.
- The output is the pressure at the radiating end, low-passed by a fourth-order Butterworth filter at about 3245 Hz to stand in for the distributed radiating area of the bell.

The authors report average CPU loads of 4.3 percent (shortest tube), 5.2 percent (longest tube) and 10.1 percent (rapid continuous slide and lip changes) without graphics, on a 2.2 GHz Intel i7 MacBook Pro, at 44.1 kHz. Informal listening by the authors found brass-like sound and no audible artefacts from adding and removing points, even with rapid length changes; they state that formal listening tests are still needed. They also report a non-smooth extra oscillation in the output at low lip frequencies, which they attribute to the lip collision model.

The companion paper tests the method on the one-dimensional wave equation. Between 15 and 16 grid intervals, the lowest mode deviates from its ideal frequency by at most minus 0.15 cents and the 15th mode by minus 67 cents; the deviations shrink as the number of grid points grows, and higher-order Lagrange interpolation did not reduce them substantially. The journal version is Willemsen et al. (2022) in the Journal of the Audio Engineering Society, which I saw only as a title page.

### 2.3 Glissando and the coupling between slide and lips

When the slide moves, the resonance that sustains the note moves with it, and the lips must follow. Willemsen et al. tie the lip resonance frequency to the tube length: it is a frequency inversely proportional to the current length, scaled by a fixed multiplier of 2.4 that they found heuristically to place the lip near the fourth resonance. Their glissando therefore moves the lip and the tube together. Players can hold a note on a moving resonance because they can sound anywhere from about a tone below to about half a semitone above a resonance peak without a discontinuity (Boutin et al. 2015, 2020a; see Section 5.2). The UNSW authors also note that good players can perform a lip glissando that crosses several resonances without using the slide.

Audio Modeling states that its SWAM trombones model the slide independently and support soft glissandos as well as glissandos with overtone jumps (vendor description, not independently checked).

### 2.4 Summary of reported artefacts and remedies

- Allpass fractional-delay transients when the delay changes: transient-elimination or transient-suppression methods (ICMC 1995; Välimäki and Laakso 1998), or use FIR interpolation (general literature).
- Doppler-like pitch shift in interpolated delay lines during fast length changes (Rocchesso 2000).
- FDTD grid changes: dispersion with a fixed conservative grid, low-pass and cost with full-grid interpolation; the dynamic grid reduces both but introduces small modal frequency deviations that grow with mode number and shrink with grid size (Willemsen et al. 2021).
- FDTD instability at fast length changes: rate limit and a Courant number slightly below 1 (Willemsen et al. 2021).

## 3. Mouthpiece and input impedance

### 3.1 Measured impedance of named tenor trombones

Bilbao and Chick (2013) measured the input impedance of a Conn 8H Artist Symphony tenor trombone with a Denis Wick 5AL mouthpiece, slide in first position, with the BIAS system in an anechoic room at 19 to 20 degrees C. They measured the bore profile with graded rod gauges for the bell, internal dial calipers where accessible, and external vernier measurements minus a nominal wall thickness elsewhere (estimated accuracy plus or minus 5 percent), and computed the impedance with an FDTD scheme at 88.2 kHz with a 20th-order approximation of the viscothermal loss term. The measured maxima (their Table I) are:

| Peak | Measured (Hz) | Computed (Hz) |
|---|---|---|
| 1 | 38.3 | 38.2 |
| 2 | 113.2 | 111.5 |
| 3 | 174.0 | 171.7 |
| 4 | 233.2 | 230.1 |
| 5 | 296.0 | 293.8 |
| 6 | 350.9 | 353.4 |
| 7 | 410.1 | 408.6 |

Most computed extrema are within about 1 percent in frequency and within a decibel in magnitude of the measurement. The authors identify the lowest peak as the weakest point of the time-domain loss model. The bore profile is shown in their Figure 7 on a logarithmic radius axis but is not tabulated.

By my arithmetic from these measured peaks, relative to equal-tempered A 440 pitches: peak 2 is 50 cents below B flat 2, peak 3 is 6 cents below F3, peak 4 is 1 cent above B flat 3, peak 5 is 14 cents above D4, peak 6 is 8 cents above F4, and peak 7 is 22 cents below A flat 4. Peaks 2 to 7 divided by their harmonic numbers give 56.6, 58.0, 58.3, 59.2, 58.5 and 58.6 Hz, so the upper resonances lie close to a harmonic series on about 58.5 Hz (B flat 1 is 58.27 Hz), while peak 1 at 38 Hz does not belong to it.

Boutin, Fletcher, Smith and Wolfe (2015) measured a Yamaha YBL 321 with a B flat to F trigger (left in B flat throughout), main slide in first position and extended by 22.7 cm for fourth position, tuning slide 18 mm out. They measured the impedance with an impedance head sealed to the rim of a modified transparent mouthpiece, calibrated on a 142 m long, 7.8 mm diameter duct, immediately after playing. The first seven peaks of the bore impedance have magnitudes between 17 and 26 MPa s per cubic metre. The curves are plotted (their Figures 2 and 4) but not tabulated.

Mattéoli et al. (2021) give the measured impedance of an A. Courtois Legend AC502 bass trombone with a 15-mode modal fit (plotted; the modal parameters may be in the published version, which I did not check).

Older impedance and transfer measurements that include the trombone: Elliott, Bowsher and Watkinson (1982) measured the input impedance and transfer function of a trumpet and a trombone (as described by Beauchamp 2012). Backus (1976) and Caussé, Kergomard and Lurton (1984) are cited by Boutin et al. as impedance measurements of brass instruments; I did not confirm which trombones they include.

Braden (2006, PhD, Edinburgh) and Braden, Newton and Campbell (2009, JASA) optimised trombone bores to target impedance curves; I have only the bibliographic records, so I cannot say whether they publish bore tables.

### 3.2 The bell measured separately

Smyth and Scott (2011) measured the trombone bell's reflection and transmission functions by attaching the detached bell to a 2 m long, 2 cm diameter PVC tube closed by a loudspeaker, with one microphone in the tube wall next to the speaker and one 7 cm outside the open end on axis, using a 20 s logarithmic sweep (Farina's method) at 24 bit and 44.1 kHz. They validated the method on open and stepped cylinders against theory. The measured bell functions agree in shape with their eight-frustum one-dimensional model but not in detail, which they attribute to higher-order and evanescent modes that a one-dimensional model omits. They state that the measured filters are approximately minimum phase.

### 3.3 Mouthpiece

Smyth and Scott (2013) model the mouthpiece as a lumped circuit: the cup volume as an acoustic compliance (volume divided by air density times the square of the speed of sound), the backbore constriction as a series inertance (air density times length divided by area) with a series resistance for losses taken from Dietz (1988). Their example values are a cup volume of 5 cubic centimetres, a constriction radius of 4.5 mm and a constriction length printed as 48 cm, which I take to be a misprint for a length of a few centimetres (unverified). Bilbao and Chick (2013) instead include the mouthpiece in the measured bore profile of the FDTD scheme and note that at lower sample rates the cup volume is resolved too coarsely; they suggest variable grid spacing.

### 3.4 Resonance alignment and the playing frequency

Boutin et al. (2015) found that for 85 percent of samples from all players, and 88 percent from professionals, the playing frequency was above the nearest impedance peak, by 1.1 percent on average (about 19 cents, my conversion). At the playing frequency the bore load is therefore usually compliant. Applying their 1.1 percent offset to the Conn 8H peaks of Section 3.1 gives playing pitches of about 31 cents below B flat 2, 13, 20, 33 and 27 cents above F3, B flat 3, D4 and F4, and 3 cents below A flat 4 (my arithmetic, combining two different instruments, so only indicative).

### 3.5 Pedal tones

The pedal note B flat 1 (about 58 Hz) has no impedance peak under it: the lowest peak is near 38 Hz. Boutin, Smith and Wolfe (2020a) state that experienced players sound the pedal note because its second and higher harmonics fall close to the second and higher resonances, while the 38 Hz resonance is hard to play because its harmonics do not coincide with resonances.

Velut, Vergez, Gilbert and Djahanbani (2017) show that linear stability analysis of a one-degree-of-freedom outward-striking lip coupled to a modal air column predicts the pedal note of a trombone, which earlier work had explained only with nonlinear arguments. Mattéoli et al. (2021) computed bifurcation diagrams for the same kind of model on a measured bass trombone and found that the pedal note's playing frequency is about a semitone lower than the linear threshold frequency, that only one stable branch exists for it, and that a lip opening area proportional to the square of the opening height (instead of proportional to the height) fits observations better for low regimes such as the pedal note (citing Bromage et al. 2010). Mattéoli et al. (2022) report that the extra "ghost note" between the pedal and the second regime occurs on bass instruments with predominantly expanding bores (tubas, euphoniums, saxhorns) and not on the predominantly cylindrical trombone.

Velut, Vergez and Gilbert (2017, mutes) report that a straight mute introduces a parasitic resonance that makes the pedal B flat hard to sustain, and that their simple physical model reproduces this.

## 4. Nonlinear propagation and brassiness

### 4.1 Shock waves in trombones (Hirschberg, Gilbert, Msallam and Wijnands 1996)

Beauchamp had suggested in 1969 that nonlinear propagation in the air column contributes to the spectral enrichment of loud trombone tones (Campbell and Myers 2023). Hirschberg, Gilbert, Msallam and Wijnands (JASA 99, 1754 to 1758, 1996) gave the first conclusive experimental confirmation, with Eindhoven and IRCAM collaborators. From the paper's abstract (read; the full text was behind a bot check): the brightness of loud trombone tones is attributed to the nonlinearity of propagation in the pipe; at fortissimo this produces shock waves, observed both in pressure measurements and in flow visualisation; and the nonlinearity is essential for the transfer of sound from the source to the listener but can be ignored in the model of how the pipe oscillation is generated. This last point means a synthesis model can keep a linear bore in the lip feedback loop and apply nonlinear steepening only to the outgoing wave on its way to the bell (my reading of the abstract; Mattéoli et al. 2021 state the related point that the harmonics generated by steepening are not reflected back to the player at the bell, citing Hirschberg et al.).

Gilbert and Petiot (1997) confirmed the result with pressure measurements at the mouthpiece and at the end of the slide of a trombone played fortissimo, showing an almost instantaneous pressure rise at the end of the slide; the radiated sound then contains significant harmonics above 40 kHz (Campbell and Myers 2023, who reproduce the traces). Kergomard and Dalmont (2023) summarise the finding as spectral enrichment produced by a shock in the cylindrical part of the instrument, much weaker in instruments with a conical entry.

Pressure levels: Elliott et al. (1982) reported trombone mouthpiece pressure levels above 165 dB SPL (as quoted by Beauchamp 2012). By my arithmetic 165 dB SPL is about 3.6 kPa rms. The UNSW measurements found that the alternating pressure in the mouthpiece is about 20 times that in the mouth, with a steady mouth pressure of 1.4 kPa in their example low note (Boutin et al. 2015).

Shock formation distance: in standard weakly nonlinear plane-wave theory, a sinusoid of pressure amplitude p and angular frequency omega forms a shock after a distance of air density times the cube of the speed of sound, divided by the nonlinearity coefficient (about 1.2 for air) times omega times p. For p = 5 kPa at 233 Hz this is about 5.5 m, longer than the slide (my arithmetic from textbook theory, not from a source read here). Real mouthpiece waveforms rise much faster than a sinusoid, so they shock sooner; Norman et al. (2010) found that the gradient of the wavefront formed in the mouthpiece is the quantity players change to control brassiness (see 4.2).

### 4.2 At what dynamic brassiness appears, and player control

Beauchamp (2012) measured trombone transfer functions (bell pressure 2 m on axis divided by mouthpiece pressure) under playing conditions at pp, mf and ff. Below the bell cutoff of about 1000 Hz the transfer function hardly depends on dynamic and agrees with a swept-sine measurement; above the cutoff, harmonics are typically 20 dB stronger relative to the mouthpiece input at ff than at mf.

Norman, Chick, Campbell, Myers and Gilbert (2010) state that the change to a brassy sound occurs between forte and fortissimo, that the dynamic at which it occurs is not clearly defined, and that some players can raise or lower brassiness at a constant dynamic by small embouchure changes. Their playing tests showed that these players change the gradient of the pressure wavefront formed in the mouthpiece, and simulations with weakly nonlinear shock theory using measured mouthpiece waveforms confirmed that this change accounts for the change in spectral enrichment (abstract read). Campbell et al. (2014) and Norman et al. (2010) are cited by Berjamin et al. for the point that nonlinear distortion contributes to timbre at moderate levels too, not only at fortissimo.

Stevenson, Campbell, Bromage, Chick and Gilbert (2009) measured lip open area against time at different dynamics in trombones and horns and found that the open-area behaviour does not change much when the instrument enters the brassy regime (abstract read), which supports propagation rather than the lips as the source of brassiness.

### 4.3 Effect of bore length and diameter

Pyle and Myers (2006) proposed expressing the effect of an expanding bore on the rate of spectral enrichment through a coordinate-stretching function of the bore diameter along the tube; weak nonlinear theory predicts enrichment roughly proportional to this function evaluated at the bell (Campbell and Myers 2023; the equation was not recoverable from the extracted text, so I describe it in words: as I understand it, it is the integral along the bore of the input diameter divided by the local diameter, which is unverified as to exact form). Gilbert, Campbell, Myers and Pyle (ISMA 2007) normalised it by the equivalent cone length to give the brassiness parameter B; ranking instruments by B agreed with ranking by the spectral centroid of played crescendos. Myers et al. (2012, JASA) is the main journal paper on this parameter.

B does not depend on the absolute radial scale, yet narrow-bore instruments are brighter than wide-bore instruments of the same profile at the same dynamic (Campbell et al. 2014; Myers et al. 2010). Campbell, Gilbert and Myers (CFA 2022, as reported by Campbell and Myers 2023) therefore defined a spectral enrichment parameter E that assumes constant input power, divides by the input diameter and by the effective length, and takes the input plane 20 mm inside the receiver. Tested against frequency-domain simulations of 34 measured instruments at constant 30 mW output, a constant of 2.2 mm gave reasonable agreement; for taxonomy they use 88 mm, which gives E values from 1.9 to 9.8 over 1370 instruments. Trumpets and trombones, with a high proportion of narrow tubing, show strong enrichment; baritones and euphoniums much less.

Viscothermal wall losses damp the high harmonics that steepening creates, and act most strongly in long narrow tubes. Chick, Logie, Campbell and Gilbert (2012) compared experiments on cylindrical tubes of trombone-like length and diameter with weakly nonlinear simulations to separate the two effects (abstract read).

Myers and Campbell (2023, Forum Acusticum) studied spectral enrichment in valve trombones (known only from a reference list).

### 4.4 How later work models it

- Msallam, Dequidt, Caussé and Tassart (1997 ISMA; 2000 Acta Acustica): time-domain physical model of the trombone including nonlinear propagation, applied to synthesis of loud tones; Thompson and Strong describe it as including wave steepening in the slide, treated as a single 3 m cylindrical tube (search summary of Thompson and Strong). I did not read the full text.
- Msallam and Dubois (1999, revised 2002): Euler equations coupled to a linear boundary-layer model, applied to the slide alone (1.5 m, 7 mm radius, absorbing outlet); with a four-harmonic input, nonlinear propagation created harmonics 5 and above and raised the fourth harmonic by 5 dB net of losses.
- Thompson and Strong (2001): frequency-domain model of the trombone as a series of cylinders (152, according to Beauchamp 2012), with steepening applied in each cylinder to the outgoing wave and a measured mouthpiece spectrum as input; including steepening substantially reduced the error between predicted and measured far-field spectra in all cases.
- Menguy and Gilbert (2000) and Gilbert, Menguy and Campbell (2008): generalised Burgers equation with losses and a diameter term, solved in the frequency domain; Maugeais and Gilbert (2017): a time-domain version.
- Vergez and Rodet (2000): an algorithm for nonlinear propagation applied to a real-time trumpet model; Hélie and Smet (2008): weakly nonlinear propagation in a straight pipe as a real-time brassy audio effect (both known from Berjamin et al.'s references).
- Berjamin, Lombard, Vergez and Cottanceau (preprint 2015, Acta Acustica 2017): Menguy-Gilbert model for both travelling waves, a TVD scheme for the nonlinear part, memory variables for the fractional-derivative wall losses, a one-mass lip model. On a 1.4 m tube of 7 mm radius with blowing pressure ramped down from 20 kPa, nonlinear propagation raised the spectral centroid up to threefold, shortened the attack from 0.35 to 0.18 s, shifted the playing frequency by up to 157 cents during the transient, and lowered the oscillation threshold.
- Resch (2019, PhD, Waterloo): compressible Euler equations solved with a discontinuous Galerkin method for trumpet and trombone; the author states this is the first numerical reproduction, from measured data, of shock formation in the trombone.
- Bilbao (2011, Forum Acusticum) presented time-domain brass simulation including nonlinear effects (known from references). The NESS brass page states that a stability proof for its nonlinear propagation scheme had not been found, and Harrison et al. (2015) list nonlinear propagation and the slide as future extensions of their environment.

## 5. Lips and playing behaviour

### 5.1 Imaging and mechanical measurements

- Copley and Strong (1996): fibre-optic stroboscopy through a trombone mouthpiece, six notes at loud and soft dynamics, giving lip opening area and lip motion parallel and perpendicular to the flow (search summary of the paper).
- Yoshikawa (1995) and Yoshikawa and Muto (2003): lip behaviour and lip-wave measurements in brass players (known from references; Yoshikawa and Muto studied horn players).
- Newton, Campbell and Gilbert (2008): mechanical response of real and artificial brass players' lips; the lips show at least two resonances (as summarised by Mattéoli et al. 2021, who note that only the first corresponds to the outward-striking motion).
- Bromage, Campbell and Gilbert (2010): high-speed video through a transparent mouthpiece of several trombonists and an artificial mouth over many frequencies and levels, to determine the exponent relating lip open area to lip opening height (abstract read; the fitted values were behind a bot check). Mattéoli et al. (2021) summarise the outcome as area proportional to height being only partly supported, with area proportional to the square of height better for low regimes.
- Cullen, Gilbert and Campbell (2000): linear stability analysis compared with a trombone sounded by artificial lips; the artificial lips' mechanical response showed several resonances, each classifiable as inward or outward striking.
- Gilbert, Ponthus and Petiot (1998): artificial buzzing lips and brass instruments, experimental results (title only).
- Fréour and Scavone (2011, 2013) and Fréour, Lopes, Hélie, Caussé and Scavone (2015): an electrolabiograph in a trombone mouthpiece, measurements of the interaction between lips, air column and vocal tract in trombone playing, and an artificial trombone player with active control of an upstream resonance, which showed that the phase between upstream and downstream impedances changes the playing frequency.

### 5.2 UNSW measurements on players (Boutin, Fletcher, Smith and Wolfe)

Boutin et al. (2015) recorded ten players from beginner to professional on B flat 2 to F4 with mouthpiece and mouth pressure transducers, vocal-tract impedance during playing, bore impedance after playing, and high-speed video at 11025 frames per second (62 microsecond exposure) through a transparent mouthpiece. Their findings: the inertance of the air in the lip channel is usually negligible; for low notes the flow swept by the moving lips is a large part of the total flow into the mouthpiece (in one example 180 of 400 cubic centimetres per second peak to peak); a large negative mouthpiece pressure occurs while the lips are closed; the phase relations vary continuously across the range; and the lip motion is consistent with two-degree-of-freedom models of varying effective length. Players did not tune their vocal-tract resonances to the note in this range.

Lip bending: players could sound continuously from 12.1 percent below to 8.1 percent above the normal frequency of F3 and from 10.0 percent below to 8.9 percent above that of B flat 3 (Boutin et al. 2015). Boutin, Smith and Wolfe (2020a) state that players can lip up about half a semitone and lip down about a tone from the normal pitch, that the range is roughly symmetrical about the impedance peak rather than about the normal playing frequency, and that the limits occur where the bore impedance magnitude is about 15 percent of its peak value. Lipping up far ends in a jump to the next register; lipping down below B flat 2 ends with the sound ceasing, not with a jump. The work done by the pressure difference on the sweeping motion is about 20 times smaller than the work input to the instrument but of the same order as the peak kinetic energy of the lips, and may supply most or all of the energy that sustains the oscillation in some cases.

Boutin, Smith and Wolfe (ICSV 2019) add that lipping up is limited partly because the negative mouthpiece pressure must overlap with the lips being closed, and that further increases in lip stiffness produce jumps to the next impedance peak (abstract read). This is the mechanism of a lip slur between natural notes in the same position (my inference).

### 5.3 Published lip-model parameter sets for the trombone

- Willemsen et al. (2021), taken from Harrison-Harsley (2018): one-mass lip (upper lip moving, lower fixed), mass 53.7 mg, damping 5 per second, effective surface area 14.6 square millimetres, lip width 10 mm, equilibrium opening 0.29 mm, lip resonance 20 to 1000 Hz, mouth pressure 0 to 6000 Pa (3000 Pa in their tests), with a collision between the lips of stiffness 10000 newtons per metre and exponent 3. By my arithmetic the mass per unit area is about 3.7 kg per square metre.
- Velut et al. (2017) as used by Mattéoli et al. (2021): opening at rest 0.5 mm, width 12 mm, lip quality factor 7.0, inverse mass per unit area 0.11 square metres per kg (mass per unit area about 9.1 kg per square metre, my arithmetic), air density 1.2 kg per cubic metre, speed of sound 340 m/s; Mattéoli et al. also fit the rest opening as a function of lip frequency to data from Elliott and Bowsher (1982).
- Smyth and Scott (ICA 2013), generalized valve in blown-open configuration: exhaust radius 8 mm, valve width 2.3 mm, valve lengths 23.2 mm, mass 0.3 g, thickness 6 mm, initial displacement 0.01 mm, discretised with the bilinear transform; a flow equation with an inertive term for the channel.

The two published masses per unit area differ by a factor of about 2.5, and both groups state that lip parameters are hard to measure.

### 5.4 Lip slurs, slide changes and legato tonguing

I found no primary measurement study of trombone legato (slide changes with soft tonguing, or natural slurs where the slide motion is accompanied by a change of partial) within the search budget. What the papers above support: a slide movement without articulation produces a continuous glissando as long as the lips stay within the bending range of the moving resonance (Boutin et al. 2015, 2020a); a change of partial at a fixed slide position requires a change of lip stiffness large enough to jump to the next impedance peak (Boutin et al. 2019). Playing practice holds that a slide change during a partial change in the opposite direction gives a clean slur without tonguing, and that otherwise a light tongue stroke interrupts the glissando; this is teaching tradition, not verified in the literature here.

## 6. Spectra, timbre and radiation

Beauchamp (2012) gives the trombone transfer function from mouthpiece pressure to bell pressure 2 m on axis, from player measurements (Bach 36 Stradivarius with a 6.5 AL mouthpiece, 1968; a later digital set recorded in 2000) and from swept-sine measurements on a Holten tenor trombone (1972). It is a high-pass characteristic with alternating maxima and minima up to the bell cutoff of about 1000 Hz; the minima fall at the playing frequencies. Above the cutoff the response is nearly flat on axis and falls off at 45 and 90 degrees off axis. Below the cutoff the transfer function under playing conditions hardly depends on dynamic; above it, it rises by about 20 dB from mf to ff. For a lossless linear system he derives that the transfer function should be proportional to the square root of the real part of the input admittance.

Smyth and Scott (2011) give measured bell reflection and transmission magnitudes (plotted, 0.1 to 10 kHz). Willemsen et al. (2021) approximate the radiation filtering of the bell with a fourth-order low-pass at about 3245 Hz, derived from the bell mouth area. A hybrid one-dimensional bore with a three-dimensional bell simulated by FDTD and compared with measured radiation exists for brass instruments (Harrison, Hamilton and López-Carromero, Institute of Acoustics; title only, the PDF was not accessible). Directivity measurements of the trombone in orchestral-instrument directivity databases exist (for example Pätynen and Lokki 2010, from memory, unverified).

Because steepening at fortissimo produces significant harmonics above 40 kHz at the end of the slide (Campbell and Myers 2023), a synthesis model that includes nonlinear propagation will alias at 44.1 or 48 kHz unless it oversamples or band-limits the steepening (my inference).

The University of Iowa Musical Instrument Samples include tenor and bass trombone chromatic scales recorded in an anechoic chamber at pp, mf and ff (pre-2012 set, 16 bit 44.1 kHz, Neumann KM 84; post-2012 set, 24 bit 96 kHz stereo), which the page states may be used for any project without restriction. These are usable as reference spectra across dynamics.

## 7. Physical models and real-time implementations

| Model | Method | Slide | Nonlinear propagation | Real-time and cost | Reported realism |
|---|---|---|---|---|---|
| Cook, TBone (ICMC 1991) | Waveguide, NeXT | Delay length | Not known | Interactive on NeXT | Not known (title only) |
| STK Brass (Cook and Scavone) | Single allpass-interpolated delay, resonant lip filter | Delay scaled 0.5 to 1.5 times | No | Trivial cost | Not evaluated |
| Msallam et al. 2000 (IRCAM) | Time-domain, steepening in the slide | Not known | Yes | Not known | Loud-tone synthesis (title) |
| Arturia Brass (2005, with IRCAM) | Physical models of trumpet, saxophone, trombone | Not known | Not known | Real-time plug-in | Vendor claim of near-indistinguishable sound |
| Smyth and Scott 2011 to 2013 | Waveguide, measured bell filters, parametric loss filter, generalized lip valve, lumped mouthpiece | Delay and loss filter parametric | No | Stated suitable for real time | Model outputs shown, no listening test |
| Bilbao and Chick 2013 | FDTD bore with viscothermal loss | Static | No | Offline at 88.2 kHz | Impedance within about 1 percent of measurement |
| Harrison et al. 2015 (NESS brass) | FDTD, valves, lip model | Not implemented (future work) | No | 1 s of 1.3 m trumpet in 0.86 s, 4.5 m horn in 3.6 s (AVX, Intel i5-4300U, 44.1 kHz) | Not evaluated formally |
| Willemsen et al. 2021 | FDTD with dynamic grid, lip with collision | Dynamic grid | No | 4 to 10 percent CPU at 44.1 kHz | Informal: brass-like, no audible grid artefacts |
| Berjamin et al. 2017 | Finite volume (TVD) and memory variables, one-mass lip | No | Yes | Not reported in parts read | Simulations only |
| Resch 2019 | Discontinuous Galerkin Euler | No | Yes, shocks | Offline | Compared with measured data |
| Audio Modeling SWAM Trombones | Vendor: waveguide-based physical modelling, no samples | Independent slide control, slide or valve mode | Not stated | Real-time plug-in | Vendor description only |

I could not verify within the search budget the details of the Yamaha VL1 trombone voices (1994) or Sample Modeling's trombone products. No formal listening test of any trombone physical model was found.

## 8. Practical data a modeller would need

- Bore geometry, open: Smyth and Scott (2011) section table and Bessel-horn bell parameters (Section 1.1), open access under CC BY; the same values are in the MIT-licensed cppBrass code (github.com/SilvinWillemsen/cppBrass, Trombone/Source/MainComponent.cpp).
- Bore geometry, plotted only: Bilbao and Chick (2013) Conn 8H with Denis Wick 5AL (Figure 7, logarithmic radius). Campbell, Gilbert and Myers hold bore measurements of 1370 instruments used for the E parameter; I found no public release.
- Impedance, tabulated: Bilbao and Chick (2013) Table I, measured and computed extrema for the Conn 8H in first position (Section 3.1).
- Impedance, plotted: Boutin et al. (2015) Yamaha YBL 321 in first and fourth positions; Mattéoli et al. (2021) Courtois AC502 bass trombone with a 15-mode fit; Elliott, Bowsher and Watkinson (1982).
- Bell reflection and transmission, plotted: Smyth and Scott (2011).
- Mouthpiece to bell transfer functions at pp, mf, ff: Beauchamp (2012), open access.
- Playing behaviour: playing frequency 1.1 percent above the peak, bending range about minus a tone to plus half a semitone, mouth pressure about 1.3 to 1.4 kPa for normal low notes, mouthpiece-to-mouth pressure ratio about 20, sweeping flow share (Boutin et al. 2015, 2020a).
- Lip parameter sets: Section 5.3.
- Recordings: University of Iowa Musical Instrument Samples, tenor and bass trombone, pp, mf, ff, anechoic.
- Software for computing impedance from a bore: OpenWInD (Inria, GPL-3, Python, one-dimensional finite elements with thermoviscous losses and several radiation conditions; an online interface at demo-openwind.inria.fr).
- FDTD reference code: cppBrass (Willemsen, MIT); NESS brass environment binaries (Harrison et al. 2015; download location given in the paper, licence not stated).

## What we would need to build it, and where to get it

- A bore description. Start from the Smyth and Scott section table with the 0.502 m Bessel-horn bell, and add a mouthpiece (cup volume and backbore from Smyth and Scott 2013, or a measured mouthpiece profile). Check the model's impedance peaks against the Conn 8H table of Bilbao and Chick (2013) and the curves of Boutin et al. (2015); OpenWInD can compute the impedance of the same profile independently. A measured profile of a real instrument would be better than either; the measurement methods are described in Bilbao and Chick (2013).
- A slide. Two published routes. Waveguide: make the cylindrical slide a variable delay with FIR (Lagrange) interpolation to avoid allpass switching transients, recompute the wall-loss filter at control rate from length and radius (Abel, Smyth and Smith 2003), and keep the bell filters fixed (Smyth and Scott). FDTD: the dynamic-grid method with a split at the slide crook, a rate limit of one twentieth of a grid spacing per sample and a Courant number of 0.999 (Willemsen et al. 2021; code available). Either route needs a decision on whether to model the Doppler and volume effects of a moving slide, for which no measurements were found.
- A bell and radiation. Measured reflection and transmission filters (Smyth and Scott method, needs a detachable bell and a 2 m test tube) or a one-dimensional piecewise-conical model; an on-axis radiation filter consistent with Beauchamp's transfer functions; directivity from a published database if spatial output is wanted.
- A lip model. A one-degree-of-freedom outward-striking lip with a collision term, using one of the parameter sets in Section 5.3, with an option for an area law proportional to the square of the opening for low notes. The control mapping must set the lip resonance relative to the current slide length so that the playing frequency is about 1 percent above the target resonance; Willemsen et al. used a fixed multiplier, which will not hold across partials. The sweeping flow component (Boutin et al.) would need a second degree of freedom.
- Nonlinear propagation. Following Hirschberg et al., apply steepening only to the outgoing wave in the cylindrical part and leave the feedback loop linear. Candidate methods: per-section steepening as in Thompson and Strong (2001, frequency domain, would need a time-domain equivalent), the real-time algorithms of Vergez and Rodet (2000) and Hélie and Smet (2008), or a Burgers-equation solver as in Maugeais and Gilbert (2017). Oversampling or band-limiting is needed because the steepened wave carries energy above 40 kHz. Calibrate against Beauchamp's 20 dB rise above cutoff from mf to ff.
- An F attachment and bass trombone valves. Switchable extra tubing of about one third of the open length, bore 0.562 inch, using a time-varying valve junction as in Bilbao (2011) and Harrison et al. (2015). No trombone-specific valve data were found.
- Validation targets. Impedance extrema (Bilbao and Chick), playing-frequency offset and bending range (Boutin et al.), threshold and pedal-note behaviour (Velut et al. 2017; Mattéoli et al. 2021), spectra at pp, mf, ff (Iowa recordings, Beauchamp 2012).

## Open questions

- Slide displacement per position: Boutin et al. report 22.7 cm for fourth position, while a uniform-length estimate gives about 27.7 cm. A direct measurement of a slide, or a check of how Boutin et al. measured the extension, would settle what the model's slide control should map to.
- The bell length in Smyth and Scott (50.2 cm in one table, 56.7 cm in the other) and the make of their trombone.
- Whether the Doppler shift and the volume change of a moving slide are audible or measurable. No study was found.
- Formal listening tests of slide artefacts in any model (waveguide or dynamic grid). None found.
- A stable real-time scheme for nonlinear propagation inside the time-varying bore, and how much of the brassy sound a feed-forward steepening filter reproduces compared with a full model.
- Trombone legato: no measurement study of soft-tongued slide changes or natural slurs was found within the search budget.
- Acoustics of the F-attachment valve (rotary, Thayer, Hagmann), its tight bends and the valve transition; bass trombone dependent and independent valve sets. Not covered because the search budget ran out; Félix, Dalmont and Nederveen on bends in wind-instrument air columns (JASA 2012, from memory, unverified) would be a starting point.
- The exact form of the brassiness parameter B and spectral enrichment parameter E: the equations were not recoverable from the extracted text and should be read in Myers et al. (2012) and Campbell, Gilbert and Myers (2021, book).
- Bromage et al.'s fitted area-height exponents, Copley and Strong's lip areas, Thompson and Strong's spectra and the full text of Hirschberg et al. were not read because of access limits.
- Directivity data for the trombone and the commercial models (Yamaha VL1, Sample Modeling) were not verified.

## References

Reading status: [full] read in full; [part] read in part; [abs] abstract or record only; [ref] known only from another source's reference list or a search summary; [code] source code read; [secondary] secondary or vendor source.

- Abel, J., Smyth, T., Smith, J. O. (2003). A simple, accurate wall loss filter for acoustic tubes. Proc. DAFx-03, London, 53 to 57. [ref]
- Arturia (2005). Press release: Arturia, in partnership with IRCAM, announces the release of BRASS. https://www.arturia.com/company/press/press-releases/arturiainpartnershipwithircamannouncesthereleaseofbrass [secondary]
- Audio Modeling. SWAM Trombones product descriptions, e.g. https://www.bestservice.com/swam_trombones.html [secondary]
- Backus, J. (1976). Input impedance curves for the brass instruments. J. Acoust. Soc. Am. 60(2), 470 to 480. [ref]
- Beauchamp, J. W. (2012). Trombone transfer functions: comparison between frequency-swept sine wave and human performer input. Archives of Acoustics 37(4), 447 to 454. DOI 10.2478/v10168-012-0056-x. Open access: https://www.journals.pan.pl/Content/101618/PDF/05_paper.pdf [part]
- Berjamin, H., Lombard, B., Vergez, C., Cottanceau, E. (2015 preprint; published in Acta Acustica united with Acustica, 2017). Time-domain numerical modeling of brass instruments including nonlinear wave propagation, viscothermal losses, and lips vibration. arXiv:1511.04247. [part]
- Bilbao, S. (2011). Modelling of brass instrument valves. Proc. DAFx-11, Paris. https://dafx.de/paper-archive/details/L_6rPtwCFNIgElNK7o_sNg [abs]
- Bilbao, S. (2011). Time-domain simulation of brass instruments. Proc. Forum Acusticum, Aalborg. [ref]
- Bilbao, S., Chick, J. (2013). Finite difference time domain simulation for the brass instrument bore. J. Acoust. Soc. Am. 134(6), 3860 to 3871. Preprint: https://www.ness.music.ed.ac.uk/wp-content/uploads/2013/08/bilbao_jasa_13_r2_double.pdf [part]
- Bilbao, S., Harrison, R. L. (2016). Passive time-domain numerical models of viscothermal wave propagation in acoustic tubes of variable cross section. J. Acoust. Soc. Am. 140, 728 to 740. [ref]
- Boutin, H., Fletcher, N., Smith, J., Wolfe, J. (2013). Lip motion, the playing frequency of the trombone and the upstream and downstream impedances. Proc. SMAC 2013, Stockholm. https://newt.phys.unsw.edu.au/jw/reprints/SMACtrombone.pdf [abs]
- Boutin, H., Fletcher, N., Smith, J., Wolfe, J. (2015). Relationships between pressure, flow, lip motion, and upstream and downstream impedances for the trombone. J. Acoust. Soc. Am. 137(3), 1195 to 1209. DOI 10.1121/1.4908236. Open copy: https://openresearch-repository.anu.edu.au/bitstream/1885/125644/1/Relationships%20between%20pressure.pdf [part]
- Boutin, H., Smith, J., Wolfe, J. (2019). The acoustics of the trombone: how do players lip up and down? Proc. ICSV26. HAL hal-02469969. [abs]
- Boutin, H., Smith, J., Wolfe, J. (2020a). Trombone lip mechanics with inertive and compliant loads ("lipping up and down"). J. Acoust. Soc. Am. 147(6), 4133 to 4144. DOI 10.1121/10.0001466. https://newt.phys.unsw.edu.au/jw/reprints/lipping.pdf [part]
- Boutin, H., Smith, J., Wolfe, J. (2020b). "Warming up" a wind instrument: the time-dependent effects of exhaled air on the resonances of a trombone. J. Acoust. Soc. Am., 1817 to 1823. DOI 10.1121/10.0002109. https://newt.phys.unsw.edu.au/jw/reprints/warming.pdf [abs]
- Braden, A. C. P. (2006). Bore optimisation and impedance modelling of brass musical instruments. PhD thesis, University of Edinburgh. [ref]
- Braden, A. C. P., Newton, M. J., Campbell, D. M. (2009). Trombone bore optimization based on input impedance targets. J. Acoust. Soc. Am. 125, 2404 to 2412. DOI 10.1121/1.3087423. [ref]
- Bromage, S., Campbell, D. M., Gilbert, J. (2010). Open areas of vibrating lips in trombone playing. Acta Acustica united with Acustica 96(4), 603 to 613. HAL hal-00474644. [abs]
- Campbell, D. M., Chick, J., Gilbert, J., Kemp, J., Myers, A., Newton, M. (2014). Spectral enrichment in brass instruments due to nonlinear sound propagation: a comparison of measurements and predictions. Proc. ISMA, Le Mans. [ref]
- Campbell, M., Gilbert, J., Myers, A. (2021). The Science of Brass Instruments. Springer, Cham. [ref]
- Campbell, M., Myers, A. (2023). The contributions of Joël Gilbert to the understanding of "brassiness". Proc. Forum Acusticum 2023, Turin. DOI 10.61782/fa.2023.0481. https://dael.euracoustics.org/confs/fa2023/data/articles/000481.pdf [full]
- Caussé, R., Kergomard, J., Lurton, X. (1984). Input impedance of brass musical instruments: comparison between experiment and numerical models. J. Acoust. Soc. Am. 75(1), 241 to 254. [ref]
- Chick, J., Logie, S., Campbell, M., Gilbert, J. (2012). Spectral enrichment and wall losses in trombones played at high dynamic levels. Proc. Acoustics 2012, Nantes. HAL hal-00811284. [abs]
- Cook, P. R. (1991). TBone: an interactive waveguide brass instrument synthesis workbench for the NeXT machine. Proc. ICMC 1991, Montreal, 297 to 299. [ref]
- Cook, P. R. (2002). Real Sound Synthesis for Interactive Applications. CRC Press. [ref]
- Cook, P. R., Scavone, G. P. Synthesis ToolKit (STK), class Brass. https://github.com/thestk/stk (src/Brass.cpp, include/Brass.h) [code]
- Copley, D. C., Strong, W. J. (1996). A stroboscopic study of lip vibrations in a trombone. J. Acoust. Soc. Am. 99(2), 1219 to 1226. [ref]
- Cullen, J., Gilbert, J., Campbell, D. M. (2000). Brass instruments: linear stability analysis and experiments with an artificial mouth. Acta Acustica united with Acustica 86(4), 704 to 724. HAL hal-00474993. [abs]
- Ducceschi, M., Bilbao, S., Willemsen, S., Serafin, S. (2021). Linearly-implicit schemes for collisions in musical acoustics based on energy quadratisation. J. Acoust. Soc. Am. 149, 3502 to 3516. [ref]
- Elliott, S. J., Bowsher, J. M. (1982). Regeneration in brass wind instruments. J. Sound Vib. 83(2), 181 to 217. [ref]
- Elliott, S. J., Bowsher, J. M., Watkinson, P. (1982). Input and transfer response of brass wind instruments. J. Acoust. Soc. Am. 72(6), 1747 to 1760. [ref]
- Ernoult, A., Cabaret, J., Chabassier, J. (2023). Openwind: a software to simulate wind instruments, as a tool for acoustic teachers. Proc. Forum Acusticum 2023. DOI 10.61782/fa.2023.0233. https://openwind.inria.fr [part]
- Fletcher, N. H. (1993). Autonomous vibration of simple pressure-controlled valves in gas flows. J. Acoust. Soc. Am. 93(4), 2172 to 2180. [ref]
- Fréour, V., Scavone, G. P. (2011). Development of an electrolabiograph embedded in a trombone mouthpiece for the study of lip oscillation mechanisms in brass instrument performance. Canadian Acoustics 39(3), 130 to 131. [part]
- Fréour, V., Scavone, G. P. (2013). Acoustical interaction between vibrating lips, downstream air column, and upstream airways in trombone performance. J. Acoust. Soc. Am. 134(5), 3887 to 3898. [ref]
- Fréour, V., Lopes, N., Hélie, T., Caussé, R., Scavone, G. P. (2015). In-vitro and numerical investigations of the influence of a vocal-tract resonance on lip auto-oscillations in trombone performance. Acta Acustica united with Acustica 101, 256 to 269. HAL hal-01277959. [abs]
- Gilbert, J., Campbell, M., Myers, A., Pyle, R. (2007). Differences between brass instruments arising from variations in brassiness due to non-linear propagation. Proc. ISMA 2007, Barcelona. [ref]
- Gilbert, J., Menguy, L., Campbell, M. (2008). A simulation tool for brassiness studies. J. Acoust. Soc. Am. 123, 1854 to 1857. Related ISNA 2008 paper: HAL hal-00475552. [abs]
- Gilbert, J., Petiot, J.-F. (1997). Brass instruments: some theoretical and experimental results. Proc. ISMA 1997, Proc. Institute of Acoustics 19(5), 391 to 400. [ref]
- Gilbert, J., Ponthus, S., Petiot, J.-F. (1998). Artificial buzzing lips and brass instruments: experimental results. J. Acoust. Soc. Am. 104, 1627 to 1632. HAL hal-01878544. [abs]
- Harrison, R. L., Bilbao, S., Perry, J., Wishart, T. (2015). An environment for physical modeling of articulated brass instruments. Computer Music Journal 39(4), 80 to 95. Accepted manuscript: https://www.pure.ed.ac.uk/ws/files/21585566/Harrison_etal_CMJ_2015_An_Articulated_Brass_Instrument_Synthesis.pdf [part]
- Harrison-Harsley, R. L. (2018). Physical modelling of brass instruments using finite-difference time-domain methods. PhD thesis, University of Edinburgh. https://era.ed.ac.uk/handle/1842/31272 [abs]
- Hélie, T., Smet, V. (2008). Simulation of the weakly nonlinear propagation in a straight pipe: application to a real-time brassy audio effect. Mediterranean Conference on Control and Automation, Ajaccio. [ref]
- Hirschberg, A., Gilbert, J., Msallam, R., Wijnands, A. P. J. (1996). Shock waves in trombones. J. Acoust. Soc. Am. 99(3), 1754 to 1758. DOI 10.1121/1.414698. HAL hal-01105563 (open PDF listed). [abs]
- Kergomard, J., Dalmont, J.-P. (2023). Tribute to Joël Gilbert. Proc. Forum Acusticum 2023. DOI 10.61782/fa.2023.0246. [full]
- Mattéoli, R., Gilbert, J., Vergez, C., Dalmont, J.-P., Maugeais, S., Terrien, S., Ablitzer, F. (2021). Minimal blowing pressure allowing periodic oscillations in a model of bass brass instruments. Acta Acustica 5, 57. DOI 10.1051/aacus/2021049. arXiv:2112.08751. [part]
- Mattéoli, R. et al. (2022). Diversity of ghost notes in tubas, euphoniums and saxhorns. Acta Acustica 6, 32. DOI 10.1051/aacus/2022026. [ref]
- Maugeais, S., Gilbert, J. (2017). Nonlinear acoustic propagation applied to brassiness studies, a new simulation tool in the time domain. Acta Acustica united with Acustica 103, 67 to 79. [ref]
- Menguy, L., Gilbert, J. (2000). Weakly non-linear gas oscillations in air-filled tubes; solutions and experiments. Acta Acustica united with Acustica 86, 798 to 810. [ref]
- Msallam, R., Dequidt, S., Caussé, R., Tassart, S. (2000). Physical model of the trombone including nonlinear effects. Application to the sound synthesis of loud tones. Acta Acustica united with Acustica 86(4), 725 to 736. [ref]
- Msallam, R., Dubois, F. (1999, revised 2002). Mathematical model for coupling a quasi-unidimensional perfect flow with an acoustic boundary layer. arXiv:1006.2991. [part]
- Myers, A., Campbell, M. (2023). Spectral enrichment in valve trombones. Proc. Forum Acusticum 2023. [ref]
- Myers, A., Campbell, M., Gilbert, J., Pyle, R. W. (2010). The brassiness potential of chromatic instruments. Proc. 10e Congrès Français d'Acoustique. HAL hal-00542865. [abs]
- Myers, A., Pyle, R. W., Gilbert, J., Campbell, D. M., Chick, J., Logie, S. (2012). Effects of nonlinear sound propagation on the characteristic timbres of brass instruments. J. Acoust. Soc. Am. 131, 678 to 688. [ref]
- Newton, M. J., Campbell, D. M., Gilbert, J. (2008). Mechanical response measurements of real and artificial brass players lips. J. Acoust. Soc. Am. 123(1), EL14 to EL20. HAL hal-00474652. [ref]
- Norman, L., Chick, J., Campbell, M., Myers, A., Gilbert, J. (2010). Player control of "brassiness" at intermediate dynamic levels in brass instruments. Acta Acustica united with Acustica 96(4), 614 to 621. HAL hal-00474648. [abs]
- Pyle, R. W., Myers, A. (2006). Scaling of brasswind instruments. J. Acoust. Soc. Am. 119(5), 3259 (abstract). [ref]
- Resch, J. (2019). Physical modelling and the associated acoustic behaviour of trumpets and trombones. PhD thesis, University of Waterloo. http://hdl.handle.net/10012/15186 [abs]
- Rocchesso, D. (2000). Fractionally-addressed delay lines. IEEE Trans. Speech and Audio Processing 8(6), 717 to 727. DOI 10.1109/89.876310. arXiv:cs/0005022. [abs]
- Smyth, T., Abel, J. S. (2009). Estimating waveguide model elements from acoustic tube measurements. Acta Acustica united with Acustica 95(6), 1093 to 1103. [ref]
- Smyth, T., Scott, F. S. (2011). Trombone synthesis by model and measurement. EURASIP Journal on Advances in Signal Processing 2011, 151436. DOI 10.1155/2011/151436. Open access, CC BY. PDF: https://trsmyth.ucsd.edu/pubs/eurasipPublished2011.pdf [part]
- Smyth, T., Scott, F. S. (2011). Parametric trombone synthesis by coupling dynamic lip valve and instrument models. Proc. SMC 2011. https://trsmyth.ucsd.edu/pubs/smc2011.pdf [part]
- Smyth, T., Scott, F. S. (2013). A trombone model emphasizing acoustic accuracy and playability. Proc. ICA 2013, Montreal. https://trsmyth.ucsd.edu/pubs/asaTrombone2013.pdf [full]
- Stevenson, S., Campbell, M., Bromage, S., Chick, J., Gilbert, J. (2009). Motion of the lips of brass players during extremely loud playing. J. Acoust. Soc. Am. 125, EL152 to EL157. HAL hal-00474634. [abs]
- Thompson, M. W., Strong, W. J. (2001). Inclusion of wave steepening in a frequency-domain model of trombone sound production. J. Acoust. Soc. Am. 110(1), 556 to 562. DOI 10.1121/1.1371759. [ref]
- University of Iowa Electronic Music Studios. Musical Instrument Samples. https://theremin.music.uiowa.edu/MIS.html [part]
- Välimäki, V. (probable author, unconfirmed) (1995). Elimination of transients in time-varying allpass fractional delay filters with applications to digital waveguide modeling. Proc. ICMC 1995. [ref]
- Välimäki, V., Laakso, T. I. (1998). Suppression of transients in variable recursive digital filters with a novel and efficient cancellation method. IEEE Trans. Signal Processing 46(12), 3408 to 3414. [ref]
- Velut, L., Vergez, C., Gilbert, J. (2016). Measurements and time-domain simulations of multiphonics in the trombone. J. Acoust. Soc. Am. 140 (HAL hal-01363547 lists page 2876; not checked). [ref]
- Velut, L., Vergez, C., Gilbert, J. (2017). Physical modelling of trombone mutes, the pedal note issue. Acta Acustica united with Acustica 103, 668 to 675. HAL hal-01385524. [abs]
- Velut, L., Vergez, C., Gilbert, J., Djahanbani, M. (2017). How well can linear stability analysis predict the behaviour of an outward-striking valve brass instrument model? Acta Acustica united with Acustica 103(1), 132 to 148. HAL hal-01245846. [abs]
- Wikipedia. Trombone. https://en.wikipedia.org/wiki/Trombone (read 13 October 2026) [secondary]
- Willemsen, S., Bilbao, S., Ducceschi, M., Serafin, S. (2021). A physical model of the trombone using dynamic grids for finite-difference schemes. Proc. DAFx20in21, vol. 2, 152 to 159. CC BY. https://vbn.aau.dk/ws/files/445882632/DAFx20in21_paper_25.pdf ; code https://github.com/SilvinWillemsen/cppBrass (MIT) [full]
- Willemsen, S., Bilbao, S., Ducceschi, M., Serafin, S. (2021). Dynamic grids for finite-difference schemes in musical instrument simulations. Proc. DAFx20in21. https://vbn.aau.dk/ws/files/445882228/DAFx20in21_paper_24.pdf [part]
- Willemsen, S., Bilbao, S., Ducceschi, M., Serafin, S. (2022). The dynamic grid (journal version of the DAFx dynamic-grid work; full title not confirmed). J. Audio Eng. Soc. 70(9), 650 to 660. DOI 10.17743/jaes.2022.0043. [ref]
- Yoshikawa, S. (1995). Acoustical behavior of brass player's lips. J. Acoust. Soc. Am. 97(3), 1929 to 1939. [ref]
- Yoshikawa, S., Muto, Y. (2003). Lip-wave generation in horn players and the estimation of lip-tissue elasticity. Acta Acustica united with Acustica 89, 145 to 162. [ref]
