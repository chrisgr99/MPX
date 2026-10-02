# Finger picker

A module that turns the chords on an mpxChart cable into finger-style guitar playing: which strings are picked, by which finger, when, and what the fretting hand holds down to give those notes. Its output is meant for mpxGuitar, though as an MPX cable it reaches any MPX module, and through mpxOut any VCV voice.

Name: mpxFingerPicker.

Status: built. Help in help/mpxFingerPicker.json, from the //? lines beside each control. A demonstration patch, mpxFingerPicker demo.vcv, plays Joni Mitchell's Both Sides Now from mpxChart, transposed up a semitone into G, through the finger picker into a nylon mpxGuitar. The two hands are in src/FingerPicking.hpp and the fretting hand's limits and search in src/Fretboard.hpp, neither with any Rack in it.

## Purpose

mpxChart plays a chord progression; mpxGuitarChart plays a guitar part someone has already written out, fingering and all. Between them is a gap: a progression with no guitar part, played the way a guitarist would play it. No VCV module fills it. acModules' Pick6 is the nearest: it steps through fixed patterns of which of six strings to trigger, with a variation option, but it has no idea of a thumb and fingers, of a bass line, or of which notes a fretting hand can reach.

This module models the two hands. The picking hand follows a style: the thumb on the bass strings, the fingers on the treble strings, in the patterns finger-style players use. The fretting hand finds, for each moment, notes from the chord on the strings the picking hand is about to play, positions a hand can hold.

Folk picking comes first: the arpeggiated, ringing style of players such as Joni Mitchell. Travis picking, walking bass lines and alternate tunings are designed for and come later.

## Signal path

1. The MPX input takes mpxChart's cable. What it carries and this module reads: the key; the current chord and the next two, and the beats until the current one gives way; where the music is, in beats from the start of the cycle and as a bar and a beat in the bar; the time signature; and where the current phrase ends. Chords arrive as a degree, an accidental and a quality in the key, and a bass note; the chord library (Chord.hpp) turns them into pitch classes, each ranked by how much a player wants it — the third and seventh first, the plain fifth last.
2. The picking pattern, at each step, names the strings to be played and the finger playing each.
3. The fretting hand puts chord notes on those strings.
4. The output is an MPX cable carrying the notes, each with its string, fret, dynamic, length and technique — let ring, mostly — and an instrument block naming the guitar: six strings in standard tuning, no capo. mpxGuitar plays each note on its string, so the notes ring, overlap and are cut off as they would on the instrument. The harmony is passed on along the cable, so a module after this one still knows the chords.

## The bass note on the cable

A slash chord — C over G — names its bass, and the bass is what the thumb plays. mpxChart's iReal reader recognises a slash bass and, as built, discards it: its chord holds a root, an accidental and a quality and nothing else. So the chord gains a bass, kept relative to the key as the root is, as a degree and an accidental; with no slash in the chart the bass is the root. It travels on the cable with the current chord and the next two, as every other part of a chord does. mpxChart's own bass output plays it too, so its bass line follows the written bass rather than the root.

## The fretboard library

The fretting hand's work is shared with the strumming and lead modules planned after this one, as the performer is shared by the three modules that play parts: a library with no Rack in it, Fretboard.hpp, with its own command-line test.

- **The instrument**: the tuning, as the MIDI note of each open string, and so the note at any string and fret. Standard tuning to start; an alternate tuning is a different list of open notes, and every module using the library has it at once.
- **The hand's limits**: the fretted notes within a stretch of four frets, no more than four fingers, a barre across several strings counting as one finger, open strings free.
- **The search**: every way of fretting a given set of strings with given notes, within those limits. A guitar is small enough to search completely — each string muted, open or fretted within reach — and the search is run only when the chord changes.

Each module brings its own requirements and its own scoring. Strumming wants full shapes with no gaps between sounding strings; finger picking wants only the strings its pattern uses; lead wants single notes moving smoothly along the neck. The library decides what a hand can do; the module decides what is a good choice among those.

## Fingering for finger picking

