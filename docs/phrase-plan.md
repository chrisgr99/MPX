# mpxPhrase — development plan

How the design in `phrase.md` is built. Each milestone ends with something that can be checked: a census, a test that must pass, or something to listen to. A milestone is not started until the one before it passes.

## Status

Every milestone up to and including 7 is built, and every control on mpxPhrase, mpxMelody and mpxVoice now does what its help says. What remains is milestone 8, the tuning, which is done by listening — against the built-in Examples, with `make phrasesim` and `python3 test/contour.py` to check a change before it is heard.

mpxVoice's ARTICULATION, ACCENT, BREATH and END NOTES AT CHANGES, its PROFILE input, its VOICE switch and the "from chord" scale were on the panel and did nothing until this point: the voice read them only into its log. They are built now, their decisions in `voiceShape` beside the voice's choice of note in MelodyVoice.cpp, checked exactly in `make melodytest`. The voice now ends its own notes, so it can hold a note past what the rhythm sent and slur it into the next; the role, not the register, decides which voice draws first.

Tuning toward pop phrasing is under way, against thirty transcribed pop songs in `research/pop`. From them: a phrase now leads in with a pickup before its bar line, decided two beats early from the chart's description of the next phrase; START's middle is the pop songs' mix of starts; a POP preset ships beside SONG and JAZZ; and mpxVoice has a REPEATED NOTES knob, since pop melodies repeat a note on 29 per cent of intervals against 5 in the jazz solos.

## Rules that apply throughout

**Tests before panels.** Every decision the module makes lives in a pure function with no Rack in it, and is checked from the command line before any panel exists. The melody engine showed the value of this: the strong-beat fault was invisible by ear and obvious in numbers.

**A build is reported as installed only when checked.** The installed plugin package must be newer than every changed source file, and any change to the plugin binary needs Rack restarted. Before saying a panel change is visible, the saved layout file for that module is checked for anything overriding it.

**Keys are never renamed without migrating the saved layout** in the same change.

**Test patches keep the Clarity module** from the autosave, with its settings.

**Nothing is committed** until reviewed and asked for.

**Audio-thread code does not allocate.** Generating a phrase happens on the audio thread when the phrase begins, so it works on fixed arrays sized for the longest phrase and the finest subdivision.

## Milestone 1 — Cadence-driven phrases in mpxChart

Replace the four-and-eight-bar phrasing rule with phrases that run from cadence to cadence.

**Work.** Move cadence detection into `ChartLayout.cpp` as a function that classifies each chord change: authentic, plagal, backdoor, tritone substitute, deceptive, half, or none. Apply the filter that decides which cadences are phrase ends: the end of a section, or at least two bars after the previous end. Use a fallback length for stretches with no cadence. Sections remain hard boundaries.

**Checks.**
- `make charttest ARGS="--phrases"` reports the new length distribution, with no phrase gaps in any chart. The contiguity check must still find nothing.
- The same filter, mirrored in `test/phrasecheck.py`, is run over the lead sheets' chord symbols. It reports how often a substantial melodic breath lies within a beat of a detected phrase end, against every bar line as a baseline, for several variants of the rule side by side.
- mpxMonitor shows the phrase and its cadence type while a chart plays.

**The fallback length is four bars.** Phrases sit above the breath groups, which already give two-bar breathing, so a stretch with no cadence becomes four-bar phrases that can each divide into two-bar groups.

**Risk.** The Python mirror of the filter can drift from the C++ rule. The census prints the thresholds it used so the two can be compared by eye, and the mirror is marked as a mirror in its comments.

## Milestone 2 — The rest of the harmony block

Publish everything mpxPhrase needs to generate a phrase whole.

**Work.** Add to the harmony block: the cadence type at the end of the current phrase; the chord changes inside the phrase as a count and up to sixteen beat offsets; the number of phrases in one pass of the form; the section letter, which appearance of the section this is, and the phrase number within the section.

**Checks.**
- The chart census confirms, for every chart, that the published change offsets match the chord changes the chart actually plays, that phrases per pass matches the count of phrases found, and that section appearances count up correctly through repeats and endings.
- mpxMonitor shows the section, its appearance and the phrase within it.

