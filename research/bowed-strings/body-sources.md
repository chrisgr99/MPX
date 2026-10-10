# Bowed strings: the body, verified sources

Gathered and checked against the papers on 10 October 2026 for phase 5 of docs/bowed-strings.md. "Read" means read off a figure, plus or minus one to two decibels; "derived" means worked out from published values; UNVERIFIED means no source was found.

I checked every number against the source PDF unless it is marked otherwise. Where a value is marked "read", I took it off a figure by eye, so allow about plus or minus 1 to 2 dB. "Derived" means I calculated it from published values. "UNVERIFIED" means I found no source.

**Conventions.** Q equals 1 divided by twice the damping ratio, so 1% of critical damping is Q 50. "Admittance" means the driving-point admittance at the bridge corner in the bowing direction. "Radiativity" means Bissinger's measure: the root-mean-square sound pressure per newton of hammer force at the bridge G corner, averaged over 266 microphones on a sphere of radius 1.2 m, with the violin hanging free in an anechoic room [B08a sec. II]. To refer it to 1 m, multiply by 1.2 (plus 1.6 dB). That conversion is derived and assumes far-field spreading.

### 1. Signature modes below 1 kHz

**Violin**

| Mode | Mean ± s.d. (min–max), 17 violins [B08a III.B.1] | Other ranges | Damping, 14-violin average [B12 sec. V] | Radiativity, Pa/N | Admittance peak, one violin (read, [W05] Fig. 2) |
|---|---|---|---|---|---|
| A0 | 275 ± 9 (253–282) | 270–290 [S3D]; 275–290 [W98] | 2.5% (Q 20) | 0.29 to 0.80 measured [B12 III.B.1]; about 0.37 average (read, [B08a] Fig. 2) | about −33 dB (0.022 m/(s·N)); A0 is weak in admittance [B08a Fig. 1 caption] |
| CBR | 407 ± 31 (363–452) | 380–440 [W02]; 380–420 [S3D] | UNVERIFIED | weak radiator, left out of the model in [B12] | about −22 dB (0.08) |
| B1− | 475 ± 16 (439–500); 470 in [B12] | 450–480 [W02]; 430–480 [S3D] | 1.22% (Q 41) | 1.01 | about −15.5 dB (0.17) |
| A1 | 469 ± 19 (430–494); A1/A0 = 1.71 ± 0.05 | 470–490 [W02] | 1.0% (Q 50) | very variable; equal to B1− in the Plowden violin [B12 III.B.2] | not separable |
| B1+ | 541 ± 22 (511–591) | 530–570 [W02]; 510–580 [S3D] | 1.15% (Q 43) | 0.95 | about −14.5 dB (0.19) |

- **Single-violin check (a Strad copy) [B12 III.A.3]:** B1− at 489 Hz with 1.2% damping and 0.72 Pa/N; B1+ at 515 Hz with 1.1% and 0.84 Pa/N; A0 at 275 Hz; A1 at 453 Hz.
- **Mechanism:** A0 radiation is driven by the B1 modes. It falls as the A0-to-B1− frequency gap grows [B08b III.A, Fig. 2; B12].
- **Other low modes:** the neck/fingerboard mode B0 sits at 240–340 Hz, near A0 [W98]. Assumed Q values in that paper are 25 for A0 and 80 for B0 (Fig. 3 caption). Tailpiece modes: three below 200 Hz and two between 300 and 800 Hz [W02, citing Stough].
- **Other admittance curves (read):**
  - [W14] Fig. 1, two violins: B1 peaks −16 to −20 dB; narrow hill peaks up to −14 dB; about −30 dB at 5 kHz; −40 to −42 dB at 10 kHz.
  - [ZW14] Fig. 1, one violin: B1 about −20 dB; hill about −19 dB at 2.2–2.5 kHz; −30 dB at 4 kHz.
- **Violin G-string wave impedance:** 0.34 N·s/m [W14 Fig. 19 caption].

**Cello**

