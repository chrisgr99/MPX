# Bowed strings: verified sources

Findings gathered and checked against the papers on 9 October 2026, for docs/bowed-strings.md. Each entry gives the value and then notes on how it was checked. The full citations are in the bowed-strings doc.

## strings | Cello Kaplan strings: Q implied by the Table 1 loss coefficients versus the plotted fit (factor-of-2 discrepancy)
VALUE: Eq. 6, Qn = (T + EI k^2)/(T (etaF + etaA/omega_n) + EI etaB k^2) with k = n pi/L, using Table 1 values at L = 0.69 m. Mode 1: A3 3212, D3 2825, G2 3722, C2 3391. Peak (mode 2 or 3): 3554, 3242, 4042, 4589. Plotted fit read from Fig. 1, mode 1: about 1600, 1400, 1850, 1650. Plotted peak: about 1750, 1600, 2000, 2250. Fig. 8 'rigid target' at the fundamental: C2 about 1800, D3 about 1400. Halving Eq. 6 gives 1606, 1412, 1861, 1696 at mode 1, which matches. So use Q = half of Eq. 6, or double all three eta coefficients, to reproduce the paper's own damping.
NOTES: CORRECTED and SHARPENED. The earlier report said 'Q1 about 2800 to 3700, plots show 1300 to 1900; check units' and gave about 1900 for A3. Fig. 1 actually shows about 1600 for the A3 mode-1 fitted Q. The mismatch is a consistent factor of about 2 at the low modes on all four strings. At mode 30 the ratio is 1.5 to 2.2. The text says the high-frequency limit 1/etaB is 'around 20', but the printed etaB gives 7.7 to 8.8 for A3, D3 and G2 and 21 only for C2, another sign of a scaling problem in the printed table. Text quote, sec. 2.1.2: the highest rigid-bench Q occurs at mode 2 or 3, 'ranging from 1200 to 3000'.

## strings | Cello open C2 and open D3: on-instrument Q of the low partials (Kaplan strings on a real cello)
VALUE: Open C2: fundamental about 530, partial 2 about 800, partial 3 (about 196 Hz, near the wolf) about 150, and roughly 250 to 600 up to 1.4 kHz. Open D3: fundamental about 620, partial 2 about 320, partial 3 about 470, and roughly 400 to 700 up to about 1 kHz. Rigid-termination values at the fundamental are about 1800 (C2) and about 1400 (D3).
NOTES: CORRECTED. The earlier report gave about 600 for the open C2 fundamental; my pixel reading is about 530, plus or minus about 50. It also tied this to the quote 'the highest Q factor among all partials for the C2 string barely reaches 600'. That quote refers to Fig. 7, the simulated first 11 finger-stopped semitones with the body. The paper says those 'numbers are much lower than those in Fig. 1 because finger damping has been added'. It is not about the open string on the real cello. The paper itself warns these measured Q values 'should not be over-interpreted'. No on-instrument Q is given for G2 or A3.

## strings | Cello D string: complete simulation parameter set (Woodhouse and Loach measurements)
VALUE: Nylon-core cello D at 147 Hz: transverse Z 0.55 kg/s, torsional Z 1.8 kg/s, torsional/transverse speed ratio 5.2, constant-Q reflection functions with Q 500 (transverse) and 45 (torsional), EI 0.0004 N m^2. Exponential friction curve: mu = 0.35 + 0.4 exp(-|slip speed|/0.01 m/s) + 0.45 exp(-|slip speed|/0.1 m/s), so mu_s = 1.2. Simulations: bow speed 0.05 m/s, fs 200 kHz, bow force 0.005 to 5 N, beta 0.02 to 0.4.
NOTES: NEW. This is a ready-made, internally consistent cello D waveguide set from the Cambridge measurements. The friction formula is printed with an exponent (v - vb)/0.01, which is negative during slipping; I rewrote it with the absolute slip speed. A conference paper, not a journal article. Q 500 with Z 0.55 implies R of about 175 kg/s (my calculation, using R = 2 Z0 Q/pi).

