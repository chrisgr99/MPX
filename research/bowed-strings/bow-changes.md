# Bow changes: what the model does and what was tried

Measured for phase 7 of docs/bowed-strings.md, October 2026. The experiments are scratch programs; the findings are here.

## What happens

A note is bowed into Helmholtz motion, then the bow slows to nothing, rests, and speeds up the other way. With the friction curve (0.8, 0.4, 0.2 m/s) and a bow that does not give, the violin's D4 on the G string at 0.6 N:

- As the bow slows, the string's motion does not shrink with it: at a hundredth of a metre a second the bridge force is still three quarters of what it was at full speed.
- While the bow rests on the string it holds it rigidly at the contact, and the string either side rings on as two separate strings, hardly damped: the bridge force stays at half its playing level throughout the rest.
- When the bow moves off the other way, that ringing makes the string slip five or six times a period, for about 300 ms, before one slip a period returns.

## By instrument, string and force

Four reversals each (40 ms slowing, 10 ms rest, 40 ms speeding up), on every string open and a fifth up, at half, once and twice a mezzo force, the bow a tenth of the string from the bridge. Clean means one slip a period again within 90 ms, Guettler and Askenfelt's limit for an acceptable attack.

| | violin clean | violin median | cello clean | cello median |
|---|---|---|---|---|
| friction curve, rigid bow | 34 of 96 | 163 ms | 2 of 96 | over 1 s |
| bristles with hair | 57 of 96 | 53 ms | 29 of 96 | 247 ms |

On the violin the friction curve's changes are clean at a forte force (about 2.4 N on the G string) and mostly not at piano and mezzo.

## What was tried

- The reversal itself: faster and slower, the force lighter, unchanged or heavier at the turn, a brief lift, the new stroke started as a rehearsed attack from rest. None reliably clean.
- Steeper friction curves (0.8, 0.4, 0.05; 0.8, 0.4, 0.02; 1.2, 0.35, 0.02). No better.
- The bristles (Matusiak and Chatziioannou's fit) with their bow hair: the best by the slip count, above. Without the hair as bad as the friction curve, so the hair is what helps. Heard: worse than the friction curve on both instruments, especially the violin, its steady tone about a fifth brighter.
- The friction curve with that hair: no better.
- Thermal friction lumped into one contact temperature (Woodhouse 2003, "Bowed string simulation using a thermal friction model", Acta Acustica 89, 355–368; the form Maestre, Spa and Smith used at a waveguide bow in 2014: dT/dt + C_C T |v| = C_H |F v| with diffusion, the coefficient piecewise linear in temperature from 1.2 cold to 0.35 at 40 K). Calibrated to Smith and Woodhouse's steady-sliding curve within 0.04. At ordinary forces the string slides steadily and never sticks: one temperature cannot cool fast enough between slips. Woodhouse's model needs the heat-diffusion Green's function, under which the temperature swings 14 K every period; he reports Helmholtz motion established more readily and transients shorter and more regular than with the friction curve.

## Sources not yet read

- van Walstijn, Chatziioannou, Lampis and Matusiak 2026, Acta Acustica: a friction model combining elasto-plastic pre-sliding (the bristles) and thermal effects. Read in full on 2026-10-11; see `thermal-elasto-plastic-2026.md`.
- Smith and Woodhouse 2000, "The tribology of rosin", J. Mech. Phys. Solids 48, 1633–1681: the thermal model's Green's function.

## Heard (Chris)

- Held notes on the friction curve, steady and with vibrato: very good.
- Separate bows on one note, friction curve: violin at a mezzo force harsh, at a forte force better but still somewhat harsh; cello at a mezzo force quite acceptable.
- Bristles with hair: worse than the friction curve on both.

## Rehearsing each bow change

On the friction curve, the player tries nine changes on a copy of the instrument as it stands when the bow starts to slow — slowing and speeding up over 20, 40 or 70 ms, a 10 ms rest, the force at the turn 0.6, 1 or 1.5 times the note's — and plays the one back to one slip a period soonest. A separate-bow phrase on the violin's A string, A4 up to E5 and back at 0.41 N: every change clean within 16 to 36 ms, against 25 to 355 ms with every change made the same way (40 ms, unchanged force). On the cello's D string the same nine took 67 to 218 ms, two never clean, against six of eight never clean.

Heard (Chris): the rehearsed versions best on both instruments.

## Uneven notes, and what evens them

In the rehearsed violin phrase the two D5s sounded unpleasant and the top E5 weak. Both are the body: E5's fundamental, 659 Hz, falls in a notch of the violin's radiativity 23 dB down (13 below the band curve), and D5's third harmonic, 1762 Hz, sits on a peak, so in the sound the fundamental is within 1 to 2 dB of the second and third harmonics where the notes below have them 6 to 15 dB down. Seed 44, which places the dense modes, is not unlucky: of 4,000 seeds matching Bissinger's band averages as well (median under 0.9 dB, worst under 3), every one has a notch of 15 dB or more under some harmonic of some semitone from G3 to E6, and 44 has the shallowest. A violin with resolved modes is uneven note to note, as measured ones are.

Two things a player does even it. VIBRATO sweeps each harmonic across the peaks and notches (Fritz and colleagues 2010); 5.8 Hz, 20 cents either way, in from 80 ms after the restart over 150 ms. THE BOW NEARER THE BRIDGE ON HIGHER NOTES, the same fraction of the vibrating length rather than the same distance, so a stopped string is driven as hard as an open one: E5 3 dB louder. Heard (Chris): the phrase with both definitely best, ahead of vibrato alone and of neither.

The cello the same way, its D string, D3 up to A3 and back at 1.0 N: with vibrato (5.5 Hz, 15 cents either way, the cello's narrower vibrato) and the bow following the note, the nine rehearsed changes. Heard (Chris): best, ahead of the same without vibrato and of a wider choice of changes (10, 20 and 40 ms, the force at the turn up to twice the note's), although by the slip count both newer versions settled less often than the first — on the cello the count is a poor guide to what is heard.

## Thermal friction, built properly

Woodhouse's model (2003; Smith and Woodhouse 2000), with his Table I parameters and figure 3's coefficient against temperature (1.2 cold, falling straight to 0.35 at 40 K, level beyond). One contact temperature over a disk 250 microns in radius at one newton, its area growing with the force; a rosin layer one micron thick, its heat capacity the contact's; the string and the bow each a stack of twelve layers from a quarter micron, growing geometrically to 8a / (3 pi) of depth, so that steady heating reaches the steady temperature of a heated disk on a half-space; backward Euler through them. Convection with the rosin through the contact.

THE BOW MOVES PAST THE CONTACT. Built first with the bow's side as a stationary half-space, the contact ran at about 60 K at 0.25 m/s, above the 40 K where the coefficient is level, and the bow only slid. While the string slides the hair under it is fresh: each sample the bow's layers are renewed in the proportion the slide bears to the contact's width. With that the model plays at a player's speeds.

Checked against Woodhouse's figure 6 (cello D string, 0.05 m/s, near his grid's (10,10)): Helmholtz motion, the contact swinging 9 to 29 K a period (his 17 to 31). His Schelleng map from rest, by flybacks: Helmholtz in 66 of 208 cells against the friction curve's 60, in a cleaner wedge; not the much larger region he found, our string's losses being different from his.