- **A0:** 97 Hz in cello 1 of [ZWS16] (sec. IV.B, Fig. 8a); "around 100 Hz" [R98].
- **T1 (B1−):** about 140 Hz [R98].
- **Main body resonance, the one that causes the wolf:**
  - 173 Hz in [ZWS16].
  - 171.7 to 176.5 Hz, Q 18.1 to 26.2 on a test rig and Q 13.4 when held by a player [ZW14 Tables I–II].
  - Peak admittance −24.4 to −25.6 dB re 1 m/(s·N), which is about 0.056 m/(s·N) [ZW14 Tables I–II].
  - The wolf is usually at F3 or F#3 on the C string; 175 Hz in their cello [ZW18 sec. II]. [EU 9.4] gives F3, 174.6 Hz.
  - Scaled by string impedance, the cello's wolf peak is about 5 dB above the highest violin peak [EU 9.4].
- **Further low peaks in cello 1:** 200, 209 and 281 Hz [ZWS16 Fig. 8].
- **Cello admittance shape (read, [ZW14] Fig. 2):** about −26 dB at the wolf; about −28 dB near 430 Hz; a cluster of −27 to −30 dB at 0.8–1 kHz; about −30 dB near 2 kHz; about −44 dB at 4 kHz.
- **String admittance used for the cello C string:** 0.71 m/(s·N) [ZW18 II.A], which is a wave impedance of 1.41 N·s/m (derived).

**Viola:** I found no open, verifiable data.

### 2. The bridge hill

**Woodhouse's model [W05].** The errata correct equation 1. As corrected, the bridge admittance is (k·Yv + iω) divided by (k − m·ω² + iω·m·k·Yv), where Yv is the body admittance.

- **Two-foot version:**
  - Clamped bridge frequency Ωb = (1/a)·√(K/m) (eq. 2).
  - Rotational admittance R = (Y11 + Y22 − 2·Y12)/d² (eq. 3).
  - The "skeleton" hill poles solve ω² − iω·R∞·K − Ωb² = 0 (eq. 18).
  - So the hill damping ratio is R∞·K/(2·Ωb), equivalently R∞·m·a²·Ωb/2. This is my reading of eqs. 18–19.
- **Table I standard values:**
  - Clamped frequency 3000 Hz; bridge top mass m 0.5 g; height a 20 mm; foot spacing d 30 mm.
  - Plates 321 × 204 mm, hinged edges, Q 50 on all plate modes.
  - Spruce top: 2.9 mm, density 420 kg/m³, elastic constants 1100/67/84/230 MPa.
  - Maple back: 4.0 mm, density 650 kg/m³, constants 860/140/170/230 MPa. The table note says two of the maple constants were guessed.
- **Body skeleton:** real part of R∞ about 100 rad/(m·s·N) near 2.5 kHz (sec. 3, Figs. 11–12). The resulting hill loss factor is about 0.19 (Fig. 12b).
  - Derived check: K is 71 N·m/rad and the damping ratio comes out at 0.19, so the hill Q is about 2.6.
  - The imaginary part of R∞ shifts the hill between about 2 and 4 kHz (Fig. 12a).
- **Ready-made test case (Fig. 4):** body modes every 200 Hz, modal mass 0.1 kg, Q 50; bridge 1.5 g with clamped frequency 3 kHz.
- **Parameter sweeps (Figs. 13–19):**
  - Bridge mass 0.2–2 g at fixed frequency.
  - Mass 0.2–10 g at fixed stiffness, which covers mutes.
  - Clamped frequency 2–5 kHz.
  - Foot spacing 5–40 mm.
  - Top and back thickness 2–4 mm; a thinner top lowers and widens the hill.

**Measured violin values**

- **Clamped bridge modes:**
  - Rocking about 3 kHz and bouncing about 6 kHz [W14 Fig. 20 caption].
  - Finite-element values with the feet clamped: 3356/6342/8310 Hz for an untrimmed bridge, 2303/5159/6973 Hz with all cutouts [RM90 Table 2].
- **Rocking frequency in practice:**
  - 2.6 to 3.6 kHz in the Oberlin trimming tests; ensemble average 3.2 kHz [B12 sec. V.A].
  - A fitted bridge model gives 2.9 kHz, moment of inertia 4.3e-7 kg·m², height 19 mm [EU 7.5]. That implies about 1.2 g effective mass at the string notch (derived).
  - Whole bridges weigh 1.5–3.3 g, typically 2.4 g [EU 7.5].
