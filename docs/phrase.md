# mpxPhrase — design

A rhythm source for one melodic line. It reads the chart on its MPX input and places notes against the chart's bars, chord changes, cadences and phrases, then sends them on one MPX cable. It produces no pitch. Each note carries a level and a duration, and the pitch is filled in downstream by an mpxVoice.

This is a proposal. Nothing in it is built.

## What problem it addresses

A melody generator can only phrase as well as the rhythm it is given. mpxEuclid repeats a fixed number of steps with no knowledge of where the bar lines, the chord changes or the phrases fall, so a line driven by it drifts through the form and never arrives anywhere. Recordings of the melody engine driven by mpxEuclid confirmed this: the pitches followed the harmony and the rhythm did not.

A tune is memorable mainly through its rhythm. The same rhythm returning is heard as a composed idea even when every pitch differs. Rhythm is also far cheaper to remember and replay than pitch: it is onsets and lengths on a grid, with no chord to re-fit. Repetition therefore belongs here rather than in the melody.

## Where it sits

mpxChart's output goes to mpxPhrase's CHART IN. mpxPhrase's NOTES OUT goes to an mpxVoice's RHYTHM IN. The same chart cable also goes to mpxMelody's CHART IN.

One mpxPhrase drives one line. A bass and a melody that should phrase differently take two mpxPhrase modules from the same chart.

**Without a melody.** NOTES OUT is an ordinary MPX cable and goes to any MPX input, not only to an mpxVoice. Patched straight to mpxOut, the phrasing plays on its own: gate, level and duration drive any envelope, percussion voice or sound source, and the pitch output carries the note set by NOTE. That makes mpxPhrase usable as a phrase-shaped rhythm for a single pitch, a drum sound, or a modulation source, with no melody generator in the patch.

**NOTE sets the pitch the notes leave with,** C4 by default. An mpxVoice downstream replaces it with the melody's own pitch, so the setting has no effect when a melody is patched and cannot interfere with one.

## Independence from other rhythm modules

mpxPhrase depends only on the chart. It never reads the notes of any other module, and no other rhythm module reads its notes. A drum pattern generator in the same patch is independent in the same way.

They still agree, because they read the same chart. Both take their timing from the chart's beat, and both see the same bars, phrase boundaries, cadence types and sections. A drum fill at the end of a phrase and a breath at the end of the same phrase therefore coincide without either module knowing the other exists. With equal CYCLE settings and the same seed rule, their variations recur on the same schedule.

What this rules out is any behaviour that depends on another module's own choices: filling exactly the silence a breath left, locking a bass line to a kick drum, or avoiding a snare's backbeat. Those require one module to read another's results, and that is not done.

Every rhythm module in the family follows the same rule for timing: no clock input, only the chart's beat. That is what makes independence safe. A rhythm module on its own clock loses the agreement the chart provides.

## Timing comes from the chart, not from a clock

mpxPhrase has no clock input. Every onset is placed by the beat position the chart publishes on the cable. It therefore cannot drift from the harmony, and it stops, rewinds and loops exactly when the chart does.

The consequence is that mpxPhrase does nothing without a chart. Nothing on the panel says so: neither lamp flashes and no note leaves, which is what a module with no chart looks like, and the help says it in words. A notice across the panel said the same thing and took room a control could use.

**A chart with no phrasing is played in four-bar phrases,** measured off the beat — the same fallback the chart itself uses for a stretch it cannot phrase. Without one, a module reading the phrase length from the cable would sit at the start of a phrase of no length for ever.

**A control moved part-way through a phrase takes effect at the start of the next sub-phrase.** Waiting for the next phrase would make a knob feel dead for several bars; acting at once would cut a breath group in half. The phrase is decided again from where the music is, so what has already sounded is left alone.

**Controls that are on the panel before their milestone say so.** Rack saves a parameter by its number, so a control added later would move every number after it and misread every patch saved before. REPEAT, CYCLE, SECTIONS, ELIDE and RECORD are therefore declared from the start, and their names in the help say they are not built yet.

**The tempo is measured from the chart.** The harmony block carries the beat position but not the tempo, so mpxPhrase measures how fast the beat is advancing. Sub-phrase lengths are set in seconds and converted to beats at that rate, for the reason given under Sub-phrases.

## Controls set likelihoods, and a seed chooses

Every control sets how likely something is, never a fixed outcome, and every choice is drawn from a seeded generator. A different seed therefore gives a different phrasing that still obeys every setting, and the same seed always gives the same phrasing back. The controls define the range of what is reasonable; the seed picks one answer within it.

VARIATION sets how far the draws may wander from the middle of their range: how long a pause actually lasts against the length asked for, where within its range a group starts, how far a note's length strays. At nought those take their middle value, so a pause is exactly the number of beats set and nothing wanders; at one they use their whole range. It is independent of the controls that say what is drawn: a plain line and an adventurous one can come from the same density, syncopation and group settings.

**It does not touch a plain yes or no, and this was a fault worth recording.** VARIATION used to sharpen those too, so at nought a draw simply asked whether its probability was better than even. Density at a half then sounded every slot above the middle and none below — twelve identical quarter notes in a row, the same in every phrase, with a one-in-six chance of a triplet that never once came up. A probability is already a proportion: density at a half means half the slots whatever else is set. So VARIATION belongs on the magnitudes and on nothing that is a yes or a no, and the seed decides the pattern even with VARIATION at nought.

VARIATION is applied as a temperature on each weighted choice. Every candidate's weight is raised to a power that grows as VARIATION falls, so at nought the heaviest candidate takes all of the probability and at one the weights are used as they are.

## Two levels: the phrase and the sub-phrase

A line is organised at two levels, and they are decided by different things.

**The phrase** is a four-bar unit of the form. It decides the kind of ending, the pairing of phrases that drives REPEAT, and what CYCLE counts. The chart computes it, because only the chart holds the form and the harmony.

**The sub-phrase** is a breath group inside a phrase, typically about three seconds long. It decides where the line pauses. Most sub-phrase endings are not cadences, so they cannot come from the harmony; mpxPhrase generates them from time and the metre.