Bow changes, as above (four reversals each, every string, three forces, flybacks counted): violin 95 of 96 clean within 90 ms, median 17 ms (the friction curve 60, median 46); cello 72 of 96, median 47 ms (curve 28, median 269). The Fauré first movement's opening 30 s through the player: violin 1 note rough by the slip count and none severe; cello 13, 3 severe — against the friction curve's 9 and 1, 21 and 7 with the same player.

## The squeak, and the bow's speed

Heard (Chris): with the thermal friction, still a high squeak at bow changes and in places through notes, "much higher than the notes". Found in spectrograms and by a measure of the energy between 3 and 10 kHz that lies off the note's harmonics (squeakOf, src/BowedRehearsal): the short length of string between the bow and the bridge ringing at its own modes — Schelleng's ripples, near the note's frequency over beta and its multiples — strongest where the bow presses heavily for its speed: as it slows into a turn, as it starts from standing still with a bite, and through a long slur drawn slowly. On a held violin Eb4 the ripple is 22 to 37 dB below the harmonics at a force twice the impedance and 14 to 23 dB below at four times; at beta 0.15 and four times it jumps into a higher mode altogether. Slower bows ripple more at the same force for the speed.

What helped, by ear and measure, on the violin's first 10 s of the Fauré (thermal friction): the force falling to nothing at the turn of a bow change, the turns quicker (10, 20, 40 ms) — "the second is best"; the bow lightened in full proportion to its speed in a slow slur; THE LOUDNESS IN THE BOW'S SPEED rather than its force, 0.15 m/s at piano to 0.4 at forte, the force 1.8 to 2.5 times the impedance in proportion to the speed — "it did almost eliminate the squeak". Choosing among rehearsed changes and attacks by least squeak changed little: the ripple belongs to the motion, not to the choice.