- **Trimming sensitivity:** 24 ± 14 kHz per gram removed from the waist, against 0.75 ± 0.03 kHz per gram from the wings; about 180 Hz per mm of waist [B06 II.B].
- **Hill (BH) peak:**
  - About 2.3 kHz [B06] and 2.4 kHz [B08a, B08b].
  - Its frequency barely follows the rocking frequency: it moved 31 ± 15 Hz when the rocking frequency moved 0.8 kHz [B06 III.B.1].
  - [B08b] separates "BH and bridge-filter peaks at 2.4 kHz and 3.5 kHz".
  - [B12] Fig. 8 fits three resonances: a "ring" near 1 kHz, the BH near 2.3 kHz, and a rocking-frequency peak near 3.2 kHz. The BH shape value is about 4.6 (read), which is about +6.6 dB in admittance relative to 1375 Hz.
- **Size of effects:**
  - Lowering the rocking frequency from 3.4 to 2.6 kHz cut peak radiativity in the 2.5–4 kHz range by about 3 times [B08b III.C.4].
  - Changing bridge parameters by ±30% moves the hill height by more than 10 dB, and a 5 g mute cuts up to 12 dB [EU 7.5].
  - Violin admittance is about 20 dB above a guitar's in the low kHz range [EU 5.3].
  - Four violins' smoothed admittance peaks at 0.07–0.13 m/(s·N), somewhere between 1.4 and 2.7 kHz (read, [W14] Fig. 23).

**Cello bridge.** Your belief is right for the admittance hills; the clamped bridge resonances themselves sit higher.

- The admittance shows two hills, about 1 kHz and 2–2.3 kHz, in two cellos [EU 5.3 Fig. 16]. [ZW14] puts the hill near 1 kHz.
- The first in-situ bridge resonance was about 1200, 1500 and 1350 Hz on three cellos [ZWS16 IV.A]. That paper cites Reinicke's clamped value of about 1 kHz.
- Clamped mode sketches: sway about 1.5 kHz, bend about 2.2 kHz, bounce about 3.1 kHz [EU 5.3 Fig. 15].
- Finite-element values with clamped feet [RM90 Tables 3–4]:
  - Standard bridge: 1698/2249/3050 Hz untrimmed, 1292/1428/2205 Hz with all cutouts.
  - Belgian bridge: 1543/2363/3360 Hz untrimmed, 1189/1569/2508 Hz with all cutouts.
- Cremer, as summarised in [RM90]: about 1000 and 3000 Hz for the cello, against about 3000 and 6000 Hz for the violin.
- A cello bridge weighs about 19 g [ZWS16 II.B].

### 3. Violin body above about 1 kHz

- **Modal density [B12 IV.B.1]:**
  - 2.2 modes per 250 Hz band rising to about 4.4 near 1.1 kHz. Their sixth-order polynomial evaluates to about 4.0 at that point.
  - My evaluation of the polynomial gives about 0.016 modes per Hz at 1.1 kHz, 0.013 at 2 kHz, 0.012 at 3 kHz and 0.014 at 5 kHz. That is a mean spacing of 63–84 Hz.
- **Flat-plate estimate from the [W05] Table I plates (derived):** about 0.014 per Hz for the top plus 0.011 for the back, so about 0.025 per Hz (40 Hz spacing). [W06] says a violin has "perhaps 100–200 modes" in the audible range.
- **Damping:**
  - With the violin hanging free, total damping is about 1% at 0.4 kHz, falling to about 0.5% (Q about 100) at 4 kHz (read, [B08b] Fig. 4).
  - The fitted trend runs with frequency to the power −0.34 [B12]. A rule of thumb gives −0.5 [B08b]. The exponents for excellent and bad violins were −0.40 and −0.45 [B08a].
  - Holding the violin raises damping a lot: the fraction of vibration energy radiated drops by about 50% [B08a]. For the cello wolf mode, Q fell from 18 to 13 when held [ZW14].
- **Modal overlap:** above 1 around 1–2 kHz [W14 sec. 3.1]; above about 1.5 kHz [W02].
- **These sources disagree.** Bissinger's mode count combined with free-free damping gives an overlap of only about 0.3 at 2 kHz. I recommend 0.02 modes per Hz with Q 50 for the playing condition. That gives an overlap of about 0.8 at 2 kHz and 1.6 at 4 kHz, consistent with Woodhouse.
- **Cello:** I found no published high-frequency modal density or damping. Scaling plate area against thickness suggests 2 to 3 times the violin's density, about 0.04–0.06 modes per Hz. That is UNVERIFIED.

### 4. Radiation