The evidence for this split is set out under Evidence below. In short: in the lead sheets measured, fewer than a quarter of sub-phrase endings fell at a cadence, while two thirds of closing cadences were followed within a bar by a sub-phrase ending. Cadences end sub-phrases; most sub-phrases do not end at cadences.

## Phrases, from the chart

**A phrase is four bars, counted from the start of its section.** A section whose length is not a multiple of four gives its leftover bars to its last phrase, and a section shorter than four bars is one phrase. The form comes first and the harmony second: in a pop song the phrases fall on the four-bar grid whatever the chords do, and the chords only colour how each one ends.

**How it ends is the cadence formed by its last chord change,** when that change arrives in the phrase's final two bars; otherwise it ends with none. A close earlier in the phrase, or one followed by another chord, is passing.

**Cadence types.** The chart classifies each phrase ending and publishes the type:

- **authentic** — dominant to tonic, a full close
- **plagal** — the four chord to the tonic, a softer close
- **backdoor** — flat seven dominant to the tonic
- **tritone substitute** — flat two dominant to the tonic
- **half** — a phrase ending on the dominant, a pause that expects an answer
- **deceptive** — dominant to the six chord, a close that is evaded
- **none** — a phrase whose last chord change is not a cadence

**A half cadence cannot be read from chords alone.** It is a phrase ending on the dominant, and ending is a melodic fact. The chart's sign for it is a dominant that arrives and is held for a bar or more. Because mpxPhrase generates the rhythm and mpxMelody generates the pitch, both can be told that a half cadence is intended, and between them they make it one: mpxPhrase ends the phrase there, and mpxMelody lands on the dominant's root, the key's fifth degree.

**Sections remain hard boundaries.** A phrase never crosses the start of a section.

## Sub-phrases, from time and the metre

Inside each phrase, mpxPhrase divides the time into sub-phrases. The division works from the top down.

**Length is set in seconds, not bars.** In 456 jazz solos, as the tempo rose nearly fourfold, from about 63 to 241 beats per minute, the median phrase grew from 3.5 beats to 9.4 while its duration fell only from 3.4 seconds to 2.3. Players keep a phrase to roughly the same few seconds and fit more beats into it at speed. A cross-cultural study of recorded music likewise finds short phrases to be a statistical universal and attributes them to the need to breathe. A length fixed in bars would give breathless lines at slow tempos and choppy ones at fast tempos.

1. **Divide the phrase into groups** of GROUP seconds, converted to beats at the measured tempo and rounded to the grid. VARY allows the groups to fragment toward the end of the phrase, for example two, two, then one, one, two, which is the sentence form, and occasionally to combine or run long.
2. **Breathe at a bar line.** Of the two bar lines either side of where the length in seconds would end a group, the nearer is taken — the later when they are as near — as long as the group still sounds for between three fifths and half as long again as asked. A group shorter than a bar and a half breathes at the half bar instead, beat three in four. The group ends in the bar before its boundary and the next line begins in the bar after, on its downbeat or with a pickup into it: a breath that ended wherever the seconds ran out put the next line's entry on the third beat of a bar, where a singer never breathes.
3. **Draw where the last note of each group falls.** ENDING sets the mix of endings, from landing on a strong beat, to landing off the beat. Each group draws its own ending from that mix, and the extremes favour one kind strongly without making it certain. The range has to be wide: song melodies ended on beat one half the time, jazz solos only one time in eight.
4. **Hold the last note.** HOLD sets how long a group's held ending is: the notes stop early enough to leave it one beat at nought and two at one — a little longer than the notes before it, as in the pop songs, where it lasted a beat and a half and the note before it an ordinary half beat — and the note that begins it always sounds where it was planned. The last note is longer than the ones before it in most phrases of every corpus — four in five in the pop songs, whose median held ending was a beat and a half; three beats in Killing Me Softly — so it is held more often than not at every setting, and more often the higher HOLD is. In the upper half of its range HOLD also lets the ending ring on into the breath after it, more often the higher it is; below the middle the breath stays silent.
5. **Leave the pause.** PAUSE sets how long the line stops moving at the end of every group, in beats, and PHRASE PAUSE is added at the end of a phrase. Each pause varies a little around its setting, by an amount scaled by VARIATION. Silence in real lines is frequent and short: the median silence between jazz phrases was 1.9 beats, and a silence of two bars or more happened about once in a hundred. Long pauses are therefore a deliberate choice rather than the norm.
6. **Draw how the next group starts.** START sets the mix of a pickup before the downbeat, a start on the downbeat, and a start off the beat. Each group draws its own start from that mix. The range has to be wide here too: in thirty pop songs half the phrases led in from the bar before and only one in six started on the bar line, while jazz phrases started off the beat more than half the time and on the downbeat only one time in ten.
**A phrase leads in from the phrase before.** A phrase's first group may begin with a pickup of up to two beats before its first bar line. **How each group starts is drawn before the group before it is filled,** and that group ends early enough to leave the pickup room after its breath: the pause is always the silence heard, and the pickup follows it — the held last note, the breath, then the lead-in. Drawing the start inside the group itself left a pickup only whatever silence happened to remain, so a short pause left none and the pickup turned into a late start, which made the breath longer rather than shorter. Across a phrase boundary the ending phrase draws how the next one starts; mpxPhrase then decides the next phrase two beats before it begins, from the chart's description of it, and plays its pickup in the room left for it. The first phrase after a start or a rewind has nothing before it; a pickup drawn for it becomes a late start, since a singer who cannot lead in comes in after the beat rather than on it. A pickup is part of the phrase it leads into: it recurs with that phrase under CYCLE, REPEAT does not overwrite it, and the melody draws it as that phrase's.

7. **Leave whole phrases out** by SILENT PHRASES, the chance that a phrase is not played at all. About one phrase in twenty-five was unsung in the lead sheets.

Chord changes play no part in where a group ends. In the jazz solos a change fell within a beat of a phrase's last note 45 per cent of the time, against 47 per cent for any note; in the song melodies the effect was weak. A control for it is not provided.