THE BOW'S CONTACT. At Woodhouse's rod contact (250 microns at 1 N) the rosin overheats from 0.5 m/s and the bow only slides — notes heard as "a thump". Held violin D4 and cello D3 at forces twice and four times the impedance across 0.05 to 1.2 m/s: Helmholtz at every speed at 600 microns (the cello's lightest at the slowest excepted); at 400 sliding from 0.8 m/s; at 900 the slow speeds fail. So 600 microns, a bow's ribbon of hair rather than a rod. With it the passage measured median 15 dB off-harmonic, worst 5.4, two notes mildly irregular, none sliding.

## The muffled quiet notes (2026-10-11)

Chris, on the Fauré renders with the settled player: both instruments excellent when loud, muffled when quiet, and the cello squeaks slightly when quiet. Not yet explained.

What it is not, measured on held notes without vibrato through the player and on the Fauré renders:

- **Not the tone's balance.** The share of the sound above five and a half times the written pitch, through a high-pass filter, is the same at every dynamic and under every friction: the violin's D5 about −9 to −10 dB from pp to ff under the friction curve, the thermal friction and the thermal elasto-plastic model alike; the cello's F3 −17 to −21 dB. In the Fauré, the quiet bars (6 to 9, about 11 to 20 s) are no darker than the loud ones by the same measure. Spectrograms show the violin's harmonics reaching about 6 kHz in the quiet bars.
- **Not a soft release.** The slip rises from a tenth to nine tenths of its peak in about the same time under every friction: the cello's F3 at piano 132 µs under the curve, 181 µs thermal, 138 µs elasto-plastic; the violin's D5 49, 62 and 47 µs.
- **The quiet bars are 15 to 20 dB below the loud ones**, as the dynamics ask.

A first measure said otherwise and was wrong: it read each harmonic at an exact multiple of the written pitch, and every friction plays a few cents flat (the violin about −5 cents), so the upper harmonics fell between the points it read and measured as 40 to 60 dB down. Two remedies were built on it and rejected by ear: friction falling with sliding speed as well as temperature ("none of them sound good"), and the measure that recommended it. Measure brightness with a filter or bands, never at nominal harmonic frequencies.

The squeak measure (squeakOf on 10 to 200 ms of each note) finds at most 3 of 78 notes squeaking in renders where Chris hears squeaks in the quiet bars, so it does not find what he hears either.

## Roughness across the violin (2026-10-11)

A measure of how much each stretch of four periods differs from the next (`make bowplayer ARGS="rough violin"`) found the rasp Chris heard in the Fauré's pianissimo when the slip count passed it: -24 dB there against -45 to -60 dB for a bow that grips and lets go cleanly. Without vibrato only; vibrato changes the period on purpose.

After pianissimo was given its own bow (0.13 m/s, 1.2 impedances, 0.16 of the length), the violin's open notes, thirds, fifths and octaves on every string at every marking, two notes in separate bows:

- **Bow changes are clean** on the D, A and E strings at every marking, -35 to -55 dB. The other strings ringing in sympathy make no difference.
- **Notes started from the string are rough** at piano and mezzo-piano, -12 to -37 dB, and the G string's low notes at every marking.
- **A started note is rough until its bite has eased.** Through 1.8 s notes, in tenths of a second: A4 at piano -15 dB at the start and still -20 dB at half a second, clean (-39) by three quarters; G3 at mezzo-piano -11 dB for 0.6 s, clean by 0.9 s; at forte clean within a quarter of a second. The bite eases over EASE, 0.8 s, chosen by ear on the cello's open C and G in phase 3 on single notes from rest, before the thermal friction and the player.
- **The attack is chosen by how soon it starts and how little it squeaks in its first 150 ms**, nothing after; a bite, and the bow position a bitten attack was rehearsed at (0.7 to 1.4 of the planned one), stay for the note.

## The cello's bow (2026-10-11)

Bowed steadily, each cello string at 0.15 and 0.34 m/s, four places (0.08 to 0.16 of the length) and forces from 1 to 12 impedances, roughness of the sound: the G, D and A strings play cleanly up to about 1.8 impedances at the slow bow and about 3 at the fast one near the bridge, and turn rough fast above that — Schelleng's maximum, which falls with distance and rises with speed. Far from the bridge (0.16) only about 1 impedance holds at a slow bow. The violin's habits, 1.8 at piano and 2.5 at forte, sat at that edge on the cello; the cello's are now 1.4 and 2.2, with pianissimo at 0.13 m/s, 1.0 impedances, 0.128 of the length. Across the cello's strings and markings (`make bowplayer ARGS="rough cello"`) that took the rough cells from 95 of 128 to 82, the D and A strings mostly clean.

**Low notes vary from period to period even bowed steadily**, whatever the force, place or speed: in the sound the open C -30 dB, F2 on the C string -36, the open G -38, the open D -43, against the violin's -49 to -56. The same over one, two or four periods, so it is not a slow change measured over a long stretch; the same with the twist removed; and it is in the string's own push on the bridge, not the body or the strings ringing in sympathy. Whether it is the period jitter real low strings have or a fault in the model is for the ear.