- **Low-frequency regime:** radiation is monopole-like below about 1 kHz. The Helmholtz number passes 1 at about 1 kHz [W14 sec. 3.1].
- **f-holes:** they give about 50% of the sound below about 1 kHz [B08a III.B.4].
- **Radiation efficiency:**
  - It rises as frequency squared below the critical frequency and stays at 1 above it [B08b; B12 IV.A].
  - Effective critical frequency is 3.9 kHz on average (minimum 3.3, maximum 4.6) [B12]. Excellent violins average 3.5 kHz and bad ones 4.3 kHz [B08a III.B.3].
  - Efficiency equals 1 when radiativity divided by corpus mobility reaches 35.2 Pa·s/m in their setup [B08a].
  - There is a universal dip in efficiency near 1.6 kHz [B08a]. The "ring frequency" is about 1 kHz [B12]; Woodhouse estimates 1–2 kHz [W05].
- **Damping split:**
  - Radiation damping rises with frequency below the critical frequency and falls as 1/f above it [B08b; B12].
  - About half of A0's energy loss is radiation [B08b; B12].
  - For the corpus, the radiated fraction rises from about 0.2 to about 0.6 (read, [B08b] Fig. 4).
- **Directivity (top over back):** about 1 at low frequency, a fast rise at 625–875 Hz, a plateau from 1 to 2.5 kHz, then a slow rise [B08a Fig. 7].

**Averaged radiativity.** Read from [B08a] Fig. 2: 17-violin averages in 250 Hz bands, in Pa/N at 1.2 m, then in dB relative to the 2375 Hz band.

| Band | A0 | 400 | 625 | 875 | 1125 | 1375 | 1625 | 1875 | 2125 | 2375 | 2625 | 2875 | 3125 | 3375 | 3625 | 3875 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Pa/N | 0.37 | 0.25 | 0.25 | 0.37 | 0.39 | 0.36 | 0.42 | 0.53 | 0.62 | 0.74 | 0.62 | 0.58 | 0.48 | 0.51 | 0.51 | 0.48 |
| dB | −6.0 | −9.4 | −9.6 | −6.1 | −5.7 | −6.2 | −5.1 | −3.0 | −1.6 | 0 | −1.5 | −2.2 | −3.8 | −3.3 | −3.3 | −3.8 |

- **Axis caveat:** the figure's "0.5" tick label sits where 0.6 belongs on its log scale. I calibrated from the 1.0 and 0.2 labels.
- **Check:** my 1375 Hz value is 0.36. [B12] quotes a mean-square of 0.15, which is 0.39 Pa/N, a match within 0.5 dB.
- **Mismatch:** [B12] also quotes 0.19 Pa/N at 630 Hz for every quality class, against 0.25 in this figure.
- **The real "dip":** in these sources it is the 600–750 Hz trough [B12], not 1–1.5 kHz. There is a small "nasal" bump near 1.2 kHz [B08b].

**Above 4 kHz.** Bissinger's radiativity data stop at 4 kHz.

- The driving-point admittance falls about 9 dB per octave from 3 to 10 kHz (read, [W14] Fig. 1).
- Corpus mobility falls about 8.5 dB from 2.4 to 4.9 kHz (read, [B12] Fig. 8).
- Since radiation efficiency is 1 above the critical frequency, radiated sound follows the mobility.
- Dünnwald's envelope curves, redrawn in [B08b] Fig. 6, show old-Italian and master violins holding level to about 5 kHz, while factory violins fall above about 3 kHz.

### 5. Open-access figures you can digitise

1. **[B08a] Fig. 2.**
   - Top panel: radiativity on a log axis labelled 0.2–1.0 Pa/N (see the tick caveat in section 4), 17 violins.
   - Bottom panel: corpus mobility 0.01–0.05 m/(s·N), log, 14 violins.
   - Horizontal axis: band centres from A0, then 400 to 3875 Hz.
   - This is the only true ensemble average I found.
2. **[B12].**
   - Fig. 8 left: corpus mean-square mobility, about 1e-4 to 1e-3, from 0.5 to 5 kHz.
   - Fig. 8 right: the bridge-filter shape function, 0.1–5 on a log axis.
   - Fig. 10: modelled squared radiativity, 0.01–1 (Pa/N)², 200–6000 Hz.