This needs only time, the metre and the phrase length, so it works identically on a chart with no cadences: a vamp becomes a run of groups.

## Filling a sub-phrase with notes

The steps below fill the inside of each group. Every random choice draws from a seeded generator, so the same seed and the same position always give the same pattern.

**The grid.** A bar is divided into slots by SUBDIVISION: quarter notes, eighth notes, sixteenth notes or eighth-note triplets. The time signature comes from the cable. Each slot has a metric strength: one for the downbeat, seven tenths for the middle of an even bar, one half for a beat, three tenths for an eighth off the beat, and two tenths for a sixteenth. These are GXW's values extended to finer subdivisions.

**Onsets.** A slot's onset probability is DENSITY multiplied by a weight derived from its metric strength. SYNCOPATION blends that weight toward its inverse: at nought strong slots are favoured, at one half every slot is equal, and at one weak slots are favoured. This is GXW's rule unchanged.

**Chord changes.** A slot on which the chord changes has its onset probability raised by ON CHANGES, and ON CHANGES also sets how far the rhythm follows the harmony, as measured in the thirty pop songs with their chords (`python3 research/pop/harmony_rhythm.py`):

- **Quick chords get more notes.** A chord of one beat had two notes under it, one of two to four beats about a note a beat, one held for two bars about six in ten. So DENSITY is scaled by how long the chord sounding lasts — by the square root of two beats over its length, between a half and one and a half.
- **A note that lands on a change is held.** The note starting on a change lasted a beat, median, against half a beat for other notes. So after a note on a change, on a chord of two beats or more, the slots up to a beat on are left empty — on a quicker chord the line keeps moving.
- **A fifth of changes are anticipated.** Of 875 changes, 60 per cent had a note start on them, 20 per cent a note half a beat before held across them, and 20 per cent neither. So a change is anticipated three times in ten at the top of the knob, with nothing on the change itself.

At the top of ON CHANGES the generator's changes come out at 62, 20 and 18 per cent, against the songs' 60, 20 and 20.

**The group's first slot** sounds, at the position drawn for it by START.

**No long gaps.** Inside a group no silence may be as long as the breath that ends it, or a listener hears the phrase end in the wrong place. A gap is measured as silence, not as the space between onsets: a note sounds on for up to a slot and twice LENGTH again, so a gap between onsets counts as a hole only when it is longer than three quarters of the breath plus that. Measured from onset to onset, a short breath filled every group to a solid run of eighth notes whatever DENSITY said.

**Length.** A note lasts until the next onset, scaled by LENGTH. At nought notes are short and separated; at one they reach the next onset, which is legato within the group.

**Level.** Set last, in decibels around a middle value, from four things, all scaled by DYNAMICS:

- **The phrase's own level.** The second phrase of each pair answers a decibel and a half stronger than the first; a contrasting section — any lettered section but A, so a chorus or a bridge — is two decibels louder; the music builds by two decibels across each pass of the form; and each phrase wanders by up to a decibel and a half either way, keyed on its place in the cycle so a returning phrase returns as loud.
- **The shape across the phrase,** by LOUDNESS SHAPE: at nought a fall from three decibels above to three below; at one an arch, peaking two fifths of the way through, which is how a sung phrase goes. The jazz solos fall the same way, but by three decibels in all on average; an average over eleven thousand phrases is flatter than any one of them, and at that depth nobody could hear a phrase rise and fall.
- **Stress.** In the pop songs a stressed syllable was the long note and the note on a strong beat, and a quarter of stressed syllables were anticipations. So a note of a beat or more, and a note anticipating a beat, is a decibel and a half louder; the metric pulse under it is light, three quarters of a decibel either way, since in the jazz solos a note on the beat was only a third of a decibel above one off it; and a note on a chord change is half a decibel louder.
- **The arrival tapers:** a phrase's last note is two decibels softer.

The figures are as written at a DYNAMICS of one half, and twice as deep at one.

## Repetition

**REPEAT** sets how closely a unit restates the one before it. At nought each is generated fresh; at one the onsets are copied exactly. Between them, each slot keeps the earlier decision with probability equal to REPEAT and is drawn afresh otherwise. REPEAT applies at both levels: a sub-phrase restating the previous sub-phrase, which is the two-bar idea stated twice at the start of a sentence, and a phrase restating the previous phrase. The final group of a phrase is always regenerated, so a restated phrase still ends in its own place.

**Motif.** MOTIF sets the chance that a breath group restates the rhythm of one of the three groups before it — the one just before half the time, the one before that three times in ten, the one before that twice in ten — whether those groups are in this phrase or the one before. In the thirty pop songs a third of the lines restate the rhythm of one of the three lines before them, in that proportion; at a ballad's tempo a phrase is often one line, so the line restated is usually in the phrase before. The copy is the source group's notes up to its ending, moved by whole beats so each note keeps its place in the beat. It runs from the start of the group until it reaches the group's own ending or runs out, and the group then carries on with its own notes. The group's ending and its breath are its own, and a copied note never starts before the group before has finished sounding. Each copied note is marked on the cable with how many notes back the note it restates is, so a melody can bring back the pitches as well: see Echo, below. At the POP presets a third of the lines restate one, as in the songs; the JAZZ presets restate fewer.

**Question and answer.** A phrase ending in a half cadence followed by a phrase ending in a closing cadence is the period form. The second phrase restates the first by REPEAT and ends differently. This uses the cadence types the chart publishes.

**CYCLE** sets after how many phrases the whole sequence of variations returns. Each phrase draws its variation from the seed combined with its position within the cycle, counted from the top of the form. With CYCLE at six, phrases one to six are six variations and phrase seven is phrase one again. At one, every phrase takes the same variation. A REPEAT chain restarts at the beginning of each cycle, or the cycle would not recur. This is the behaviour of GXW's cursor running slower than the chart, without a path.

**Recurrence and the form.** The pattern of onsets recurs exactly after CYCLE phrases. Its fit to chord changes recurs only when the form does. The rhythm heard therefore recurs exactly when CYCLE is a multiple of the number of phrases in the form, and otherwise recurs as the same rhythmic idea fitted to different changes.

