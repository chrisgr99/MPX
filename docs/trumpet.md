# Trumpet

A module that plays one MPX part on a physically modelled B flat trumpet, on the brass engine the trombone is built on (docs/brass.md): the lips, the air column with its valves, the bell and the player are all inside it.

Name: mpxTrumpet. Status: phase 1 built — the bore and the valves, in frequency and in time (`make trumpetbore`); no lips or player yet. The literature is surveyed in `research/brass/trumpet-and-flugelhorn.md`.

## What it is

The B flat trumpet first, and only it: the C trumpet and the piccolo are shorter variants for later, and the flugelhorn is a module of its own after them. Mutes come later too.

Everything the trombone has proved is kept: the air column as delay lines where it is cylindrical and grids where it flares, Adachi and Sato's hinged lips shutting from the corners, the wave to the bell steepening outside the loop that sets up the oscillation, one filter from the bell to the listener, the tongue and the breath, the engine that renders ahead of what is heard, and the tuning panel. What is new is the valves, the trumpet's own lips and pressures, and a player that chooses fingerings and plays from a table.

## The instrument

**The bore** is the OpenWInD trumpet profile (`examples/frequential/Geom_trumpet_conical.txt`, INRIA, credited), its 34 points of distance from the rim and radius taken as straight lines between them (`BrassBore::trumpet`): the cup narrowing from 8.40 mm to the throat's 1.825 mm at 17.7 mm; the backbore widening to 4.20 mm at 83.7 mm; a step to the leadpipe's 4.64 mm, widening to the cylinder's 5.82 mm at 0.3037 m; the cylinder to 0.7987 m; then the tubing after it, between 5.8 and 7.4 mm to 1.02 m, and the bell, widening to 61.0 mm at 1.4187 m. The file does not say which trumpet it was measured from, nor where the valves are: they are put in the cylinder, the first at 0.640 m, each a straight path of 25 mm, 22.5 mm apart. The main tuning slide's pull lengthens the cylinder before them. Played in time (`BrassBore::trumpetTimed`), the cup from rim to throat is lumped — its volume a compliance, 1.6 cubic centimetres, and the inertance of its narrowing in series — the backbore and the leadpipe are the front grid, the cylinder and the valves' paths are delay lines, and the rest is the back grid, as the trombone's are.

**The valves** sit in the cylinder. Each is two paths side by side, its straight path and its loop, each its own pair of delay lines, joined at both ends by a junction where the tubes meeting share one pressure: twice the sum of the waves arriving, each weighed by its tube's area and the share of the air the valve gives its path, over the sum of those, each tube sending back that less what it brought (`BrassWaveguide::setValve`). Down, the loop takes all the air; up, the straight path; part-way, both, in proportion (Bilbao 2011; Harrison, Bilbao and Perry 2015). A path the valve shuts takes no part and is left empty. The wall's losses are those of the paths the air takes, by their shares, worked out again as a valve moves. So a valve's travel, about 15 ms, changes the air column as it does, and a valve slur's break and a half-pressed valve's sound come from the physics. The steepened wave to the bell meets the same waves coming back at each junction, as at the trombone's.