## strings | Cello bridge effective resistance R (Schelleng/Raman sense), open strings
VALUE: Open C2 about 490 kg/s (range about 450 to 560 given reading error). Open D3 about 255 to 265 kg/s (Kaplan, Mansour Fig. 8), or about 175 kg/s using Serafin et al.'s Q of 500 and Z of 0.55. If Q1 is assumed to be 600 for G2 and A3: about 380 and 215 kg/s. Velocity reflection factor at the bridge about -0.994 to -0.995. Fundamental T60 = 6.91 Q1/(pi f1): C2 about 17 s (Q 530); D3 about 9 s (Q 600).
NOTES: CORRECTED. The earlier C2 figure of about 560 assumed Q1 = 600, but Fig. 8 reads about 530. No published measured R for the cello was found. A single R is crude: on the real cello, Q varies strongly from partial to partial, for example C2 partial 3 near the wolf at about 150. Mansour 2017 replaces R with measured body behaviour; their single-mode test body is 172 Hz, effective mass 120 g and Q 40, which they call 'rather extreme'. For comparison, violin open D gives R/Z0 of about 260 (Schoonderwaldt), and the cello values here give R/Z0 of about 340 to 390. Treat these as order of magnitude only.

## friction | Hyperbolic friction curve: form and the values commonly used for violin
VALUE: In the paper's notation, mu = mu_d* + K/(z + z0), with K = 8 cm/s, mu_d* = 0.4 and z0 = 20 cm/s. The paper states that these give mu_s = 0.8 and mu_s minus mu_d* = 0.4. Here z is the relative slip speed, which equals v_B/beta in Helmholtz motion. In your notation: mu(dv) = 0.4 + 0.4 x 0.2/(0.2 + |dv|), with dv in m/s, mu_s = 0.8, mu_d = 0.4 and v0 = 0.2 m/s.
NOTES: Re-verified in the 2008 paper. Correction: the denominator is printed as z minus z0 in both the 2007 and 2008 versions, so this is a misprint in the source, not a text-extraction artefact. The plus sign is required by the paper's own statements: it gives mu_s = 0.8 and the effective delta-mu written as z(mu_s - mu_d*)/(z + z0). The minus form would give mu = 0 at z = 0 and a singularity at z = z0. The values are attributed to Guettler (2002, Acta Acustica 88, 970-985) and Woodhouse and Galluzzo (2004, Acta Acustica 90, 579-589). Rewriting K as (mu_s - mu_d) times z0 = 0.08 m/s is my own algebra.

## friction | Hyperbolic friction curve in a simulated cello D string, with the simulation settings
VALUE: mu = mu_d + (mu_s - mu_d) v0/(v0 + |v - v_b|), with mu_d = 0.3 and mu_s = 0.8. v0 is called the 'initial bow velocity' and has no number. Settings: cello D string; bending stiffness B = 0.0004 N m^2; sample rate 200 kHz; constant bow speed 0.05 m/s, with the string starting from rest; bow force 0.005 to 5 N; beta 0.02 to 0.4. Torsional wave speed 5.2 times the transverse speed. Transverse and torsional impedances 0.55 and 1.8 kg/s. Constant-Q reflection functions with Q = 500 (transverse) and Q = 45 (torsional), measured on a nylon-core cello D string tuned to 147 Hz.
NOTES: All values re-verified. Added: the 5.2 wave-speed ratio, and the fact that these runs started from rest, not from Helmholtz motion. The paper credits the impedance and Q data to Woodhouse and Loach ('Torsional behaviour of cello strings', Acustica 85, 1999, 734-740). Serafin et al. also report that removing torsion did not reduce the playable area.

## friction | Smith and Woodhouse exponential friction curve (fitted to steady sliding)
VALUE: mu = 0.4 exp(-|dv|/0.01) + 0.45 exp(-|dv|/0.1) + 0.35, with dv the sliding speed in m/s. mu_s at dv = 0 is 1.2, the asymptotic mu_d is 0.35, and the decay speeds are 0.01 and 0.1 m/s.
NOTES: Coefficients re-verified in both sources. Both print the exponent as (v - v_b)/0.01 and so on, so the formula holds for positive slip and the absolute value is implied. My calculations: mu_s = 1.2; the slope at dv = 0+ is 44.5 per m/s; so three intersections occur once F_N exceeds about 0.045 Z (in N, with Z in kg/s), which covers essentially every playing force. Serafin et al. note the fit ignores any dynamic behaviour after a change in velocity. See the separate entry on how this curve performs against experiment.