**Returning sections.** SECTIONS makes the phrases of a returning section reuse the patterns generated for that section's first appearance.

**Elision.** ELIDE lets a closing cadence's arrival also be the first note of the next phrase, with no breath between them. At nought every phrase breathes; turned up, more phrase joins run straight through.

## The seed

The module has a SEED plate, nought to 999, and an OWN SEED button.

**With OWN SEED off**, the seed used is the seed on the chart cable plus SEED. This is the ordinary case. The chart's seed is what makes a whole patch reproducible, and adding to it rather than replacing it keeps that true while letting this module be varied on its own. Two mpxPhrase modules on one chart with different SEED settings give two independent lines, and changing the chart's seed varies all of them at once.

**With OWN SEED on**, the chart's seed is ignored and SEED is used alone. This keeps this module's phrasing fixed while the chart's seed is changed to vary everything else in the patch, which is the way to hold a phrasing you like while exploring the rest.

**When the cable carries no seed** — no chart patched, or a chart that has not published one — SEED is used alone whatever OWN SEED says, so the module still behaves predictably rather than drawing from an arbitrary number.

The same rule, the same plate and the same button belong on mpxMelody, whose seed at present is only added to the chart's. One rule across every MPX module that draws at random means a patcher learns it once.

## Recurrence in mpxMelody

mpxMelody's unpatched draw is `melodyDraw` in Melodic.hpp: a pure function of the seed, the phrase's position within a cycle of CYCLE phrases, and the position of the note inside that phrase. With CYCLE at four, phrase five draws exactly as phrase one did; at one, every phrase is the same line. Equal cycles on mpxPhrase and mpxMelody bring rhythm and pitch round together, and unequal ones recur only when both coincide — six against four after twelve phrases.

**Two things are deliberately absent from that hash, and no line recurred while they were in it.** The pass counter, so every time round the form drew differently. And the note's handle, which is unique for the session and counts upward for ever, so even the same beat of the same bar drew differently on the second pass.

**A rewind restarts the whole chain.** The chart returns its pass counter to nought rather than advancing it, so every module that folds the counter into its draws begins where it began before; mpxPhrase drops its repeat chain, since what a phrase restates is the phrase immediately before it; and mpxMelody clears each voice's last note, because a line carried across a rewind starts with an interval from the previous take. A loop still advances the counter: running off the end of the form and back to the top is another pass through the music.

## Arrival: the phrase anchor

A line that does not know where its phrase ends cannot arrive anywhere, and that is most of what separates a melody from a series of notes in the right rhythm.

The chart publishes how the current phrase ends and how far off that end is. Within two beats of it the melodic step is pulled toward the pitch classes that make an ending sound like one: the tonic at a closing cadence, because that is what closing means and the ear is waiting for it; the dominant's root at a half cadence — the key's fifth degree — because it sits over a dominant without resolving, where its third, the leading tone, its seventh and its fifth, the key's second degree, each leave the line suspended, which is what leaves a phrase open on purpose and makes the next one an answer.

The pull grows as the end approaches — nothing two beats out, strongest on the arrival — and there is none anywhere else in a phrase. A tonic pull applied throughout would make every note the tonic. Measured in `make melodytest`: the anchor puts a phrase end on the tonic in half of all draws, against none without it.

## What the rhythm tells the melody

A melody hears notes one at a time, and a phrase breathes after its last note, so a melody counting down to the phrase's end misses the note that matters: the last note falls two or three beats before the end, and the arrival was never aimed at all. The rhythm source knows which note is which, so it says so. Every note mpxPhrase sends carries flags on the cable:

- **Arrival** — the last note of the phrase. The voice takes it on the tonic at a full close, decisively, and on one of the tones of the dominant actually sounding at a half cadence: in a minor key that dominant is raised, and the natural minor's seventh against it is the one note guaranteed to sound wrong.
- **Approach** — the note before the arrival, when it is in the same group. Before a full close the voice takes it on the second or the seventh, so the tonic is arrived at rather than leapt to.
- **Group end** — the last note before a breath inside the phrase. The voice takes it on the root, third or fifth of the chord sounding, never its seventh or added sixth, so every breath is held on a consonance.
- **Pickup** — a note leading into the next phrase, sounding in the last beats of the one before. It sounds where the voice would otherwise be pulling toward the ending phrase's arrival, so the voice leaves it out of that pull, and draws it from the next phrase's place in the cycle, counted back from that phrase's bar line.

Two numbers ride with the flags:

- **Echo** — how many notes back the note this one restates is, when MOTIF has copied a group's rhythm; nought otherwise. The voice keeps every note it has chosen and looks that far back. Its own MOTIF knob sets how surely it then takes the same pitch. Where that pitch does not fit the chord now sounding, the same step moved to where the line is favoured less strongly, and failing that a move the same way by about as far, so a figure over new harmony is still heard as the same figure. In the pop songs a line restating an earlier line's rhythm takes the same notes a third of the time and new ones half the time; moving the same steps elsewhere is rare.
- **Along** — how far through its breath group the note falls, nought at the group's first note and one at its last. The voice's CONTOUR knob pulls each group toward the shape of a sung line, measured in the same songs: the high point comes early — a quarter of the way through at the median, and at the very first note in a quarter of the lines — and the line falls about four semitones from it to its last note, which ends about where the line began. So the pull aims four semitones above the group's first note a quarter of the way through, and one below it at the end. At full, a note three semitones from the aim keeps a seventh of its weight.

A rhythm source that sets no flags, such as mpxEuclid or a keyboard, leaves the voice to the countdown it used before, with no echo and no contour.

**Held endings, the key and borrowed chords.** Three rules keep a line sounding right when the harmony leaves the key:

- **An ending held into the next chord belongs to both.** Where a line's last note will still be sounding when the chord changes, it is chosen from the tones of the current chord that are also in the next chord's triad, when there are any: over B flat 7 moving to E flat 6 in F, B flat rather than F.
- **The melody stays in its key while the harmony borrows.** A note outside the key is a third as likely as it would otherwise be, and a note and its own alteration — A and A flat — a twelfth as likely within two notes of each other, which a listener hears as the melody slipping out of tune.
- **Over a diminished chord the line moves through its tones and holds only those in the key.** In Michelle the melody over D diminished in F climbs F, A flat, B natural and steps down to G on the C that follows: every tone of the chord, moving. Holding A flat or B, the tones outside the key, is what sounds wrong, so a long note over a diminished chord takes its tones in the key — D and F — and the others are passed through. Every other note of the key lies a semitone from one of its tones, so the palette is the chord.
- **A borrowed chord's own tones are not outside the key.** The pull away from notes outside the key applies to passing notes, not to the chord sounding: over B flat 7 in F, A flat is the chord.
- **A line moves through a chord and arrives at the next by step.** A skip of a third to a chord tone carries on the way it started — F, A flat, B over D diminished — rather than turning back as a leap does; and on the note that falls where the chord changes, the new chord's tones a step from the note before are favoured, as Michelle arrives at each chord: A to G onto C.
- **A long note is a chord tone.** A note held three quarters of a beat or more lands on the root, third or fifth of the chord, whatever CHORD LOCK is set to — a long note is heard against the harmony rather than passing through it.

**The arrival lands on the cadence chord.** In a phrase that ends at a cadence, the last note may not fall before the phrase's last chord change, and where the phrase has already begun to breathe when that chord arrives, the breath gives way: an ending in the right place with a shorter breath is an ending, and an ending over the chord before is not.

## How the chart reads a phrase

**Why the form comes first.** Read from cadence to cadence, pop songs split in the wrong places. In Killing Me Softly a G7 held for a bar looks like a half cadence two bars in, and nothing in the chords marks the end of bar four, where the song's phrase ends; in A Thousand Miles a chorus bar is played three times, each ending in a plagal close. Laid out in fours from each section's start, both come out exactly as they are sung: Killing Me Softly in 1–4, 5–8, 9–12, 13–16, 17–20 and 21–26, and A Thousand Miles in fours throughout. Both are kept as chord text in `research/pop/songs`.

**A phrase is not a breath.** Four bars is often longer than a singer goes without breathing; the breaths inside a phrase are mpxPhrase's groups, set in seconds. In Killing Me Softly each four-bar phrase is two sung lines with a breath between them.

## The examples, and the simulation

**The chart's picker has a built-in Examples playlist:** seven short progressions, all but one in C, each testing one thing — a period with a half cadence and a full close, the pop loop, ii–V–I twice, the same shape at two chords to the bar, a minor period, a single four-bar phrase, and the period twice for repetition to come round in. They are written as plain chord text in iReal Pro's notation and stored exactly as an imported song is, so nothing downstream treats them differently. `make phrasetest` checks every one of them is phrased as a musician reads it.

**`make phrasesim` says in words what a patch would play**: the chart's phrasing, the phrase generator and the voice's choice of note, run on the settings saved in the patch in Downloads, on its own chart or on any example. It runs the same code the modules run — the voice's choice lives in MelodyVoice.cpp so that the module and the simulation call one function rather than two copies — and it names what a listener can only call unmusical: a hole inside a group, a breath filled in, a phrase landing on the wrong degree.

## Controls

Grouped by what they decide.

**Pitch:** NOTE, a plate showing a note name, C1 to C7, default C4.

**Starting points are a style on the chart, and presets.** STYLE on mpxChart travels down the cable; when it changes, mpxPhrase sets its knobs to that style's rhythm and each melody voice to that style's line (see docs/chart.md). POP 1 to 3 and JAZZ 1 to 3 also ship as factory presets, so they appear in Rack's own Preset menu beside your own saved ones, with Rack's save, open, copy and paste. The preset files are generated at build time from the same `phraseStyle` the census in `phrasetest` measures, which is what keeps what ships equal to what was measured. A preset carries the whole module, the note and the seed included, as a preset does everywhere else in Rack.

**Sub-phrases:** GROUP, one to eight seconds. VARY, nought to one. START, bipolar, setting the mix from mostly pickups, through downbeat starts, to mostly off-beat starts. ENDING, nought to one, setting the mix from mostly strong-beat endings to mostly off-beat ones. MOTIF, nought to one, the chance a group restates an earlier group's rhythm.

**Pauses:** GROUP PAUSE, nought to two beats, at every group end. PHRASE PAUSE, nought to four beats — a bar — at a phrase end instead of the group pause, where a phrase holds a single group: one pause per boundary, and a phrase that is one group has no boundary inside it. Both step by half beats, and a pause is the whole of the silence: a late start comes out of it rather than being added to it. In the pop songs a breath between lines was a beat and a half in the middle and seldom over three, and a longer silence inside a phrase is heard as the line having stopped, so the knobs stop there; a patch saved with a longer pause plays at the limit. HOLD, nought to one. SILENT PHRASES, nought to one. ELIDE, nought to one.

**The notes inside a group:** SUBDIVISION, a plate. DENSITY, SYNCOPATION, ON CHANGES, LENGTH and DYNAMICS, each nought to one. LOUDNESS SHAPE, nought to one, from a fall to an arch, sits with the controls that act across a whole phrase.

**Repetition and chance:** REPEAT, nought to one. CYCLE, a plate from one to sixteen phrases. SECTIONS, nought to one. VARIATION, nought to one. SEED, a plate from nought to 999, with an OWN SEED button beside it.

**Recording:** a RECORD button, as on mpxMelody.

**Jacks:** CHART IN and NOTES OUT, both MPX. DENSITY CV, a control voltage added to DENSITY, nought to ten volts over the full range. Further control-voltage inputs are deferred until the module has been played.

**Defaults, from the evidence:** GROUP three seconds; VARY low; PAUSE about two beats and PHRASE PAUSE a little longer; HOLD toward silence, so a sustaining synthesizer patch still leaves audible space; SILENT PHRASES near nought; VARIATION near the middle. START and ENDING take their defaults from the SONG style: START at its middle, which is the pop songs' mix of mostly pickups and late starts, and endings favouring the strong beat.