**The loops** are each in tune alone: pressed by itself, the first lowers the open trumpet's resonances 2 to 8 a tone, the second a semitone and the third three semitones, which takes 179.9, 87.3 and 277.8 mm past the straight path. Together they are sharp, as Moore's (2016) arithmetic says they must be: the first and second 10.5 cents, the second and third 14.9, the first and third 28.2, all three 49.4 (Moore: 11, 16, 30, 54). Makers make the first two a little long to share that out; how much is not published, so they are left in tune alone. The first and third loops have slides the player moves, each lengthening its loop twice over (`BrassWaveguide::setValveSlides`), for the notes that need them. Three valves give eight combinations and seven lengths, the third alone and the first and second together being nearly one (D'haes and Rodet 2002).

**The lips** are Adachi and Sato's Table I values, which are a trumpet's — the trombone uses them scaled up by about one and a half — with their mass and stiffness set by the lip frequency, from about 60 Hz to 1 kHz. Above the fourth resonance real lips change from striking outwards to striking sideways (Yoshikawa 1995; Adachi and Sato 1996); the hinged lips' stretch carries some of that, and how much is a question for the high register.

**The pressures** run far higher than the trombone's: thresholds about 4 to 8 Pa a hertz of the note (Fletcher and Tarnopolsky 1999), and up to about 25 kPa for a loud high note.

**Brassiness**: the trumpet's cylinder is shorter than the trombone's slide but narrower, and blown harder. The steepening is the trombone's, along the cylinder and the loops in use.

**The bell** has a 61 mm radius, so its cutoff is near 900 Hz; the listener's filter is designed for it as for the trombone's.

## What it is built from

| What | Source | Use |
|---|---|---|
| The bore, mouthpiece to bell | OpenWInD, Geom_trumpet_conical.txt, 1.4187 m | the bore, re-expressed |
| Measured input impedance, a Smith-Watkins B flat trumpet: maxima 83.5, 234.2, 353.1, 469.1, 591.6, 702.7, 812.5 Hz | Bilbao and Chick 2013, Table I | the bore's target |
| Calculated peaks and their heights, modes 1 to 8 | Adachi and Sato 1995, Table II | a second target |
| The valve junction | Bilbao 2011; Harrison, Bilbao and Perry 2015 | the valves |
| The loops' lengths and the combinations' errors | Moore 2016 | the loops |
| Lip values, and each mode's lip and sounding frequencies | Adachi and Sato 1995, 1996, Tables I and III | the lips, and where they sound |
| Thresholds, pressures, the level against the pressure, the spectral envelope | Fletcher and Tarnopolsky 1999 | targets |
| Anechoic notes, Bach 180S37, E3 to D6, pp, mf and ff, with and without vibrato | University of Iowa Musical Instrument Samples, Aren Van Houzen, 2003 | the sound's targets, the table, listening |

## The player

**From a table.** For every note the recordings have, without vibrato, at each of their dynamics, the settings that make it sound like the recording — the lips' tension, gap, damping and mass, the breath, how it rises, where it starts and the curve it rises by — found by searching against that recording, as the trombone's were (`make brasssearch`), each search starting from the note beside it so that neighbours stay alike. A note is played from the table: nothing is auditioned for a tongued start. Markings between the recorded ones are blended from them, and ppp and fff are taken one step beyond.

**Fingerings.** For each note the player chooses one of the seven lengths and a resonance: the usual fingering, an alternative where it makes a passage or a trill easier, the first or third slide out for the sharp combinations, and the lips for the last few cents.

**Joins.** Tongued, from the table; a valve slur, the valves moving as the lips go to the new resonance, with no tongue; a lip slur, on one fingering; and trills, by valve or by lip. Shakes, falls and doits, half-valve effects and flutter tonguing come later.

## The code

The trombone's code assumes a slide: its instrument has one continuous length and its player places notes by slide position. The first step is to part the instrument's shape — its tubes and which of them the controls lengthen or switch — from how the player reaches a note: by slide positions, or by valve combinations with their slides. The trombone's benches must give the same results after the change.

## Phases

1. **The bore and the valves** (`make trumpetbore`), built. Tuned by its tuning slide, 19.0 mm in, to the measured Smith-Watkins trumpet's fourth resonance, the open trumpet's resonances 2 to 6 are within 26 cents of that trumpet's and the seventh 42 cents under, past the 2 per cent asked by 8 cents; against Adachi and Sato's calculated trumpet, from 1.6 cents under to 45 over. The two trumpets differ from each other by as much, and the Smith-Watkins was measured with a Kelly Screamer, a high-register mouthpiece of very small cup, which raises the upper resonances. The seven lengths are as above. The delay lines with grids at the ends are within 2.7 cents of the calculation for every one of the seven at 44.1 and 48 kHz, at about 0.12 s of computing a second of sound, and stay bounded with the valves moving twenty times a second while the column rings. The trombone's air column gives the same samples to the last digit as before the valves were added.
2. **The lips and the sound**: held notes from E3 to D6 at pp, mf and ff against the Iowa recordings (the timbre bench), and the thresholds and pressures against Fletcher and Tarnopolsky.
3. **The tables and the search**: the tables of what each note sounds like at each pressure, then the settings table from the searches.
4. **The player**: fingerings, valve slurs, lip slurs and trills, rehearsed only where the table has nothing.
5. **mpxTrumpet**: the trombone's panel and tuning section, and the table's settings on the knobs while tuning.

## Open questions

- Whether the hinged lips hold the high register, or the sideways-striking change needs a second mode.
- How much the valves' bends and ports add to the losses, and whether the model needs it (Pyle 2020).
- Whether the recordings' pp, taken five feet away in an anechoic room, is quiet enough over its noise to search against for every note.
- The pedal notes, held, like the trombone's, by the higher resonances at their harmonics.
