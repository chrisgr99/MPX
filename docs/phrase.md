# mpxPhrase — design

A rhythm source for one melodic line. It reads the chart on its MPX input and places notes against the chart's bars, chord changes, cadences and phrases, then sends them on one MPX cable. It produces no pitch. Each note carries a level and a duration, and the pitch is filled in downstream by an mpxVoice.

This is a proposal. Nothing in it is built.

## What problem it addresses

A melody generator can only phrase as well as the rhythm it is given. mpxEuclid repeats a fixed number of steps with no knowledge of where the bar lines, the chord changes or the phrases fall, so a line driven by it drifts through the form and never arrives anywhere. Recordings of the melody engine driven by mpxEuclid confirmed this: the pitches followed the harmony and the rhythm did not.

A tune is memorable mainly through its rhythm. The same rhythm returning is heard as a composed idea even when every pitch differs. Rhythm is also far cheaper to remember and replay than pitch: it is onsets and lengths on a grid, with no chord to re-fit. Repetition therefore belongs here rather than in the melody.

## Where it sits

mpxChart's output goes to mpxPhrase's CHART IN. mpxPhrase's NOTES OUT goes to an mpxVoice's RHYTHM IN. The same chart cable also goes to mpxMelody's CHART IN.

One mpxPhrase drives one line. A bass and a melody that should phrase differently take two mpxPhrase modules from the same chart.

**Without a melody.** NOTES OUT is an ordinary MPX cable and goes to any MPX input, not only to an mpxVoice. Patched straight to fromMPX, the phrasing plays on its own: gate, level and duration drive any envelope, percussion voice or sound source, and the pitch output carries the note set by NOTE. That makes mpxPhrase usable as a phrase-shaped rhythm for a single pitch, a drum sound, or a modulation source, with no melody generator in the patch.

**NOTE sets the pitch the notes leave with,** C4 by default. An mpxVoice downstream replaces it with the melody's own pitch, so the setting has no effect when a melody is patched and cannot interfere with one.

## Independence from other rhythm modules

mpxPhrase depends only on the chart. It never reads the notes of any other module, and no other rhythm module reads its notes. A drum pattern generator in the same patch is independent in the same way.

They still agree, because they read the same chart. Both take their timing from the chart's beat, and both see the same bars, phrase boundaries, cadence types and sections. A drum fill at the end of a phrase and a breath at the end of the same phrase therefore coincide without either module knowing the other exists. With equal CYCLE settings and the same seed rule, their variations recur on the same schedule.

What this rules out is any behaviour that depends on another module's own choices: filling exactly the silence a breath left, locking a bass line to a kick drum, or avoiding a snare's backbeat. Those require one module to read another's results, and that is not done.

Every rhythm module in the family follows the same rule for timing: no clock input, only the chart's beat. That is what makes independence safe. A rhythm module on its own clock loses the agreement the chart provides.

## Timing comes from the chart, not from a clock

mpxPhrase has no clock input. Every onset is placed by the beat position the chart publishes on the cable. It therefore cannot drift from the harmony, and it stops, rewinds and loops exactly when the chart does.

The consequence is that mpxPhrase does nothing without a chart. That is stated on the panel in the same way mpxMelody states a missing voice.

**The tempo is measured from the chart.** The harmony block carries the beat position but not the tempo, so mpxPhrase measures how fast the beat is advancing. Sub-phrase lengths are set in seconds and converted to beats at that rate, for the reason given under Sub-phrases.

## Controls set likelihoods, and a seed chooses

Every control sets how likely something is, never a fixed outcome, and every choice is drawn from a seeded generator. A different seed therefore gives a different phrasing that still obeys every setting, and the same seed always gives the same phrasing back. The controls define the range of what is reasonable; the seed picks one answer within it.

VARIATION sets how far the draws may depart from the most likely outcome. At nought each draw takes the most likely result, so the module produces the most typical phrasing its settings allow and the seed makes no difference. At one each draw samples the full range. It is independent of the controls that say what is drawn: a plain line and an adventurous one can come from the same density, syncopation and group settings.

VARIATION is applied as a temperature on each weighted choice. Every candidate's weight is raised to a power that grows as VARIATION falls, so at nought the heaviest candidate takes all of the probability and at one the weights are used as they are.

## Two levels: the phrase and the sub-phrase