The hand frets only the notes being played, not a whole chord shape. The folk patterns use four strings: the index, middle and ring fingers play the third, second and first strings, and the thumb plays one of the fourth, fifth and sixth, whichever the shape puts the bass on. The other two bass strings are left out, neither fretted nor played. Because a string sounds only when it is picked, a barre may lie across a string that is left out.

The requirements, which every candidate must meet:

- Every treble note is a tone of the current chord. A tone the chord library ranks as wrong for the chord, the third of an eleventh, is not used.
- The thumb's string sounds the bass: the chord's bass note, which is its root unless the chart writes another, lower than every treble note.
- Across the four strings, the chord's essential tones are present: the root, and the tones the chord library ranks first and second — the third, the seventh, and whatever stands in their place, a suspended fourth, a sixth, an altered fifth. The plain fifth and the extensions are welcome and not required.
- The hand's limits, as above.

The scoring, among the candidates that meet them:

- **The position knob**: a preference for shapes near the fret it sets, from the open position to about the twelfth. A preference, not a limit: a chord with no good shape near it gets a good shape elsewhere.
- **Staying put**: nearness to the shape before, and fewer played strings changing fret, so a progression does not jump about the neck unless the knob or the chords ask it to.
- **Open strings**, in the open position, since they ring and are what finger-style guitar is made of. Further up the neck an open string counts neither for nor against a shape: picked open strings ringing among stopped notes high on the neck are a sound of finger-style guitar, where for a strummed chord they count against.
- **Fewer fingers**, and no second finger crowded in at the index finger's fret.
- **A deeper bass string**, a little.

In the open position this gives the familiar shapes on the strings the pattern plays: C as x3x010, G as 3xx003, D as xx0232, A minor as x0x210, and F with the bass on the sixth string under a barre, 1xx211.

When the chord changes, the hand moves to the new shape at the change. A stopped string that the new shape frets differently, or leaves out, is stopped, a finger lifted or put down; a string at the same fret rings on until it is played again. An open treble string rings on unless the new shape puts a finger on it, since the fretting hand does not touch it as it moves. An open bass string rings on if it is a tone of the new chord and the new shape does not fret it, and is stopped otherwise, the thumb and palm keeping the bass clean.

THE HAND MOVES EARLY. On the last step before a change, a treble string the next shape frets differently is played as the next shape has it, the hand already on its way, so it rings on into the new chord. Without this, a note picked on the last eighth was stopped a quarter to half a beat later as the finger lifted: in an emulation of Falling, with the chord changing every two beats, 44 of 292 treble notes were clipped that way, 19 of them open strings the hand had no reason to touch. With both rules, none. The next shape is asked of the picker a step early, with the chance it will be given at the change, so it is the shape the hand then takes. A thumb note off the shape's bass string, a run note or an alternate bass, stops the other bass strings, so the bass it moved from does not ring on under it — G and A together on the way into C; coming back to the shape's bass lets the other ring. With no chord, the hand comes off the strings. A step that falls on the change plays the new chord.

## The picking hand

A pattern is data, not code: the steps of one bar, an eighth note each, each naming which fingers play — the thumb on the bass, the index, middle and ring on the third, second and first strings. A pinch is two fingers on one step. Each pattern has two forms, one for a bar of an even number of beats and one for a bar of three; a bar of another length plays the even form, cut off or repeated. The steps are counted from the bar line, so a chord that changes in the middle of a bar takes up the pattern where it is. The second eighth of each beat falls where the chart's swing puts it.

The folk patterns to start with, the thumb on the beat and the fingers between:

- **Forward roll**: in a bar of four, thumb, index, middle, ring, thumb, ring, middle, index — the thumb on the first and third beats, the fingers rolling up the treble strings and back. In a bar of three, thumb, index, middle, ring, middle, index.
- **Pinch and roll**: the thumb and the ring finger together on the first and third beats, then index, middle, index. In a bar of three, the pinch on the first beat, then index, middle, index, middle, index.
- **Thumb and pinch**: the bass alone on each beat, and the middle and ring fingers together on the first and second strings between.
- **Broken arpeggio**: the bass, then each treble string upward once, a beat each: thumb, index, middle, ring on the four beats of a bar of four. In a bar of three, the bass on the first beat and the three fingers in eighths after it.
- **Arpeggio in eighths**: the same upward figure an eighth note a step: thumb, index, middle, ring, twice in a bar of four, the thumb on the first and third beats. It goes straight up each time, where the forward roll comes back down. In a bar of three, up and back once: thumb, index, middle, ring, middle, index.