## friction | Hysteresis rule for choosing among three solutions
VALUE: The middle intersection is always unstable and the two outer ones are always stable. A sticking string stays stuck until the sticking solution disappears. A slipping string stays on its slipping branch until that branch disappears, and then the state jumps to the other outer solution. In Galluzzo's notation, with v1 < v2 bounding the ambiguous band of v_h: sticking continues until v_h < v1, and slipping continues until v_h > v2.
NOTES: Re-verified in both. Galluzzo credits McIntyre and Woodhouse with generalizing Friedlander's and Schelleng's results. Smith's waveguide bow-junction page states that hysteresis is neglected: its table is indexed by bow velocity minus the sum of the incoming wave velocities and returns a reflection coefficient. So that table does not implement this rule, and you must carry the stick or slip state from sample to sample yourself. https://ccrma.stanford.edu/~jos/pasp/Bow_String_Scattering_Junction.html. McIntyre, Schumacher and Woodhouse (1983), JASA 74(5), 1325-1345, not read directly.

## friction | Hysteresis rule in your variables: closed form for the hyperbolic curve
VALUE: Let dv_h = v_bow - v_h, a = F_N/(2Z) and delta-mu = mu_s - mu_d. Stick is allowed when |dv_h| <= a mu_s; then v = v_bow and F = 2Z dv_h. For positive slip, the junction equation becomes dv^2 + (v0 + a mu_d - dv_h) dv + v0 (a mu_s - dv_h) = 0; take the larger root. Three solutions exist only if a delta-mu > v0, equivalently F_N delta-mu/v0 > 2Z. They then exist for dv_h between g_min = 2 sqrt(a delta-mu v0) - v0 + a mu_d and a mu_s. If a delta-mu <= v0 there is no ambiguous band; use a mu_s as the slip threshold. Rule: if stuck, stay stuck while |dv_h| <= a mu_s; otherwise slip on the larger root, with the sign of dv_h. If slipping with sign s, stay on the larger root while s dv_h >= g_min. When it drops below, recapture to stick if |dv_h| <= a mu_s; otherwise slip with the opposite sign.
NOTES: I re-derived this independently and it is correct. The quadratic follows from dv + a mu(dv) = dv_h. g(dv) = dv + a mu(dv) has its minimum where (v0 + dv)^2 = a delta-mu v0, giving the double root dv* = sqrt(a delta-mu v0) - v0 and the g_min above. g_min is always positive, so the slip-to-stick recapture is well defined. When dv_h > a mu_s, the product of the roots is negative, so the larger root is the only positive one. Check case (Z = 0.25 kg/s, F_N = 1 N, mu_s = 0.8, mu_d = 0.4, v0 = 0.2 m/s): band 1.4 to 1.6 m/s, dv* = 0.2 m/s. Confirmed. For the exponential curves there is no closed form; start the root search from the previous slip speed or from the large-dv side.

## friction | Estimating Schelleng's R from string damping or waveguide loop gain
VALUE: R = Z0 coth(T1/(2 tau1)), approximately 2 Z0 tau1/T1 = 2 Z0 Q1/pi. Here Q1 = pi f1 tau1 = pi/ln(1/|lambda|), tau1 is the amplitude decay time of the fundamental, and lambda = (Z0 - R)/(Z0 + R) is the bridge reflection factor. Measured: Q1 = 480, 410, 281, 945 and R = 76, 65, 45, 108 kg/s for monochord D, violin open D, violin stopped D and monochord E. Measured c_lower scaled as R to the power -2.3, far steeper than the inverse proportionality Schelleng predicts.
NOTES: Re-verified, with the Q1 values and the R^-2.3 result added. My calculations: if your waveguide's total loop gain per period at the fundamental is lambda, then R is approximately 2 Z0/ln(1/|lambda|). Solving eq. (A1) for R > Z0 gives exactly R = Z0 (1 + |lambda|)/(1 - |lambda|), which is the same as the coth form. Caveat: Raman's model puts all the loss in one frequency-independent reflection, so a waveguide with a frequency-dependent loss filter is not a Raman string. The authors show Schelleng's F_min underestimates measurements by about 6 to 11 times, so use it as a bound, not as an equality test.