**The presets, in three weights of each.** POP 1 is a ballad — Michelle, Killing Me Softly: few notes, mostly quarters, lines held at their ends and entered late, the loudness arching. POP 2 is the thirty pop songs' own middle. POP 3 is up-tempo: busier, pushed, more pickups and anticipations. JAZZ 1 is a jazz ballad; JAZZ 2 the jazz solos' own figures; JAZZ 3 up-tempo bebop, dense, off the beat, short notes and more triplets. Each ships for mpxPhrase and for mpxVoice under the same name, since a phrase preset's rhythm and a voice preset's line are made to go together. The module's own defaults are SONG, from the lead sheets, which is not a preset.

## What moves out of mpxVoice

Pauses are decided by mpxPhrase, so BREATH on mpxVoice should be at nought whenever mpxPhrase is upstream: with both turned up the two modules pause on top of each other and every phrase ends twice.

It is kept rather than removed. A voice fed by mpxEuclid, or by any other rhythm source that does not phrase, has no other way to breathe — and removing a parameter renumbers every one after it, which would misread every patch saved before. Whether it goes is still open, and it is tied to the question of whether a melody may alter the rhythm's rests.

## Additions to the harmony block on the cable

All are computed by mpxChart, which alone holds the whole progression.

- **The cadence type at the end of the current phrase**, from the list above.
- **The chord changes inside the current phrase**, as a count and up to sixteen offsets in beats from the start of the phrase, so that a phrase can be generated whole.
- **The number of phrases in one pass of the form**, so that phrases can be counted from the top across passes, which CYCLE depends on.
- **The section letter** of the current phrase, **which appearance of that section** this is, and **the phrase number within the section**, which SECTIONS depends on.
- **The next phrase**, described by the same fields — its length, cadence, chord changes, section and place in the form, and the pass it falls in, which is one more than the current pass after the last phrase of the form — so that a phrase can be decided before it begins and lead in with a pickup.

The phrase boundaries are four-bar units from each section's start, with each phrase's cadence read from its last chord change.

## Evidence

These measurements informed this design, and published research is cited where it bears on a decision. All the measurements can be rerun.

**Cadences in 2,137 iReal charts.** `make charttest ARGS="--cadences"`, with no phrase-length preference applied.

- Kinds found: 9,485 authentic, 6,464 dominants held a bar or more, 4,337 plagal, 764 deceptive, 447 backdoor, 330 tritone substitute.
- 94 per cent of cadence arrivals fall on beat one.
- 169 charts, eight per cent, contain no closing cadence at all.
- Bars between one closing cadence and the next: two bars 25 per cent, four bars 21, one bar 11, three bars 7, six bars 7, eight bars 6.5.

Closing cadences are therefore more frequent than four- or eight-bar phrases, and many are not phrase ends. That is why a phrase's ending is read only from its last chord change.

**Breath groups in 22 MusicXML melodies**, sixteen jazz and Latin lead sheets and six classical pieces. `python3 test/subphrase.py held`. A sub-phrase ends at a rest of an eighth note or longer, or at a held note of at least a half note running straight into the next note. 432 sub-phrases.

- Independent check: 73 per cent of lyric words followed by punctuation fall exactly at a sub-phrase end found this way.
- Length: under a bar 18 per cent, about one bar 23, one to two bars 17, about two bars 19, about three bars 9, about four bars 6, over four bars 8. Seventy-seven per cent are two bars or shorter.
- Start: pickup 37 per cent, on the downbeat 37, after the downbeat 22.
- Last note: on beat one 48 per cent, off the beat 30, mid-bar 14, on another beat 9.
- Last note longer than the notes before it: 71 per cent.
- A chord change within a beat of the last note: 62 per cent, against 53 per cent for any note.
- Silence is 6 per cent of the melodies' span. The time from a group's last note starting to the next group's first note is two to three beats 61 per cent of the time and four beats or more 29 per cent: the line stops for several beats, mostly on a held note rather than in silence.
- Phrases with no melody note at all: 6 of 162.
- Ending bar within a four-bar unit: bar four 30 per cent, bar two 27, bar three 22, bar one 21.
- In lead sheets with chord symbols, the harmony at a sub-phrase end was a closing cadence or a dominant in 22 per cent of cases.
- Of 77 closing cadences in those lead sheets, 65 per cent were followed within a bar by a sub-phrase end.

**Phrases in 456 jazz solos**, from the Weimar Jazz Database: 11,082 phrases marked by the transcribers and 200,809 notes. `python3 test/weimar.py`. The database is 42.5 MB under the Open Database Licence and is kept outside this repository, at `~/Documents/MusicResearch/weimar/wjazzd.db`.

- Length: median 2.7 seconds, middle half 1.5 to 4.4 seconds; median 13 notes, middle half 8 to 24; median 6.8 beats.
- Length by tempo, median beats and seconds: slow, about 63 beats per minute, 3.5 beats and 3.4 seconds; medium slow 5.3 and 3.3; medium 6.1 and 2.9; medium up 6.8 and 2.6; up, about 241 beats per minute, 9.6 beats and 2.3 seconds. Wind players alone, 94 per cent of the phrases, give almost identical figures.
- Silence between phrases: median 1.9 beats, 0.67 seconds. Under two beats 54 per cent, two to four beats 33, one to two bars 12, two bars or more about 1.
- Start: off the beat 55 per cent, on another beat 23, a pickup into the next bar 12, on the downbeat 10.
- Last note: off the beat 62 per cent, on another beat 16, beat one 13, mid-bar 10.
- Last note longer than the notes before it: 56 per cent.
- A chord change within a beat of the last note: 45 per cent, against 47 per cent for any note.

**Phrases in thirty pop songs**, from a lead-sheet book, transcribed as rhythm, pitch and the stress of each syllable, without the words: 366 phrases and 2,645 notes. `python3 research/pop/analyze.py`; the format is in `research/pop/SCHEMA.md`. A phrase ends at a rest of an eighth note or longer, or where a line of the lyric ends with punctuation.