A line is organised at two levels, and they are decided by different things.

**The phrase** runs from one cadence to the next. It decides the kind of ending, the pairing of phrases that drives REPEAT, and what CYCLE counts. Its boundaries come from the harmony, so the chart computes them.

**The sub-phrase** is a breath group inside a phrase, typically about three seconds long. It decides where the line pauses. Most sub-phrase endings are not cadences, so they cannot come from the harmony; mpxPhrase generates them from time and the metre.

The evidence for this split is set out under Evidence below. In short: in the lead sheets measured, fewer than a quarter of sub-phrase endings fell at a cadence, while two thirds of closing cadences were followed within a bar by a sub-phrase ending. Cadences end sub-phrases; most sub-phrases do not end at cadences.

## Phrases, from the chart

**A phrase ends at a cadence, not at a fixed length.** Phrase length is not assumed to be four or eight bars. Where cadences are found, a phrase is as long as the distance between them. A fixed length is used only as a fallback for harmony with no cadences in it: modal vamps, two-chord grooves and pedal points.

**Cadence types.** The chart classifies each phrase ending and publishes the type:

- **authentic** — dominant to tonic, a full close
- **plagal** — the four chord to the tonic, a softer close
- **backdoor** — flat seven dominant to the tonic
- **tritone substitute** — flat two dominant to the tonic
- **half** — a phrase ending on the dominant, a pause that expects an answer
- **deceptive** — dominant to the six chord, a close that is evaded
- **none** — a boundary found by the fallback, with no cadence at it

**Not every tonic arrival is a phrase end.** Counted across 2,137 charts, closing cadences fall most often two bars apart, and one bar apart more than one time in ten. Many of those are the tonic touched in passing, inside a turnaround, not the end of a phrase. A cadence is therefore taken as a phrase end only when it closes a section, or when it comes at least two bars after the previous phrase end. A tonic held for a bar or more does not bypass the two-bar minimum; checked against the lead sheets, allowing it found real breaths no better while making one phrase in six a single bar long.

**A half cadence cannot be read from chords alone.** It is a phrase ending on the dominant, and ending is a melodic fact. The chart's sign for it is a dominant that arrives and is held for a bar or more. Because mpxPhrase generates the rhythm and mpxMelody generates the pitch, both can be told that a half cadence is intended, and between them they make it one: mpxPhrase ends the phrase there, and mpxMelody lands on the second, fifth or seventh degree.

**Sections remain hard boundaries.** A phrase never crosses the start of a section.

## Sub-phrases, from time and the metre

Inside each phrase, mpxPhrase divides the time into sub-phrases. The division works from the top down.

**Length is set in seconds, not bars.** In 456 jazz solos, as the tempo rose nearly fourfold, from about 63 to 241 beats per minute, the median phrase grew from 3.5 beats to 9.4 while its duration fell only from 3.4 seconds to 2.3. Players keep a phrase to roughly the same few seconds and fit more beats into it at speed. A cross-cultural study of recorded music likewise finds short phrases to be a statistical universal and attributes them to the need to breathe. A length fixed in bars would give breathless lines at slow tempos and choppy ones at fast tempos.

1. **Divide the phrase into groups** of GROUP seconds, converted to beats at the measured tempo and rounded to the grid. VARY allows the groups to fragment toward the end of the phrase, for example two, two, then one, one, two, which is the sentence form, and occasionally to combine or run long.
2. **Prefer hypermetric edges.** Bars group into strong and weak positions as beats do. A group boundary is moved, where the division allows, to fall after an even-numbered bar of a four-bar unit rather than an odd one. The measured preference is mild, and this is a weight, not a rule.
3. **Draw where the last note of each group falls.** ENDING sets the mix of endings, from landing on a strong beat, to landing off the beat. Each group draws its own ending from that mix, and the extremes favour one kind strongly without making it certain. The range has to be wide: song melodies ended on beat one half the time, jazz solos only one time in eight.
4. **Hold or release the last note.** HOLD sets how the pause at the end of a group is filled: at nought the last note is short and the pause is silence; at one the last note sustains through it. The last note is longer than the ones before it in most phrases of both corpora, seven in ten for songs and more than half for jazz, so a longer last note is the tendency at every setting of HOLD above nought.
5. **Leave the pause.** PAUSE sets how long the line stops moving at the end of every group, in beats, and PHRASE PAUSE is added at the end of a phrase. Each pause varies a little around its setting, by an amount scaled by VARIATION. Silence in real lines is frequent and short: the median silence between jazz phrases was 1.9 beats, and a silence of two bars or more happened about once in a hundred. Long pauses are therefore a deliberate choice rather than the norm.
6. **Draw how the next group starts.** START sets the mix of a pickup before the downbeat, a start on the downbeat, and a start off the beat. Each group draws its own start from that mix. The range has to be wide here too: song melodies started on the downbeat or with a pickup about a third of the time each, while jazz phrases started off the beat more than half the time and on the downbeat only one time in ten.
7. **Leave whole phrases out** by SILENT PHRASES, the chance that a phrase is not played at all. About one phrase in twenty-five was unsung in the lead sheets.

