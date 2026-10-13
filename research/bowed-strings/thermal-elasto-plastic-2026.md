# A thermal elasto-plastic friction model (van Walstijn et al., 2026)

M. van Walstijn, V. Chatziioannou, A. Lampis and E. Matusiak, "A thermal elasto-plastic friction model for bowed-string simulation", Acta Acustica 10, 47 (2026). https://doi.org/10.1051/aacus/2026042. Open access (CC BY). PDF downloaded by Chris to ~/Downloads/aacus260048.pdf on 2026-10-11 and read in full. Measurement data: https://zenodo.org/records/17782538 (string type B, tension T2).

## Why it matters here

Our friction was Woodhouse's original thermal model: the coefficient of friction depends on the contact temperature alone. The paper names that model's known shortcoming, found by Woodhouse and Galluzzo: without any dependence on sliding speed it cannot reproduce the rapid change from sticking to slipping seen in measured bridge force at the first slip of a note. It was read for the muffled quiet notes Chris heard; a later measurement showed the steady release is not softer under the thermal model than under the friction curve (see `bow-changes.md`), so the muffling has another cause. The model is built as BowedString::setRosin.

The paper lists two ways the field has dealt with it:

1. **The enhanced thermal model** (Woodhouse and Galluzzo, refs 13 and 14 of the paper): the friction is a temperature factor times a friction curve falling with sliding speed, a "rate-and-state" form. The force becomes `F = Fb · y(τ) · μstr(v)`.
2. **This paper's thermal elasto-plastic model**: the same product form for the steady part, plus the rosin's shear deformation as a state (the bristle displacement z). The deformation builds up before the first slip, the string starts to slip partially, it warms, the friction falls, it slips faster and warms faster — a "snowball" that makes the release rapid even with no speed dependence at all (μd = μs). Rapid transitions came out in all nine of their test cases.

## The model

- Friction per unit length `F = Fb σ0 z` (bristle damping and viscous terms omitted as negligible beside the thermal effects).
- Bristle law `∂t z = v [1 − α(z, v, τ) z / zss(v, τ)]`, with `zss = y(τ) μstr(v) / σ0` and the elasto-plastic adhesion map α (zero below the break-away displacement `zba = χ zd(τ)`, a half-sine ramp up to `zss`, one beyond).
- Heat from friction is the work done less the change in stored bristle energy: `Qf = Fb σ0 z ς`, where ς is the sliding part of the relative velocity.
- Heat balance, simplified and first order, their equation (22): a heat-absorption term in aτ, a conduction term in bτ that grows with the square root of the sliding speed (conduction into the rosin, string and a moving hair surface), and a constant conduction term in cτ, balancing Qf. The exact form of the bτ term was garbled in the text extraction; read it from the PDF before implementing.
- Temperature factor `y(τ) = (1 + ya (τ/τg)^ξ) / (1 + (τ/τg)^ξ)`: one at ambient, falling towards ya, halfway at the glass transition τg above ambient.
- Steady-sliding coefficient `μss(τ) = (μs + μd ya (τ/τg)^ξ) / (1 + (τ/τg)^ξ)`, fitted to Smith and Woodhouse's rosin-coated-rod data; the speed curve follows as `μstr = μss / y`. The speed curve always lies above the measurable steady curve.
- Contact area proportional to bowing force (the hair ribbon wraps the string at constant mean pressure), so Amontons' law holds in steady sliding.
- Bow-hair compliance, torsion, a finite-width bow with four contact points, and a modal string. All results at a 5 µs time step.

## Parameters for a cello G string (their Table 1)

| | |
|---|---|
| μs, μd | 1.05, 0.3 |
| σ0 | 1.0 × 10⁶ per metre (estimated from rosin's shear modulus, about 500 MPa, a 0.05 mm layer and 50 N of hair tension: about 2 × 10⁶) |
| τg, ya, ξ, χ | 25 K above ambient (45 °C), 0.4, 2.0, 0.5 |
| aτ, bτ, cτ | 1.0 × 10⁻⁶ m/K, 0.22, 1.0 × 10⁻⁴ m/(s K) |
| bow hair Dh, Kh, ζh | 0.62 kg/m, 11.0 × 10⁶ N/m², 1000 kg/(s m) |

The heat constants control the size of the force drop at the first slip: bτ especially. Without the speed dependence (μd = μs = 1.05) a re-tuned set — ya 0.3, τg 35, bτ 0.2 — matched the Schelleng diagram only slightly worse.

## What they found

- Measured against 1,600 robot-bowed strokes on a cello G string at 0.1 m/s (40 positions × 40 forces), the Helmholtz region of the Schelleng diagram is reasonably well predicted, down to some of its patchiness near the minimum force; the double-slip and multiple-slip boundary partly matches; S-motion appears at about the right distances but at lower forces than measured.
- The spectral centroid of the bridge force — their measure of brightness — and its RMS largely follow the measured patterns across the plane. The model's mean friction is too high at the largest forces.
- The match worsens at small bowing forces, which they put down to the simplified heat balance.
- In Helmholtz motion the friction traces a clockwise loop against sliding speed, never following the steady-sliding curve, and the contact temperature rises steeply in each slip (to about 66 °C in their example) and falls fast while sticking.

## Bearing on our model

- The enhanced-thermal product (our temperature model times a hyperbola falling with sliding speed) was built and rejected by ear on 2026-10-11: squeaks in the quiet bars, and the loud ones worse than temperature alone.
- Their full model is built as `BowedString::setRosin` with their Table 1 values and their first-order heat balance, at a single bow point (theirs is a finite-width bow of four points), with the bow hair as a lumped mass, spring and damper over a 1 cm ribbon. The Stribeck curve their co-parameterisation implies is tabulated once over speed and force per metre. Chosen with `BowedInstrument::Design::friction = ROSIN`; `make bowplayer ARGS="... friction=rosin"`.
- At the player's piano settings for the cello's F3 it slips twice a period, where the other frictions slip once: the player's forces and bow positions were chosen under the thermal model, and Schelleng's region moves with the friction.
- Our layered heat model (Woodhouse's diffusion with bow-side renewal) is more detailed than theirs; theirs is used with their model because their constants were fitted with it.