- Length: sung part median 5 beats, middle half 3.5 to 7; the breath after it median 1.5 beats, middle half 0.5 to 3. With its breath a phrase spans eight beats most often, then four and six.
- Start, measured from the nearest bar line: a pickup within two beats before it 47 per cent, within a beat after it 29, on it 16, later 8. Pickups are spread from half a beat to two beats.
- Last note: the longest in the phrase 79 per cent; median a beat and a half; an anticipation, off the beat and held across it, 43 per cent; on beat one 21.
- Onsets: on a beat 48 per cent, the eighth off the beat 39, a sixteenth 11. One note in five is an anticipation held across the beat.
- Repetition: a phrase's onsets overlap the phrase before it by more than half 21 per cent of the time, and some earlier phrase in the song 56 per cent.
- Pitch: repeated notes 29 per cent of intervals, against 5 in the jazz solos; steps about half; a quarter of phrases hold three or more repeated notes in a row; median phrase range five semitones.
- Prosody: a stressed syllable lasts a beat, median, against half a beat unstressed, and falls on beat one or three 45 per cent of the time against 9. A quarter of stressed syllables land on the eighth before a strong beat. The syllable before a full stop lasts a beat and a half, before a comma a beat. 73 per cent of phrases end on a stressed syllable.

**Published research.**

- On 1,705 German folk songs from the Essen collection, a rule placing a boundary at every rest was correct 99 per cent of the time but found only 45 per cent of annotated phrase boundaries; the best models relied mainly on gaps between onsets, meaning long notes. Pearce, Müllensiefen and Wiggins, ISMIR 2008. This agrees with both corpora above: phrases end more often on a held note than in a rest.
- Temperley's Grouper model prefers phrases of roughly eight to ten notes, alongside a rule favouring large gaps and one favouring parallel grouping. Folk melodies are sparser than jazz solos, whose median was thirteen notes.
- Short phrases are among the statistical universals found across 304 recordings from nine regions, attributed to periodic breathing. Savage, Brown, Sakai and Currie, PNAS 2015.
- The Essen collection's phrase marks were added by its encoders rather than taken from the original sources, which limits how far they can be treated as ground truth.

**Limits of the evidence.** The jazz solos are improvised, almost entirely by wind players, and busier than sung melodies; the findings in seconds should transfer to song-like lines better than those about beat positions, which are the most style-specific. The twenty-two lead sheets are a small sample and mostly jazz standards. The pop songs were transcribed from printed lead sheets by reading the page, so pitches are less certain than rhythms, and a phrase's first verse only is recorded where verses are stacked. Lead sheets differ in how they notate breaths. The held-note threshold was chosen, not derived. The cadence detection is by chord symbol only and cannot see key changes. The figures are strong enough to set defaults and ranges, and not strong enough to fix constants; those are tuned by ear with the recording log.

**Swing and triplets in the same 456 jazz solos.** `python3 test/swing.py`, over 200,809 notes. The swing ratio is the beat-upbeat ratio: within one beat, how long the first half lasts against the second, where one is even and two is a full triplet feel. It is measured at whichever level the player is dividing the beat — eighths where the beat is in two, sixteenths where it is in four — because measuring only the eighth level made slow tempos look straight: at sixty-five beats a minute almost no beat is divided in two. One figure per solo, so a long solo does not outvote fifty short ones; 22,574 eighth pairs and 7,717 sixteenth pairs, from the 344 solos with at least twenty of either.

- At the eighth-note level the median solo swings at 1.31, with the middle half between 1.16 and 1.46. A full two to one is not what players do.
- It depends on the tempo, and not in one direction. Around 120 to 200 beats a minute the median is 1.43; it falls to 1.36 by 240, 1.24 by 280 and 1.15 by 320, and it is also lower below 120, at 1.22. So the ratio peaks at medium tempos and flattens at both ends.
- Swung sixteenths are much flatter: median 1.10, middle half 1.02 to 1.18, and they are what slow tempos use — below 160 beats a minute the sixteenth level is where the dividing happens.
- The feel the transcribers named barely moves it. Pooling pairs rather than solos, swing reads 1.26, two-beat 1.33, funk 1.27 and latin 1.23. The ratio is a matter of tempo and player, not of a style setting.
- Triplet figures appear on 13 per cent of the beats that carry any note, and every solo but two uses at least one. But only 60 per cent of those beats sound the middle unit of the triplet — the other 40 per cent are the swung pair written as a triplet, which is what swing already produces. A true triplet event therefore falls on 8 per cent of played beats.
- They do not come in runs. Of the stretches of consecutive beats sounding a middle unit, 82 per cent are a single beat, 13 per cent two and 3 per cent three. So it is a figure that happens, not a mode the line goes into.

## Feel: swing on the chart, triplets in the phrase

**Swing is not a rhythm and does not belong to a rhythm module.** It is how the beat is divided, which is the chart's business already: the chart is what publishes the beat, the bar, the time signature, the phrase boundaries, the seed and the pass count — everything a patch has to agree on. Two rhythm modules are peers with no cable between them, so a swing control on each would let them disagree with no way to tell why, and making one feed the other is ruled out by the rule that neither depends on the other's results. So SWING is a control on mpxChart and a number on the harmony block, and everything downstream divides the beat the same way without being told.

**What the control sets is an amount, not a ratio.** The measured ratio peaks near 1.43 at medium tempos and flattens toward even at both ends, so a knob set to a literal ratio would be wrong at every tempo but one. The knob runs from none to full, and full is the measured ratio for the tempo the chart is running at, not two to one, which the corpus says players do not play.

**The chart does the converting, and publishes the answers.** The ratio depends on which division is being swung — eighths swing at about 1.4 and sixteenths at about 1.1 — so the harmony block carries a ratio for each level rather than one number to be interpreted. A module looks up the ratio for whichever division it is working at and shifts its notes by it. Nothing downstream knows anything about tempo curves, and two modules cannot convert the same amount differently, which is the disagreement putting swing on the chart was meant to prevent. The amount itself travels too, for anything that wants to know how hard the music is swinging rather than by how much to move a note.