Chord changes play no part in where a group ends. In the jazz solos a change fell within a beat of a phrase's last note 45 per cent of the time, against 47 per cent for any note; in the song melodies the effect was weak. A control for it is not provided.

This needs only time, the metre and the phrase length, so it works identically on a chart with no cadences: a vamp becomes a run of groups.

## Filling a sub-phrase with notes

The steps below fill the inside of each group. Every random choice draws from a seeded generator, so the same seed and the same position always give the same pattern.

**The grid.** A bar is divided into slots by SUBDIVISION: quarter notes, eighth notes, sixteenth notes or eighth-note triplets. The time signature comes from the cable. Each slot has a metric strength: one for the downbeat, seven tenths for the middle of an even bar, one half for a beat, three tenths for an eighth off the beat, and two tenths for a sixteenth. These are GXW's values extended to finer subdivisions.

**Onsets.** A slot's onset probability is DENSITY multiplied by a weight derived from its metric strength. SYNCOPATION blends that weight toward its inverse: at nought strong slots are favoured, at one half every slot is equal, and at one weak slots are favoured. This is GXW's rule unchanged.

**Chord changes.** A slot on which the chord changes has its onset probability raised by ON CHANGES.

**The group's first slot** sounds, at the position drawn for it by START.

**No long gaps.** Inside a group a run of empty slots longer than half the group is broken by an onset on the strongest empty slot in it.

**Length.** A note lasts until the next onset, scaled by LENGTH. At nought notes are short and separated; at one they reach the next onset, which is legato within the group.

**Level.** A note's level spreads around a middle value by its slot's metric strength, scaled by DYNAMICS. A note on a chord change receives a small further accent.

## Repetition

**REPEAT** sets how closely a unit restates the one before it. At nought each is generated fresh; at one the onsets are copied exactly. Between them, each slot keeps the earlier decision with probability equal to REPEAT and is drawn afresh otherwise. REPEAT applies at both levels: a sub-phrase restating the previous sub-phrase, which is the two-bar idea stated twice at the start of a sentence, and a phrase restating the previous phrase. The final group of a phrase is always regenerated, so a restated phrase still ends in its own place.

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

mpxMelody's unpatched random draw is at present seeded partly by the chart's pass counter, so every pass through the form produces a different line and the line never recurs. That is corrected as part of this work: the draw takes its variation from the position within a cycle, using the same rule as CYCLE here.

mpxMelody gains a CYCLE setting and an OWN SEED button on its master panel. When mpxPhrase and mpxMelody have equal CYCLE settings, rhythm and pitch recur together. When they differ, the combination recurs only when both cycles coincide; six against four recurs after twelve phrases.

mpxMelody's phrase anchor, which exists in the melodic step and is not yet connected, is driven by the cadence type: toward the tonic at a closing cadence, and toward the second, fifth or seventh degree at a half cadence.

## Controls

Grouped by what they decide.

**Pitch:** NOTE, a plate showing a note name, C1 to C7, default C4.

**Style:** STYLE, a plate offering starting points — SONG and JAZZ at least. Choosing one writes its values into the controls below, which can then be adjusted; it does not override them afterwards.

**Sub-phrases:** GROUP, one to eight seconds. VARY, nought to one. START, bipolar, setting the mix from mostly pickups, through downbeat starts, to mostly off-beat starts. ENDING, nought to one, setting the mix from mostly strong-beat endings to mostly off-beat ones.