## friction | Measured cello Schelleng limits (acceptance-test reference)
VALUE: Cello D string (Thomastik Dominant), bowed by machine with a rosined Perspex rod at v_b = 0.05 m/s, starting from Helmholtz motion. Upper limit about 0.13/beta N. Lower limit about 0.00086/beta^2 N, which Galluzzo also writes as 0.017 v_b/beta^2. Galluzzo infers a bridge dashpot rate of the order of 20 N s/m. Plotted range: beta about 0.02 to 0.1, force about 0.1 to 2 N, in 20 log steps each.
NOTES: Re-verified. The limits were fitted by eye. Columns of S-motion appear near beta = 1/12, 1/8 and 1/6. My calculations: with Z = 0.421 kg/s the upper limit implies delta-mu of about 0.32 (about 0.42 with Z_T = 0.55). Galluzzo's own estimate (Z^2 v_b/2 of about 0.009, giving R_b times delta-mu of about 10 kg/s) implies he assumed delta-mu of about 0.5. Schoonderwaldt et al. (2008) say this is the only earlier complete experimental Schelleng diagram: one cello string at one bow speed.

## friction | Cello playing values measured on players
VALUE: Six cellists, two cellos, one bow, a 3-octave D-major scale (D2 to D5). Whole-bow legato at 80 bpm: mean bow-bridge distance 5.89 cm, player means 4.54 to 7.35 cm, overall extremes 2.43 to 11.24 cm. Mean bow speed 16.62 cm/s, player means 15.07 to 17.90 cm/s, extremes 6.14 to 32.39 cm/s. Legato with detache at 160 bpm, lower two-thirds of the bow: 7.56 cm, player means 6.09 to 9.15 cm, extremes 3.16 to 11.88 cm. Mean speed 40.48 cm/s, player means 37.19 to 43.83 cm/s, extremes 12.27 to 89.88 cm/s. Bow force only in arbitrary units (about 0.82 to 0.84) from a model; the authors say there are no real bow-force measurements for cello.
NOTES: Correction: the original entry called the extremes (2.43 to 11.24 cm; 6.14 to 32.39 and 12.27 to 89.88 cm/s) player means. They are the overall minimum and maximum values. Player means come from Table 1. My beta conversions for a 0.70 m open string: 5.89 cm is 0.084, 7.56 cm is 0.108. Stopped notes have shorter strings, so the true beta is larger.

## helmholtz | (1) Spectra of the ideal waveforms
VALUE: Bridge-force harmonics fall as 1/n, independent of beta. Bow-point velocity harmonics go as |sin(n*pi*beta)|/n, with nulls at n = k/beta
NOTES: Verified in Euphonics (not peer-reviewed). The spectrum should get steeper at low bow force because the corner is more rounded (Woodhouse 2014, Sec. 2.1).

## helmholtz | (2) Corner rounding and sharpening
VALUE: Propagation, damping, dispersion and the finger round the corner. The stick-slip transitions at the bow sharpen it, more strongly at higher bow force. Low force gives a rounder waveform and weaker high harmonics
NOTES: Verified. Section citation corrected: the review covers this in Sec. 2.1, not 'Sec. 1-2'.

## helmholtz | (2) Schelleng ripples
VALUE: Ripple period = beta*T, so about 1/beta ripples per period (12 at beta = 0.083; 13 at beta = 0.0764). 'Rabbit ears' in the friction force just before and after the slip. With a friction-curve model, finger-side ripples are larger than bridge-side ripples
NOTES: Verified. CLARIFICATION: Mansour's Fig. 6 case is the cello's C string (open C2 = 65.4 Hz) stopped to play at 164.23 Hz, with F_N = 0.746 N and beta = 0.0764. Euphonics: ripples grow with bow force. ADDED: torsion lets ripples leak past the bow, making them much weaker. Torsion also adds a 'torsional spike' early in sticking, and bending stiffness adds 'precursor waves' (Mansour 2017, Sec. 3.1).

