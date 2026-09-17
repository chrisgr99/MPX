# mpxPhrase — development plan

How the design in `phrase.md` is built. Each milestone ends with something that can be checked: a census, a test that must pass, or something to listen to. A milestone is not started until the one before it passes.

## Status

Milestones 1 and 2 are built and their census checks pass. What remains of both is visual: mpxMonitor showing each phrase, how it ends, its section and appearance, and its chord changes while a chart plays.

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

**Risk.** Defaults that match the lead-sheet statistics can still sound mechanical. The statistics are a floor, not a finish; milestone 8 is where the sound is judged.

## Milestone 4 — The module

mpxPhrase as a playable module.

**Work.** The panel, with every control present and labelled, for arranging in the panel editor. Timing from the chart's beat, with no clock input, and the tempo measured from how fast that beat advances. A phrase generated when the chart reaches its start, and notes sent at their times. Correct behaviour when the chart stops, rewinds, loops or has a section chosen: the pattern is regenerated from the new position rather than continued. The missing-chart notice on the panel. NOTE, SEED and OWN SEED. A DreamerHelp entry for every control.

**When a setting changes mid-phrase,** the change takes effect at the start of the next sub-phrase. Waiting for the next phrase would make a knob feel dead for several bars; applying it mid-group would cut a breath group in half.

**Checks.**
- A test patch built from the autosave: mpxChart into mpxPhrase and mpxMelody, mpxPhrase into mpxVoice, with Clarity kept.
- A second path in the same patch, from mpxPhrase straight to fromMPX with no melody, confirming the notes play at the pitch set by NOTE and that an mpxVoice on the other path replaces it.
- The panel and help load, and cables take.
- The measured tempo is checked against the chart's tempo setting, and follows a change of tempo within a beat or two.
- **Listening checkpoint with you:** does the line breathe, does it leave audible space, and do the controls do what their names say.

## Milestone 5 — Repetition

**Work.** REPEAT at both levels. CYCLE, counting phrases from the top of the form across passes, with the repeat chain restarting at each cycle. Question and answer from a half cadence followed by a closing one. SECTIONS. ELIDE.

**Checks — added to `make phrasetest`, all exact:**
- with CYCLE at N, phrase N plus one has the same onsets as phrase one
- with REPEAT at one, consecutive phrases share every onset except in the final group
- with SECTIONS at one, a returning section's phrases match its first appearance's

**Listening checkpoint:** can a restated phrase be heard as a restatement, and does the cycle audibly come back.

## Milestone 6 — Bringing mpxMelody into line

**Work.** Replace the pass counter in the melody's unpatched draw with the position within a cycle, so a line recurs. Add CYCLE and OWN SEED to the master panel with the same rule as mpxPhrase. Connect the melodic step's phrase anchor to the cadence type: the tonic at a closing cadence, the second, fifth or seventh degree at a half cadence. Remove BREATH from mpxVoice, migrating your saved layout.

**Checks.**
- `make melodytest` gains an exact recurrence check: with equal CYCLE settings, the pitches of phrase N plus one match phrase one.
- A recording shows melody notes at closing cadences landing on the tonic markedly more often than elsewhere.
- **Listening checkpoint:** with equal CYCLE settings on both modules, the whole line recurs.

**Decision needed first:** whether BREATH leaves mpxVoice here, or stays until the question of a melody altering the rhythm's rests is settled.

## Milestone 7 — Recording

**Work.** A RECORD button on mpxPhrase writing to the same folder as mpxMelody's log: settings at the start and on change, with the seed in use and its source; one line per phrase with its form position, cycle position, cadence type, sub-phrase division and the count of slots copied by REPEAT; one line per note. A short script that summarises a log per phrase and per setting, so a take can be read without reading every line.

**Check.** A recorded take reproduces the milestone 3 and 5 statistics when summarised.

## Milestone 8 — Tuning, and finishing

**Work.** Listening sessions with recordings, adjusting the constants inside the rules rather than adding controls. Defaults settled. Your arranged panel folded into the code. `phrase.md` rewritten as a description of the built module rather than a proposal. Help entries brought up to date. A changelog entry.

**Check.** The generated statistics at the final defaults are reported next to the lead-sheet figures in `phrase.md`, so the gap between them is visible.

## Not in this plan

The drum pattern generator. Swing, ratchets and fills. Whether a melody may alter the rhythm's rests. Any module reading another module's notes, which the design rules out.