**Pauses:** PAUSE, nought to eight beats, at every group end. PHRASE PAUSE, nought to eight beats, added at a phrase end. HOLD, nought to one. SILENT PHRASES, nought to one. ELIDE, nought to one.

**The notes inside a group:** SUBDIVISION, a plate. DENSITY, SYNCOPATION, ON CHANGES, LENGTH and DYNAMICS, each nought to one.

**Repetition and chance:** REPEAT, nought to one. CYCLE, a plate from one to sixteen phrases. SECTIONS, nought to one. VARIATION, nought to one. SEED, a plate from nought to 999, with an OWN SEED button beside it.

**Recording:** a RECORD button, as on mpxMelody.

**Jacks:** CHART IN and NOTES OUT, both MPX. DENSITY CV, a control voltage added to DENSITY, nought to ten volts over the full range. Further control-voltage inputs are deferred until the module has been played.

**Defaults, from the evidence:** GROUP three seconds; VARY low; PAUSE about two beats and PHRASE PAUSE a little longer; HOLD toward silence, so a sustaining synthesizer patch still leaves audible space; SILENT PHRASES near nought; VARIATION near the middle. START and ENDING take their defaults from the SONG style: roughly equal pickups and downbeat starts, and endings favouring the strong beat.

**The two styles, from the two corpora.** SONG: starts divided between pickup and downbeat, endings mostly on strong beats, the last note held. JAZZ: starts mostly off the beat, endings mostly off the beat, a higher DENSITY and SYNCOPATION, and shorter holds.

## What moves out of mpxVoice

Pauses are decided by mpxPhrase. The BREATH knob on mpxVoice is removed when mpxPhrase is built. It is not repurposed until the question of whether a melody may alter the rhythm's rests has been settled.

## Additions to the harmony block on the cable

All are computed by mpxChart, which alone holds the whole progression.

- **The cadence type at the end of the current phrase**, from the list above.
- **The chord changes inside the current phrase**, as a count and up to sixteen offsets in beats from the start of the phrase, so that a phrase can be generated whole.
- **The number of phrases in one pass of the form**, so that phrases can be counted from the top across passes, which CYCLE depends on.
- **The section letter** of the current phrase, **which appearance of that section** this is, and **the phrase number within the section**, which SECTIONS depends on.

The phrase boundaries themselves change: from the present four-and-eight-bar rule to cadence-driven boundaries with a fallback length.

## Evidence

Four measurements informed this design, and published research is cited where it bears on a decision. All the measurements can be rerun.

**Cadences in 2,137 iReal charts.** `make charttest ARGS="--cadences"`, with no phrase-length preference applied.

- Kinds found: 9,485 authentic, 6,464 dominants held a bar or more, 4,337 plagal, 764 deceptive, 447 backdoor, 330 tritone substitute.
- 94 per cent of cadence arrivals fall on beat one.
- 169 charts, eight per cent, contain no closing cadence at all.
- Bars between one closing cadence and the next: two bars 25 per cent, four bars 21, one bar 11, three bars 7, six bars 7, eight bars 6.5.

Closing cadences are therefore more frequent than four- or eight-bar phrases, and many are not phrase ends. That is why a filter is needed before a cadence is taken as a phrase boundary.

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

**The phrasing rule checked against those melodies.** `python3 test/phrasecheck.py` applies the rule to the chord symbols of the seventeen lead sheets that have them and measures the breath found within a beat of each phrase end, against every bar line as a baseline. A substantial breath, a half note or longer, lies within a beat of 40 per cent of bar lines, and of 51 per cent of the phrase ends the rule finds, with the mean breath rising from 1.6 to 2.2 beats. Minimums of three and four bars scored 54 and 56 per cent: slightly higher, within the noise of a sample this size, and at the cost of swallowing two-bar phrases, which the chart census shows to be the commonest cadence spacing. The minimum is two bars.

**The rule over the 2,137 charts.** `make charttest ARGS="--phrases"`. Every chart's phrases cover it with no gaps. Lengths: one bar 8 per cent, two bars 21, three bars 16, four bars 34, five bars 9, six to eight bars 11. Endings: authentic 25 per cent, half 23, plagal 14, backdoor and tritone substitute 3 between them, and none 35 per cent, which are section ends and fallback boundaries.