## helmholtz | (3) Friction build-up before the first slip, bow accelerating from rest
VALUE: Smoothed static friction force about Z*a*t^2/(beta*(1-beta)*T_period). For a constant bow speed v0 the force rises in steps of 2*Z*v0, each beta*T wide. The bridge force rises in proportion to a*t^2, reaching (1-beta)*mu_s*F_N at the first slip
NOTES: Verified. Guettler: error under 0.31 percent for t greater than 3T at beta = 1/5, and exact whenever t/(beta*T) and t/((1-beta)*T) are both integers. This is a deterministic check of the junction and delays.

## helmholtz | (3) Time of the first slip and the slip speed just after it
VALUE: t_rel = sqrt(beta*(1-beta)*T_period*mu_s*F_N/(Z*a)). Relative velocity just after release = F_N*(mu_d - mu_s)/(2Z). Example: cello D3 (Z = 0.547 kg/s, T = 6.81 ms), beta = 0.0899, F_N = 1 N, a = 1.39 m/s^2, mu_s = 1.2 gives t_rel = 29.7 ms (4.35 periods), bow speed 4.1 cm/s
NOTES: Formula verified against eq. (5b). Arithmetic re-checked. The value 1.39 m/s^2 is column 9 of Galluzzo's measured grid (Euphonics 9.5).

## helmholtz | (3) Guettler wedge (perfect attacks)
VALUE: Perfect attacks (Helmholtz from the first slip) occur only inside a wedge in the force-acceleration plane, larger far from the bridge. Acceleration must stay below limits A and D (equivalently force above a minimum) and above B and C (force below a maximum). C is cancellation by the first slip pulse after 1/beta nut reflections. On one string the acceleration limits scale with frequency, and T_period*Z = twice the string mass
NOTES: Verified. The closed forms (8b), (10b) and (12) are still garbled in text extraction; check them against the printed paper. Guettler's verification runs: T = 5.0 ms, Z = 0.22 kg/s, lossless ends, hyperbolic friction (1.0 / 0.75 at 10 cm/s / 0.5), a = 0.5 to 9 m/s^2, F = 0 to 1 N, more than 15000 runs per plot, 12 periods after the first slip, six integer-ratio beta values. CORRECTION to the summary: outside the wedge attacks are not perfect, but they are not necessarily longer than 20 periods.

## helmholtz | (3) Constant-speed ('switch-on') starts and the effect of losses
VALUE: With lossless ends, the minimum bow speed needed at time point C is always above the maximum allowed at A, so constant bow speed cannot produce regular triggering. Losses at the nut or bow acceleration fix this. With lowered Q values, constant-speed strokes (half a period of acceleration) gave very few perfect attacks for beta above 1/9
NOTES: Reworded from 'condition C always fails'. Guettler: with lowered Q values and plastic friction, non-integer bow positions give wedges similar to integer ones (Fig. 13).

## helmholtz | (3) Simulated transients with the friction-curve model
VALUE: Cello D3, beta = 0.0899: with the classical friction curve, all model variants except one gave roughly a quarter of the samples succeeding within 20 periods. The friction-curve wedge sat 'in the wrong part of the diagram' with 'far fewer successful gestures'; the thermal model came much closer. Rigid bridge: fewer successes. Finger-stopped: more. No bending stiffness: more. No torsion: surprisingly few
NOTES: Verified; the quoted phrase is from the 2014 review. Do not expect a friction-curve simulation to match measured Guettler maps. Test the analytic wedge and its qualitative shape instead.

## helmholtz | (3) Measured transient statistics, cello G2 strings
VALUE: beta = 0.0786 (5.5 cm on a 70 cm string). F = 1.15 to 3.5 N, a = 0.15 to 3.15 m/s^2. Mean transient of successful attacks: 6.74 to 10.26 periods depending on the string. 70.7 to 85.0 percent success inside the fitted playable region. 20 periods is about 0.2 s
NOTES: Verified. Transient measured from the first slip using Galluzzo's algorithm. Under 10 periods is treated as more musically acceptable.

## helmholtz | (4d) Corner width against time step
VALUE: In Smith's simplified waveguide bow model the main Helmholtz pulse travelled as a single-sample impulse. Running the bow-string part at a higher rate improves impulse timing. McIntyre et al. 1983 reportedly used 128 time steps per period and Gaussian reflection functions 3.9 to 40 percent of the period wide
NOTES: The Smith statements are verified, but CORRECTED in scope: they describe his model, not every lumped-loss waveguide. The McIntyre et al. 1983 numbers are NOT re-verified and conflict with the 256-step example in entry (2). Inference: what matters is the number of samples across the corner. Desvages 2018 (Sec. 6.2.4) on aliasing was not re-verified.