A step with no fingers is a rest, the strings already picked ringing through it. Changing the pattern takes effect on the next step.

Every note is let ring: finger-style guitar is the ring of one note under the next.

## Style, pop to jazz

One large knob, from pop at nought to jazz at the top, decides how the fretting hand chooses, on its own: it does not read mpxChordVoicing or anything else on the cable but the chart.

**The tones.** Toward jazz the chord is heard as a jazz player hears it. From a quarter of the way, a plain triad gains its seventh, and the seventh becomes required: a major seventh on a major triad, a flat seventh on a major triad on the fifth degree of the key, which is a dominant, and a minor seventh on a minor triad. From 0.6, a chord with a seventh gains its ninth, unless it has an altered one, and a dominant its thirteenth, as tones the hand may use and need not. A chord the chart already colours keeps what it has; a chord with no perfect fifth — diminished, augmented, suspended — is left as written. The tones carry their degrees, so the scoring knows a ninth from a fifth.

**The shapes.** Toward jazz, as far as the knob goes:

- **Open strings** count against a shape, two points each at the top, where at nought they count for one in the open position.
- **The hand sits higher**: the position the scoring aims at rises to the fifth fret at the top, or stays where the Position knob puts it if that is higher.
- **The treble strings carry the colour**: a ninth, eleventh, thirteenth or sixth there counts two points for the shape at the top, a seventh one; the plain fifth counts one and a half against and the root one, the root being the thumb's.
- **No doubled notes**: each pitch class sounding twice counts one and a half against.
- **Each treble string moves little**: every semitone a treble string moves from the shape before counts 0.4 against.

So C, A minor, F and G in C at the top come out as C with D, E and B over it, 8xx757; A with C, G and B, 5xx587; F with C, E and A, x8x555; and G with B, F and A, xx5465, each treble string moving one to three semitones at a change. At nought nothing changes: C is still x3x010.

## Dynamics

The levels follow the hand and the beat. At the Accent knob's middle: the thumb on the first beat of the bar at 0.95, the thumb on any other step at 0.8, a finger on a beat at 0.65, and a finger between beats at 0.55. The Accent knob scales each level's distance from 0.72: at nought every note is 0.72, at the top the contrast is doubled, and the levels are held between 0.15 and 1. The first step of a new chord is 0.05 stronger at the middle, and the last beat of a phrase 5% softer. The notes of a pinch are struck together at one level, the thumb's when the thumb is in it. Timing and small differences of loudness are left to mpxGuitar's performer, which humanises them as it does any part.

## Variation

Three knobs vary what is played, each in its own dimension, each from none to a lot. At nought on all three the pattern is played as written with the best shape for each chord, as the module did before they existed.

**Voicing**, the fretting hand. A chord that returns is held a different way from the last time it was held, as often as 80% of the knob's value; the same chord carried into a new bar takes another shape as often as 35% of it. The other shape is one of the near-best for the chord, chosen among those scoring no more than 6 below the best at the knob's bottom and 24 at its top, so the higher the setting, the further it may go: another chord tone on top, the bass on another string, or higher on the neck. The picker remembers the last shape of each of the last sixteen chords it has held.

**Picking**, the picking hand's rhythm and fingers. A step of the bar is changed as often as 35% of the knob's value, the same steps in the same way in every bar of a phrase. A step with the thumb in it either has the thumb play the chord's fifth, or else its root, on another bass string within the hand's reach, or has a finger join it in a pinch. A step with only fingers either leaves them out, the strings ringing through, or moves them to the next string, or adds a pinch, the thumb on a beat and the finger above off one. A rest in the pattern is filled with the index or the middle finger. A finger added or moved never goes to a string played on the step before, as played, or the step after, as written: a pinch whose added note was the very note picked again half a beat later sounded like the first cut short. Where no finger is free the step is played as written. The first step of each bar and of each new chord keeps its bass note.