**The grid stays on mpxPhrase.** Swing has to be shared because a swung line over a straight part is always wrong. A grid need not be: a drum part in sixteenths under a melodic line in eighths is how music is ordinarily built, and the two are peers. A grid on the chart would therefore be a setting every module has to be free to depart from, which is a control with no reliable effect. It also belongs beside the controls it only means anything with — density, length and syncopation, all of which act on the resolution it sets.

**How the deformation is applied.** Placement stays on an even grid, and swing is the last step before the pattern is handed over: both a note's onset and its end go through the same mapping, so a note that reached the next onset still reaches it and a gap stays a gap. A nudge applied to onsets alone would lengthen every second note and shorten every other. The group boundaries are mapped with the notes, or anything reading where a group sounds to would be comparing swung notes against even boundaries.

**Triplets are the middle unit, and they belong to the line.** On a swung line the eighth-note pair already is a triplet — the long part is two units, the short part the third — so a triplet figure is the line also sounding the middle unit that swing skips. That makes it one control on mpxPhrase rather than a second grid: TRIPLETS, an amount, the likelihood that a beat sounds the middle unit. Default about 8 per cent of played beats, from the measurement, applied per figure rather than per group, with the occasional pair or three of consecutive beats the corpus shows. Below a full swing setting the long-short pair does not line up with an even three-per-beat division, so a triplet figure is drawn evenly across its beat and the swing deformation is left off it for that beat.

## Recording

The RECORD button writes a file in the same folder and manner as mpxMelody's. A settings line is written at the start and whenever a setting changes, and records the seed actually in use and where it came from. One line is written per phrase, giving its position in the form and in the cycle, its cadence type, its sub-phrase division, and how many slots were copied by REPEAT. One line is written per note as it is sent.

## Testing

A command-line test in the manner of `make melodytest` runs the generator over real charts and reports, for each setting: sub-phrase lengths in seconds and in beats at several tempos, against GROUP and VARY, checking that seconds stay roughly level as tempo changes; start and ending positions against START and ENDING; the proportion of chord changes carrying a note against ON CHANGES; whether every group ends in its pause; whether the pattern after CYCLE phrases is identical to the first; whether VARIATION at nought gives the same phrasing for every seed; and whether the same seed gives the same phrasing on every run. The last three checks are exact and must never fail.

The generated statistics at the SONG and JAZZ styles are compared directly with the lead-sheet and jazz-solo figures above, which gives a check on whether each style produces lines that breathe like the music it is named for.

## Build order

1. Phrase boundaries and cadence types in mpxChart.
2. The other harmony block additions.
3. The generator as a pure function, with the command-line test.
4. The module: panel, timing from the chart, sending notes.
5. REPEAT and CYCLE, then SECTIONS and ELIDE.
6. The recurrence correction, CYCLE and the cadence-driven phrase anchor in mpxMelody.
7. Recording.

## Standalone mode — deferred, and how it would work

Everything above assumes a chart. Most of what mpxPhrase does needs only time and the metre: the sub-phrase division, START, ENDING, the pauses, HOLD, REPEAT, CYCLE, VARIATION and the styles. Only four things come from the chart — the beat, the metre, where the phrases fall, and the chord changes that ON CHANGES uses — and three of those can be supplied directly. So the module could make phrase-shaped rhythm for anybody in Rack, with no chart and no other MPX module in the patch.

This is deferred, not designed away. It is written down so the decisions are not made twice.

**Revealed from the right-click menu, not by an expander.** An expander would mean another entry in the browser, adjacency to get wrong, and an orphan state to describe, for the sake of a handful of settings. A setting that reveals part of the panel keeps the module one thing, and the plugin already resizes a panel and pushes its neighbours aside when the panel editor changes a width.

**Nothing extra is on the panel in the ordinary case.** No clock jack, no reset. The panel is control-heavy enough without inputs that a patch with a chart in it would never use.

**Hiding the band removes the cables on its jacks.** A hidden jack with a cable in it is an invisible connection, which is worse than losing the cable. Every MPX port already removes cables that cannot work on the frame after they appear, so this behaves as the rest of the plugin does, and it happens at the moment the setting is changed rather than quietly.

**The band on the left, revealed with the mode:** CLOCK and RESET inputs; BEATS IN A BAR; PULSES PER BEAT, since people clock at one pulse a beat, four, or twenty-four; PHRASE LENGTH in bars, which takes the place of the chart's four-bar units; and FORM LENGTH in phrases, so that every fourth or eighth phrase ends as a close — which keeps PHRASE PAUSE, question and answer, and CYCLE meaningful with no harmony at all.

**A band on the right, revealed with it:** GATE, LEVEL and a trigger at each phrase start.

- **GATE** is the note. Its length is the note's length, so LENGTH and HOLD are audible through it and no duration output is needed.
- **LEVEL** matters more than it looks. Every note carries a level from DYNAMICS, from its slot's metric weight and from the accent on a chord change; without the output, every note is the same loudness and the phrasing loses its accents, which is half of what makes a line sound played.
- **The phrase trigger** is the one thing this module knows that nothing else in a rack does. It can reset an arpeggiator, advance a sequencer or fire a fill, so the rest of a patch can agree with the phrasing.

Pitch needs no output: with no melody every note is the pitch NOTE sets, and an oscillator can be tuned to it.

**Two controls do nothing in this mode** and read as inactive rather than pretending: ON CHANGES, because there are no chord changes, and SECTIONS, because there is no form to return to.

**The clock is safe here, though it is refused with a chart.** A rhythm running on its own clock drifts away from a chart's harmony, which is why there is no clock input in the ordinary case. With no chart in the patch there is nothing to drift from: the phrases are counted from the clock itself.

**The cost is care rather than code.** The generator does not change. Two modes mean two ways for the module to be wrong, so every test would have to run both, and the help would need to describe both without making the ordinary case sound complicated.

## Deferred

Swing. Ratchets and fills, which belong mainly to the drum pattern generator. Whether the melody may alter the rhythm's rests. The drum pattern generator itself, which shares this module's grid, weighted draw and repetition machinery but is a separate module with its own controls.