**Result.** `make charttest ARGS="--form"`. Over 2,137 charts: 111,234 chord changes published for phrases compared with the changes played, with no disagreements; no faults in section appearances or phrase numbers within sections; 47 phrases of 26,709 hold more than the sixteen changes the cable carries, and the cable says how many there really were. The chart module and the census resolve chords through the same function, `chartChordAt`, so the comparison checks the code the module plays through.

## Milestone 3 — The generator, as a pure function

`src/Phrasing.hpp` and `src/Phrasing.cpp`, with no Rack in them.

**Work.** One call generates a whole phrase. It takes the phrase length, the time signature, the subdivision, the chord change offsets, the cadence type, every control value, the seed, the position within the cycle, and the previous phrase's pattern. It returns the sub-phrase boundaries and, for every note, its offset, duration and level. It takes the tempo as an argument, in beats per second. It covers the sub-phrase division with GROUP in seconds, START and ENDING as drawn mixes, PAUSE, PHRASE PAUSE, HOLD and SILENT PHRASES, the notes inside each group, the SONG and JAZZ style starting points, and VARIATION as a temperature on every weighted choice. REPEAT and CYCLE are left to milestone 5.

**Checks — `make phrasetest`.** Run over real charts at each style and at several tempos, the generated statistics are compared with the evidence in `phrase.md`:
- sub-phrase duration in seconds stays roughly level as tempo changes, while its length in beats grows with tempo, as it did in the jazz solos
- at the SONG style, starts and endings divide roughly as the lead sheets did; at the JAZZ style, roughly as the jazz solos did
- silences between groups have a median near the PAUSE setting, and silences of two bars or more stay rare unless PAUSE asks for them
- every group ends in its pause, and a phrase end pauses longer than a group end

Exact checks, which must never fail:
- the same seed and settings give the same phrase on every run
- VARIATION at nought gives the same phrase for every seed
- no note falls outside its phrase, and no note overlaps the breath

**Result.** `make phrasetest`, over 1,326 real phrases at three tempos in both styles. At the SONG style: starts pickup 36 per cent, downbeat 37, after the beat 27, against the lead sheets' 37, 37 and 22; endings on a strong beat 62 per cent, matching; the last note the longest in its group 75 per cent against 71. At the JAZZ style: starts off the beat 55 per cent and on a downbeat 33, against the solos' 55 and 10 plus 23 on another beat; endings off the beat 62 per cent, matching; last note longest 58 per cent against 56. Group length holds 2.1 to 2.9 seconds at every tempo while growing from 2.9 to 11.5 beats, against the solos' median of 2.7 seconds. Pauses median 2.1 to 2.2 beats against the solos' 1.9. All four exact checks pass.

**Four faults the census found, three of them mine.** Sharpening the START and ENDING mixes by VARIATION turned an intended 62 per cent into 85, so a mix is now used as the proportion it is. GROUP was being measured against the group's whole span rather than its sounding part, which made every group short by the length of its pause. Snapping every boundary to the nearest bar line robbed groups of up to a third of their length. And making the last note the longest by construction let it run past the end of the phrase, which the check caught as one note in twenty thousand.

**Risk.** Defaults that match the lead-sheet statistics can still sound mechanical. The statistics are a floor, not a finish; milestone 8 is where the sound is judged.

## Milestone 4 — The module

mpxPhrase as a playable module.

**Work.** The panel, with every control present and labelled, for arranging in the panel editor. Timing from the chart's beat, with no clock input, and the tempo measured from how fast that beat advances. A phrase generated when the chart reaches its start, and notes sent at their times. Correct behaviour when the chart stops, rewinds, loops or has a section chosen: the pattern is regenerated from the new position rather than continued. NOTE, SEED and OWN SEED. A DreamerHelp entry for every control.

**When a setting changes mid-phrase,** the change takes effect at the start of the next sub-phrase. Waiting for the next phrase would make a knob feel dead for several bars; applying it mid-group would cut a breath group in half.

**Result.** `src/mpxPhrase.cpp`, twenty-three parameters, two inputs and one output, registered in the plugin and given a DreamerHelp entry for every control. The test patch is at `patches/mpxphrase-test.vcv`, built from the autosave with Clarity and its settings kept: the chart feeds mpxPhrase and mpxMelody, mpxPhrase feeds the mpxVoice on one path and a second mpxOut on the other, and mpxEuclid is left in place but no longer drives the voice. The installed package is newer than every changed source and contains the new module, so Rack needs restarting to see it.