**Ornament**, notes the pattern does not contain. On a step with a single treble note, as often as 30% of the knob's value — the same steps in every bar of a phrase — the note is reached by a hammer-on or a pull-off: hammered on from the open string to a note in the first four frets, or from two frets below further up; pulled off from a scale step above within the hand's reach. Where both will do, the hammer-on is played 65% of the time; where only a pull-off will, as on a note that is an open string, it is played half the time, so the ornaments rise about as often as they fall. The first note is picked at the step and the second sounds by the left hand a full step later, where the next step falls, so the first is heard as a note rather than a clipped grace note. An ornament is not played where the pattern's next step picks the same string, nor on the step before a change, where it would land as the hand moves. Before a change of bass, as often as 70% of the knob's value, the thumb walks to the new bass on the last two steps, a scale step at a time from the side the old bass is on: G, A, B into C; C, B, A into G. The run is decided once for each change.

**Palm mute**, the side of the picking hand resting on the bass strings. The thumb's notes are palm-muted — muffled, the attack kept and the ring gone in under a second, under treble that rings — as a figure, as the other variations are: the same steps in every bar of a phrase, as many as the knob says, the first beat of the bar only in the knob's upper half. Muted notes scattered one at a time sounded like mistakes. At nought none; at the top all. It starts at nought, the bass ringing, as the folk patterns want it.

**A figure for the phrase.** Which steps the Picking and Ornament variations change, and how, is decided for the phrase, not for each step: a step of the bar changed in one bar of the phrase is changed the same way in each of them, through its chord changes, as a player settles into a varied figure rather than scattering changes. The figure is drawn from the phrase's place in the form, its section's letter and its number within the section, so a section that returns in a pass brings back its figures; a chart with sections but no letters uses the phrase's number, and a chart with no phrases takes four bars at a time. In the phrase's last bar, the steps the figure leaves alone are changed too, as often as 60% of the figure's chance, where a player fills. A bass run is 1.6 times as likely into the last change of a phrase.

**The same every time from the top.** Every chance is drawn from the chart's seed and its pass counter, with the step or the phrase, so a patch plays the same notes each time it is played from the top, and each pass of the form differs.

## The bass

The thumb's notes are a layer of their own: a sequence of bass notes chosen chord by chord, which the patterns read. To start, the chord's bass note on every thumb note, on the lowest string the shape allows.

So that walking bass lines can be added without remaking anything, the bass layer can see the next chord and the beats until it, which the cable already carries. Travis picking's alternating bass — the root and then another chord tone, usually the fifth, on a second bass string — is a second way of choosing the same layer's notes, with a busier thumb pattern.

## Controls

On a 10 HP panel, every control from the panel library, reading down: the chord and the shape the hand holds; the patterns under a PATTERN heading; the Position, Accent and Style knobs in a row; the three variation knobs in a row, small; then the MPX input on the left, the Palm mute knob, small, and the output on the right.

- **Pattern**: the picking pattern, a lamp group reading down: FORWARD ROLL, PINCH & ROLL, THUMB & PINCH, ARPEGGIO 1/4 (the broken arpeggio) and ARPEGGIO 1/8 (the arpeggio in eighths). A new pattern goes at the end of the list, so a saved patch opens on the pattern it was saved with.
- **Position**: where on the neck the hand prefers to play, from the open position to the twelfth fret.
- **Accent**: how much stronger the accented notes are than the rest, from none to triple. Starts at a third of the way, the levels the patterns were written for.
- **Voicing**, **Picking** and **Ornament**: how much each kind of variation happens, from none to a lot.

THESE FOUR EXAGGERATE AT THE TOP. Each reaches half again beyond the most a player would do — at the top Accent triples the written contrast, every returning chord is held another way, about half the steps are changed, and about half the single treble notes are ornamented with a run into nearly every change of bass — so that turning one up makes plain what it does, and it can then be brought back to taste. The knob's top is 1.5 of the setting the Dynamics and Variation sections describe at one; Voicing, Picking and Ornament start at 20%, 17% and 17%, the same settings as before.
- **Palm mute**: how many of the thumb's notes are palm-muted, from none to all. A small knob between the two jacks. Starts at none.
- **Style**: from pop to jazz, as the Style section says. Starts at pop.