3. **[B08b].** Fig. 4: damping and radiated fraction, 0.1 to about 1.5, from 0.3 to 10 kHz. Fig. 6: Dünnwald envelopes, 200 Hz to about 7 kHz, with 25 dB scale bars.
4. **[W05].** Fig. 2: one violin, −50 to −10 dB re 1 m/(s·N), about 180 Hz to 7 kHz. Figs. 13–19: skeleton curves, −60 to +10 dB.
5. **[W14] (Cambridge manuscript).**
   - Fig. 1: two violins, −60 to −10 dB re 1 m/(s·N), about 180 Hz to 10 kHz.
   - Fig. 19a: five violins as a string impedance ratio, −65 to −15 dB, 196 Hz to about 10 kHz. Add 9.4 dB to convert to dB re 1 m/(s·N).
   - Fig. 23a: smoothed admittance of four violins, linear 0–0.15 m/(s·N), 0–4 kHz.
6. **[ZW14].** Fig. 1: violin, −55 to −15 dB, about 150–4000 Hz. Fig. 2: cello, −60 to −20 dB, 65–4000 Hz.
7. **[EU 5.3].** Fig. 2: seven violins. Fig. 16: two cellos with averaged curves. Both are scaled by string impedance on a semitone axis; I did not check the axis values.

### 6. Where the sources disagree, and what I chose

- **Origin of the bridge hill.** Bissinger finds the BH frequency set by the f-hole "island" and insensitive to the bridge [B06]. Woodhouse's model ties it to the bridge resonance [W05].
  - Choice: use one hill oscillator at about 2.4 kHz with Q about 2.5. Treat the rocking-frequency shoulder near 3.2 kHz as optional.
- **Modal density.** Choice: 0.02 modes per Hz (section 3).
- **Which mode is the cello wolf.** [ZW14] calls it "probably the B1− mode", but [R98] places C3 (B1+) near 180 Hz.
  - Choice: treat it simply as the strongest mode near 175 Hz.
- **Critical-frequency wording.** [B08a] puts the along-grain critical frequency two octaves above the cross-grain one. [B08b] and [B12] say the reverse: along-grain 4–5 kHz, cross-grain 9–17 kHz. The second version is physically right.

### 7. Recommended parameter set

**Violin.** Levels are radiation levels in dB relative to the B1− radiativity of 1.01 Pa/N. Effective mass at the bridge is Q divided by (2πf times peak admittance), derived from the readings in section 1 and therefore rough.

| Element | f (Hz) | Q | Level (dB) | Effective mass at bridge |
|---|---|---|---|---|
| A0 | 275 | 20 | −8 (range −11 to −2) | about 0.5 kg |
| CBR | 405 | 40 (UNVERIFIED) | −20 (my judgment) | about 0.2 kg |
| B1− | 470 | 41 | 0 | about 0.08 kg |
| A1 | 469 | 50 | −10 (my judgment; very variable) | not applicable |
| B1+ | 541 | 43 | −0.5 | about 0.07 kg |
| High-frequency modes above 630 Hz | random, 0.02 per Hz (range 0.012–0.025) | 50 when played (50–100 hanging free) | band levels follow the section 4 table | not applicable |
| Bridge hill | 3.0 kHz clamped (2.6–3.6); peak about 2.4 kHz | hill damping 0.19 (Q about 2.6) | about +6 dB over the 1.4 kHz level | 1.0 g (0.5–1.5 g); a 20 mm, d 30 mm, real part of R∞ 100 |
| Radiation | critical frequency 3.9 kHz (3.3–4.6) | not applicable | efficiency rises as f² below, 1 above; −8 to −9 dB per octave above about 4 kHz | not applicable |

**Cello.**

| Element | f (Hz) | Q | Level | Note |
|---|---|---|---|---|
| A0 | 97–100 | UNVERIFIED (try 20) | radiation UNVERIFIED | weak in admittance |
| B1− (T1) | 140 | UNVERIFIED | UNVERIFIED | [R98] |
| Wolf mode | 175 (172–177) | 18 (13 when held) | 0 dB reference; −25 dB re 1 m/(s·N) | effective mass about 0.29 kg (derived) |
| Further peaks | 200, 209, 281 | UNVERIFIED | lower (read) | [ZWS16] |
| Bridge hills | about 1.0 kHz and about 2.1 kHz | UNVERIFIED (try 2–3) | about −27 to −30 dB re 1 m/(s·N) (read) | first in-situ bridge resonance 1.2–1.5 kHz; bridge 19 g |
| High-frequency modes | 0.04–0.06 per Hz (UNVERIFIED) | 30–50 (UNVERIFIED) | not available | no published cello radiativity found |