**Three decisions taken while building it.** Every control is declared now, including milestone five's REPEAT, CYCLE, SECTIONS and ELIDE and milestone seven's RECORD, because Rack saves a parameter by its number and adding them later would misread every patch saved before; they are named on the panel and in the help as not built yet. The two styles are factory presets in Rack's own Preset menu rather than a control on the panel, which declutters it and brings saving your own for free; the preset files are generated from `phraseStyle` by `make presets`, and the module's defaults come from the same header, so the shipped preset, the defaults and the measured style cannot drift apart. And a chart that publishes no phrasing is played in four-bar phrases measured off the beat, which is the same fallback the chart itself uses, rather than leaving this module at the start of a phrase of no length.

**Checks.**
- A test patch built from the autosave: mpxChart into mpxPhrase and mpxMelody, mpxPhrase into mpxVoice, with Clarity kept.
- A second path in the same patch, from mpxPhrase straight to mpxOut with no melody, confirming the notes play at the pitch set by NOTE and that an mpxVoice on the other path replaces it.
- The panel and help load, and cables take.
- The measured tempo is checked against the chart's tempo setting, and follows a change of tempo within a beat or two.
- **Listening checkpoint with you:** does the line breathe, does it leave audible space, and do the controls do what their names say.

## Milestone 4a — Feel: swing and triplets

Numbered this way so the later milestones keep the numbers they already have. It comes before repetition, because a repeated phrase that swings differently from its first appearance is not a repetition.

**Work.** A SWING control on mpxChart, from none to full, and on the harmony block the amount plus one ratio per division — the eighth level and the sixteenth level — converted by the chart from the amount and the tempo it is running, along the curve measured in `test/swing.py`. A reader looks up the ratio for the division it works at and knows nothing of the curve. In mpxPhrase, the onsets and the durations of a phrase are deformed by that ratio after the notes are placed, not before: placement stays on an even grid so that the metre, the chord changes and the sub-phrase boundaries are all still reasoned about in plain beats. A TRIPLETS control on mpxPhrase, the likelihood a beat sounds the middle unit of the triplet, drawn per figure.

**Checks — `make phrasetest`.** All pass. The curve itself is checked against the per-solo medians at six tempos, so a curve that drifted from the corpus cannot leave every other figure looking right. With swing at nought every onset stays on its slot — none of 1,868 off it. At a published ratio of 1.50 the notes that came out measure 1.50 over 875 pairs, which is what catches a mapping applied to onsets but not to the notes' ends. Triplets asked for on 15 per cent of played beats land on 16, with one run in two hundred longer than three beats.

**A fault the swing check found that had nothing to do with swing.** A group whose last note wanted to land past its sounding end was clamped to half a slot before that end — which is not a position on the grid, so the note came out a quarter of a beat off it. It is clamped to the last slot that fits inside the group now. Nothing but an exact check on onsets would have shown it.

**Open, for you.** Where the SWING control goes on the mpxChart panel, which is already arranged. It is at the moment in the band between the transport and the seed, which is free but was not chosen.

## Milestone 5 — Repetition

**Work.** REPEAT at both levels. CYCLE, counting phrases from the top of the form across passes, with the repeat chain restarting at each cycle. Question and answer from a half cadence followed by a closing one. SECTIONS. ELIDE.

**Checks — added to `make phrasetest`, all exact and all passing:**
- with CYCLE at N, phrase N plus one has the same onsets as phrase one
- with REPEAT at one, consecutive phrases share every onset except in the final group — 9 restated, none lost
- with REPEAT at nought the phrase before makes no difference at all, which is what stops a repeat leaking in where none was asked for
- with ELIDE at one and a closing cadence, the last group sounds to the phrase's end rather than breathing at it

**How SECTIONS works, which is not what the plan assumed.** Nothing is stored. The decision to restate is keyed on the section letter and the phrase within the section and on nothing else, so the second time round draws exactly as the first did — no pattern has to be kept for a section that may not return for two minutes, and a rack that joins the music halfway still plays the section the way it was played before it was listening.

**Listening checkpoint:** can a restated phrase be heard as a restatement, and does the cycle audibly come back.

## Milestone 6 — Bringing mpxMelody into line