## helmholtz | (4d) Too little loss or rounding gives instability
VALUE: With rigid, lossless ends all periodic motions are unstable (Friedlander, Keller). A perturbation grows as a subharmonic modulation of the Helmholtz motion, and realistic losses make stable periodic motion possible. Torsional dissipation may help suppress the instability. In Smith's model, significant damping of the string by the bow was needed for robust Helmholtz motion
NOTES: Verified. Not re-verified: McIntyre et al. 1983, Sec. II D (narrow r(t) destabilises even the simplest regime). Test that the chosen loss filters hold a stable Helmholtz state without slow subharmonic drift.

## helmholtz | (4e) DC drift: area of each reflection function
VALUE: The integral of h1 and of h2 must equal -1 (DC gain exactly -1). 'If this condition is not met the mean values of the left- and right-going travelling waves can drift, which in physical terms would correspond to the entire string shifting position.' Mansour et al. set the loss filter's DC gain to unity
NOTES: Verified word for word. McIntyre et al. 1983 eq. (6) and footnote 46 not re-verified. A typical waveguide loop filter with DC gain magnitude below 1 breaks this.

## Violin, Thomastik Dominant set (Pickering 1985, rigid bench)
VALUE: E5 (wound steel): T 72.56 N, mu 0.383 g/m, Z 0.1666 kg/s, relaxed diameter 0.307-0.315 mm. A4 (Perlon core): 56.25 N, 0.666 g/m, 0.1936 kg/s, 0.676-0.686 mm. D4 (Perlon): 43.72 N, 1.162 g/m, 0.2254 kg/s, 0.795-0.801 mm. G3 (Perlon): 44.57 N, 2.661 g/m, 0.3443 kg/s, 0.803-0.808 mm.

## Violin typical ranges, all four strings (Guettler table)
VALUE: L 32.8 cm. E5: mu 0.38-0.48 g/m, c 432.5 m/s, Z 0.165-0.210 kg/s, T 71.4-90.7 N. A4: 0.58-0.75, c 288.64, Z 0.167-0.217, T 48.3-62.7. D4: 0.92-1.63, c 192.67, Z printed 0.178-0.193, T 34.3-60.6. G3: 2.12-3.09, c 128.58, Z 0.272-0.397, T 35.0-51.1. Set sum 189.0-265.1 N.

## Violin D4 and E5, Prim steel strings: mu, Z, decay, Q and bridge resistance R
VALUE: D4 (steel core, chrome steel winding, 0.70 mm): mu 1.29 g/m, Z0 0.25 kg/s. E5 (plain steel, 0.26 mm): mu 0.41 g/m, Z0 0.18 kg/s. Monochord D: tau1 0.521 s, Q1 480, R 76 kg/s. Violin open D: 0.448 s, 410, 65 kg/s. Violin stopped D: 0.296 s, 281, 45 kg/s. Monochord E: 0.457 s, 945, 108 kg/s.

## Typical friction-coefficient delta (mu_s minus mu_d)
VALUE: Values from 0.3 to 0.8 are 'commonly' observed or used. Fitting the violin D maximum-force data gives about 0.6, or about 0.4 once torsion is included (Z_tot about 0.17 kg/s). The hyperbolic-model fit gives delta-mu* of 0.51 to 0.69, or 0.45 with torsion. Table I gives 0.38, 0.58, 0.60 and 0.60 at 5, 10, 15 and 20 cm/s.

## Violin playing values measured on players (Askenfelt 1989)
VALUE: Player 1: piano 0.29 m/s at 42 mm from the bridge with 0.5 N; forte 0.21 m/s at 18 mm with 2.3 N. Player 2: piano 0.21 m/s at 42 mm with 0.4 N; forte 0.24 m/s at 20 mm with 1.7 N. The most accurate player kept force variations under 0.2 N (about 50%) at piano and under 0.7 N (about 33%) at forte. The dynamic difference was about 9 dB for each player.