The fetch tool kept its own copies of the PDFs; I deleted my temporary render files.

### References

- **[B06]** Bissinger G (2006). The violin bridge as filter. J. Acoust. Soc. Am. 120, 482–491. https://strad3d.org/files/bissinger/JASA%202006%20vn%20bridge%20as%20filter.pdf
- **[B08a]** Bissinger G (2008). Structural acoustics of good and bad violins. J. Acoust. Soc. Am. 124, 1764–1773. https://strad3d.org/files/bissinger/JASA%202008%20Struct%20Acoust%20good%20bad%20violins.pdf
- **[B08b]** Bissinger G (2008). Structural acoustics model of the violin radiativity profile. J. Acoust. Soc. Am. 124, 4013–4023. https://strad3d.org/files/bissinger/BissingerStructAcousticModel_vn_rad_prof.pdf
- **[B12]** Bissinger G (2012). Parametric plate-bridge dynamic filter model of violin radiativity. J. Acoust. Soc. Am. 132, 465–476. https://strad3d.org/files/bissinger/JASA%202012%20dynamic%20filter%20model.pdf
- **[W98]** Woodhouse J (1998). The acoustics of "A0–B0 mode matching" in the violin. Acustica/Acta Acustica 84, 947–956. https://euphonics.org/wp-content/uploads/2022/03/the-acoustics-of-A0_B0-mode-matching-in-the-violin.pdf
- **[W02]** Woodhouse J (2002). Body vibration of the violin — what can a maker expect to control? Catgut Acoust. Soc. J. 4(5), Series II, from p. 43. https://euphonics.org/wp-content/uploads/2022/03/CAS_Jansson.pdf
- **[W05]** Woodhouse J (2005). On the "bridge hill" of the violin. Acta Acustica united with Acustica 91, 155–165. https://euphonics.org/wp-content/uploads/2022/03/BridgeHill.pdf — errata: https://euphonics.org/wp-content/uploads/2022/03/Bridge-hill-errata.pdf
- **[W06]** Woodhouse J, Cross I, Moore BCJ, Fritz C (2006). Perceptual tests with virtual violins. Proc. Inst. Acoust. 28(1), from p. 615. https://www.dalembert.upmc.fr/lelam/Membres/Fritz/HomePage/PostDoc/Woodhouse_IOA06.pdf
- **[W14]** Woodhouse J (2014). The acoustics of the violin: a review. Rep. Prog. Phys. 77, 115901. https://www.repository.cam.ac.uk/handle/1810/245817
- **[ZW14]** Zhang A, Woodhouse J (2014). Reliability of the input admittance of bowed-string instruments measured by the hammer method. J. Acoust. Soc. Am. 136, doi 10.1121/1.4900570; page numbers UNVERIFIED. https://www.repository.cam.ac.uk/handle/1810/246474
- **[ZWS16]** Zhang A, Woodhouse J, Stoppani G (2016). Motion of the cello bridge. J. Acoust. Soc. Am. 140, 2636–2645. https://euphonics.org/wp-content/uploads/2022/03/Ailin_IC_cellobridge.pdf
- **[ZW18]** Zhang A, Woodhouse J (2018). Playability of the wolf note of bowed string instruments. J. Acoust. Soc. Am. 144, 2852–2858. https://euphonics.org/wp-content/uploads/2022/03/Ailin-wolf-note.pdf
- **[RM90]** Rodgers OE, Masino TR (1990). The effect of wood removal on bridge frequencies. Catgut Acoust. Soc. J. 1(6), Series II, 6–10. https://fiddlerman.com/wp-content/forum-media-uploads/elcb/2021/04/Bridge_wood_removal_Rodgers.pdf
- **[R98]** Rossing TD, Roberts M, Bynum E, Nickerson L (1998). Modal analysis of violins and cellos. ICA/ASA '98, lay-language paper 3aMU2. https://acoustics.org/pressroom/httpdocs/135th/rossing.htm
- **[S3D]** Strad3D web page "The Signature Modes" (no author named). https://strad3d.org/st_4.html
- **[EU]** Woodhouse J, Euphonics (online book):
  - section 5.3: https://euphonics.org/5-3-signature-modes-and-formants/
  - section 7.5: https://euphonics.org/7-5-tonal-adjustment-in-the-violin/
  - section 9.4: https://euphonics.org/9-4-chasing-the-wolf/