**Work.** Replace the pass counter in the melody's unpatched draw with the position within a cycle, so a line recurs. Add CYCLE and OWN SEED to the master panel with the same rule as mpxPhrase. Connect the melodic step's phrase anchor to the cadence type: the tonic at a closing cadence, the second, fifth or seventh degree at a half cadence. Remove BREATH from mpxVoice, migrating your saved layout.

**Result.** Built, except for removing BREATH — see the decision below.

**The draw is now a pure function,** `melodyDraw` in Melodic.hpp, so recurrence is checked from a command line rather than listened for. It hashes where the phrase falls in a cycle of CYCLE phrases and where the note falls inside that phrase, and nothing else. Two things had to come out of it, and between them no line had ever repeated: the pass counter, so every time round the form drew differently, and the note's HANDLE, which is unique for the session and counts upward for ever — so even the same beat of the same bar drew differently on the second pass.

**The anchor is what makes a phrase arrive.** The chart says how a phrase ends and how far off that end is; within two beats of it the melody is pulled toward the tonic at a closing cadence, and toward the second, fifth or seventh degree at a half cadence, which are the degrees that sit over a dominant without resolving. The pull is nothing two beats out and strong on the arrival, and there is no anchor anywhere else in a phrase — a tonic pull applied throughout would make every note the tonic.

**Checks — `make melodytest`, all exact and all passing:**
- with CYCLE at four, phrase five draws exactly as phrase one, and the four phrases inside a cycle differ from one another
- it comes round across passes of the form, which is what the pass counter used to prevent
- a locked seed ignores the chart's
- the anchor lands a phrase end on the tonic in 50 per cent of draws against nought per cent without it

**And the whole chain now restarts together.** A rewind set the chart's pass counter forward, and the pass counter is what every random process downstream folds into its draws — so a rewind gave a different rhythm and a different melody from the take before it, which is the opposite of what a rewind is for. A rewind now returns the counter to nought; a loop still advances it, because running off the end of the form and back to its start really is another pass. mpxPhrase drops its repeat chain on any jump, since what a phrase restates is the phrase immediately before it, and mpxMelody clears each voice's last note, because a line carried across a rewind begins with an interval from the previous take.

**Decision still yours:** whether BREATH leaves mpxVoice. It is left in place, and I would keep it: a voice fed by mpxEuclid or any other unphrased rhythm source has no other way to breathe. With mpxPhrase upstream it should be at nought, or the two modules pause on top of each other — which is what made the first patches sound as though every phrase ended twice.

## Milestone 7 — Recording

**Work.** A RECORD button on mpxPhrase writing to the same folder as mpxMelody's log: settings at the start and on change, with the seed in use and its source; one line per phrase with its form position, cycle position, cadence type, sub-phrase division and the count of slots copied by REPEAT; one line per note. A short script that summarises a log per phrase and per setting, so a take can be read without reading every line.

**Check.** A recorded take reproduces the milestone 3 and 5 statistics when summarised.

**Result.** Built. RECORD, in mpxPhrase's right-click menu, writes a new file each take — `phrase-log-` and the moment it started, in the DreamerMPX folder of the Rack user folder, beside mpxMelody's. A settings line when a take starts and whenever a knob moves, with the seed actually in use and where it came from; a line per phrase, with its place in the pass, the form and the cycle, its cadence, its section, its groups and how many slots REPEAT restated; and a line per note, with its group, its place, its length, its level, whether it is a phrase's arrival, its approach or a group's end, and whether it was joined legato to the note before.

## Milestone 8 — Tuning, and finishing

**Work.** Listening sessions with recordings, adjusting the constants inside the rules rather than adding controls. Defaults settled. Your arranged panel folded into the code. `phrase.md` rewritten as a description of the built module rather than a proposal. Help entries brought up to date. A changelog entry.

**Check.** The generated statistics at the final defaults are reported next to the lead-sheet figures in `phrase.md`, so the gap between them is visible.

## Not in this plan

Standalone mode — running from a clock with no chart, revealed from the right-click menu, with its own input band and a gate, level and phrase trigger on the right. Described in `phrase.md`; it would follow the milestones above, since it changes what feeds the generator and not the generator itself.

The drum pattern generator. Swing, ratchets and fills. Whether a melody may alter the rhythm's rests. Any module reading another module's notes, which the design rules out.