The shape is written lowest string first, with an x for a string left out.

Further controls — the bass layer's choices, a capo — are added as the styles that need them are built, not before.

## Inputs and outputs

- **MPX in**: mpxChart's cable.
- **MPX out**: the notes, their strings and frets, the harmony and the pedals passed on, and the instrument: six strings in standard tuning, named Finger-picked guitar. Notes arriving on the input are not passed on.

## Tests

`make fretboardtest` holds the generator to the shapes every guitarist knows. Asked for full chords in standard tuning, it must find the open C, G, D, A, E, Am, Em and Dm, the F and B barre chords, and the common sevenths, as their familiar shapes; where it disagrees, the scoring is tuned until it agrees. Then it checks the finger-picking requirements on the patterns' string sets: every candidate playable, the bass the root, the essential tones present.

`make pickertest` checks the fingering and the patterns. It finds the familiar open shapes on the strings the pattern plays; it asks for 47 chords, slash chords among them, at seven positions up the neck, and checks every shape found against every requirement; it plays C, A minor, F and G through every pattern, in a bar of four and a bar of three, and checks the output: on each step exactly the fingers the pattern names, each finger on its own string, the thumb on the bass, every note a tone of its chord, the hand within its limits, and the shape changing at each chord change; and it checks the dynamics: the first beat over the third, the thumb over the fingers, and a pinch's two notes at one level. It plays C, G, A minor and F eight times over with every variation at nought and then at the top: at nought, the seed changes nothing and each chord is held the same way every time; at the top, the same seed plays the same notes and another seed others, C is held more than one way, every shape meets every requirement, each kind of variation turns up, the thumb plays the bass on the first beat of every bar, every picked note is a tone of its chord, every hammer-on and pull-off lands on the held note from a scale note on its string half a step later, and every bass-run note is in the scale with the last a step from the new bass. It checks, in every pattern with Picking past its top, that no treble string is picked on two steps running unless the pattern itself does so. It plays C, A minor, F and G with Picking at the top and checks that the first three bars of each phrase play one figure, chord after chord, that the phrases' figures are not all the same, and that each phrase's last bar adds to its figure. It checks that on the step before each change every treble note is the next shape's, with Voicing at nought and at the top. It checks Style: C, G as the dominant, and A minor gaining their sevenths and colours at the top, a triad staying a triad below a quarter of the way, a written seventh kept as written; every shape, pop and jazz, meeting every requirement of its chord; and toward jazz fewer open strings, more sevenths and colours on the treble strings, fewer doublings and a higher hand, with C still x3x010 at nought. It checks Palm mute: none at nought, every thumb note at the top, some at a half but never on the first beat of the bar, never a finger's note, and the same steps muted in every bar of a phrase. It checks that Accent at nought plays every note alike and that the range widens as it rises. `make pickrender` plays the progression through the performer and the modelled guitar for sixteen bars at the module's starting settings, on a guitar at its starting Taper and Fret damping, once in each pattern, into build/picker-forward-roll.wav and its four neighbours, to be listened to.

## Build phases

1. **The fretboard library and its test**: tuning, the hand's limits, the search, and full-chord scoring checked against the familiar shapes. And the bass note on the cable: mpxChart's reader keeps a slash bass, the chord carries it, and mpxChart's bass output plays it.
2. **The module**: the MPX input and output, one folk pattern, finger-picking fingering with the position knob, the root bass. Played through mpxGuitar from mpxChart.
3. **The other folk patterns**, the pattern radio group, and the dynamics.
4. **Finish**: the panel laid out, the help text, and a demonstration patch.

## Still to come

- **Travis picking**: the alternating bass and its patterns.
- **Walking bass**: passing notes from one chord to the next, using the cable's look ahead.
- **Alternate tunings**: open D, open G and modal tunings, as lists of open notes for the fretboard library.
- **Strumming and lead modules**, each on the same fretboard library with requirements of its own.