**Phrases in 456 jazz solos**, from the Weimar Jazz Database: 11,082 phrases marked by the transcribers and 200,809 notes. `python3 test/weimar.py`. The database is 42.5 MB under the Open Database Licence and is kept outside this repository, at `~/Documents/MusicResearch/weimar/wjazzd.db`.

- Length: median 2.7 seconds, middle half 1.5 to 4.4 seconds; median 13 notes, middle half 8 to 24; median 6.8 beats.
- Length by tempo, median beats and seconds: slow, about 63 beats per minute, 3.5 beats and 3.4 seconds; medium slow 5.3 and 3.3; medium 6.1 and 2.9; medium up 6.8 and 2.6; up, about 241 beats per minute, 9.6 beats and 2.3 seconds. Wind players alone, 94 per cent of the phrases, give almost identical figures.
- Silence between phrases: median 1.9 beats, 0.67 seconds. Under two beats 54 per cent, two to four beats 33, one to two bars 12, two bars or more about 1.
- Start: off the beat 55 per cent, on another beat 23, a pickup into the next bar 12, on the downbeat 10.
- Last note: off the beat 62 per cent, on another beat 16, beat one 13, mid-bar 10.
- Last note longer than the notes before it: 56 per cent.
- A chord change within a beat of the last note: 45 per cent, against 47 per cent for any note.

**Published research.**

- On 1,705 German folk songs from the Essen collection, a rule placing a boundary at every rest was correct 99 per cent of the time but found only 45 per cent of annotated phrase boundaries; the best models relied mainly on gaps between onsets, meaning long notes. Pearce, Müllensiefen and Wiggins, ISMIR 2008. This agrees with both corpora above: phrases end more often on a held note than in a rest.
- Temperley's Grouper model prefers phrases of roughly eight to ten notes, alongside a rule favouring large gaps and one favouring parallel grouping. Folk melodies are sparser than jazz solos, whose median was thirteen notes.
- Short phrases are among the statistical universals found across 304 recordings from nine regions, attributed to periodic breathing. Savage, Brown, Sakai and Currie, PNAS 2015.
- The Essen collection's phrase marks were added by its encoders rather than taken from the original sources, which limits how far they can be treated as ground truth.

**Limits of the evidence.** The jazz solos are improvised, almost entirely by wind players, and busier than sung melodies; the findings in seconds should transfer to song-like lines better than those about beat positions, which are the most style-specific. The twenty-two lead sheets are a small sample and mostly jazz standards. Lead sheets differ in how they notate breaths. The held-note threshold was chosen, not derived. The cadence detection is by chord symbol only and cannot see key changes. The figures are strong enough to set defaults and ranges, and not strong enough to fix constants; those are tuned by ear with the recording log.

## Recording

The RECORD button writes a file in the same folder and manner as mpxMelody's. A settings line is written at the start and whenever a setting changes, and records the seed actually in use and where it came from. One line is written per phrase, giving its position in the form and in the cycle, its cadence type, its sub-phrase division, and how many slots were copied by REPEAT. One line is written per note as it is sent.

## Testing

A command-line test in the manner of `make melodytest` runs the generator over real charts and reports, for each setting: sub-phrase lengths in seconds and in beats at several tempos, against GROUP and VARY, checking that seconds stay roughly level as tempo changes; start and ending positions against START and ENDING; the proportion of chord changes carrying a note against ON CHANGES; whether every group ends in its pause; whether the pattern after CYCLE phrases is identical to the first; whether VARIATION at nought gives the same phrasing for every seed; and whether the same seed gives the same phrasing on every run. The last three checks are exact and must never fail.

The generated statistics at the SONG and JAZZ styles are compared directly with the lead-sheet and jazz-solo figures above, which gives a check on whether each style produces lines that breathe like the music it is named for.

## Build order

1. Cadence-driven phrase boundaries and cadence types in mpxChart, with the census extended to check them against the lead sheets.
2. The other harmony block additions.
3. The generator as a pure function, with the command-line test.
4. The module: panel, timing from the chart, sending notes.
5. REPEAT and CYCLE, then SECTIONS and ELIDE.
6. The recurrence correction, CYCLE and the cadence-driven phrase anchor in mpxMelody.
7. Recording.

## Deferred

Swing. Ratchets and fills, which belong mainly to the drum pattern generator. Whether the melody may alter the rhythm's rests. The drum pattern generator itself, which shares this module's grid, weighted draw and repetition machinery but is a separate module with its own controls.
