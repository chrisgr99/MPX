# MPX Guitar Player — Specification

Specification for Claude Code. Author: Chris Graham. Date: 2026-09-29.

*Decisions taken since it was written, which override the text below where they disagree.*

*D1: articulations ride on the MPX cable, as fields added to the note event — set at note-on, declaring what the note is rather than what it sounds like, so the renderer still decides how to play it. There is no `part` field and no band cable: one MPX cable is one instrument, always, however many jacks that takes. Per-part data (program, tuning, capo, drum kit, instrument name) is state on the bus beside the harmony. Nothing new is needed during a note: bend, pressure and timbre already exist as updates aimed at a named note.*

*D2: three pieces rather than two. The performer is a library with no Rack in it — notes and flags in, a timed schedule out (pitch moving through its bends and slides, a gate already shortened by palm mute or staccato, velocity with the accents in it, a timbre value, and which string each voice belongs to), driven by one rules table in a file. Two modules read that schedule, and sharing it they cannot disagree about what a hammer-on is; tuning the table by ear improves both.*

*`mpxGuitarist` plays the schedule as control voltage, so any VCV voice can play a guitar part. One MPX input, one instrument, one set of jacks — a bass part and a lead part are two of these modules, each driving its own voice chain, each taking its part's tuning, capo and string count off the cable with nothing set on the panel. Its jacks are mpxOut's without the bend: gate, volts per octave, level, pressure, timbre, pan. These are ordinary polyphonic Rack cables and a channel is a STRING, not a note — six channels for a six-string part whether or not all six sound. That is what makes the articulations work with stock modules: a bend moves one channel and leaves the other five, a hammer-on sends no new gate so the pitch glides under a held envelope, and a palm mute is a shorter gate and a lower timbre. The bend is inside the volts-per-octave output, which is the pitch as played; there is no separate bend jack to add back in.*

*`mpxOut` is untouched, for the notes as they were written.*

*D3: `mpxSound` holds one FluidSynth and one copy of the bank, with six or eight MPX inputs — one instrument each, a program each, its own strings' channels each. A synth per module would mean a copy of a thirty-megabyte bank per part. Drums are a percussion input on the same module; mpxDrums stays as the synthesised kit and mpxPiano keeps the Salamander samples.*

*D4: inside MPX, beside the reader.*

*`mpxGuitar` in the text below is the GENERATIVE guitarist, §6 — the module that invents a part from the harmony rather than playing one. It is unlikely to be built: the reason for importing a song is that the material is already musical, and inventing one from a chord grid is the problem the import was meant to avoid. So nothing is competing for the name.*

*BUILT SO FAR: D1 — the note event carries the techniques, the string and fret, the vibrato, the grace, the strum and the bend points; the instrument is a state block beside the harmony; mpxGuitarChart fills all of it from the file and mpxMonitor shows it. The performer library (src/Perform.hpp), with every number in DreamerMPX/perform.txt and a test per articulation in `make performtest`. `mpxGuitarist`, which plays its schedule as control voltage, a channel per string. And `mpxSound`, which plays the same schedule through FluidSynth 2.6.1, built static in dep by tools/build-fluidsynth.sh, with `make soundtest` for the engine alone.*

*NOT BUILT: the harmony from the notes, the style profiles of §6.5, the MIDI file export of §7.6, and the variations of §6.6. The performer has no lookahead — see Perform.hpp — so a slide into a note arrives just after the onset rather than before it, and nothing else in the design depends on knowing the future.*

*Held for later. This is the player for Guitar Pro material, to be implemented after the import module. The reader described in [guitar-pro.md](guitar-pro.md) already covers §5's import and §13's "later work" formats — gp3/4/5, .gp, .gpx and bare .gpif all read today — so §5 is now about what the score module does with what is read, not about writing the parser again.*

## 1. Purpose

Build a set of VCV Rack modules that play guitar-centred band parts with the same realism as the Ultimate Guitar (UG) Guitar Pro player. The main goal is not to reproduce Guitar Pro files. The goal is to let Chris's MPX modules (mpxChart, mpxArp, mpxRand, and the planned mpxMelody and mpxComp) generate new parts and variations that are then performed the way a guitarist would play them, and rendered with the same kind of engine UG uses.

Guitar Pro import comes second. It serves two purposes:

1. It is a reference: a real transcription can be played through the same pipeline, so the realism of the pipeline can be judged against the UG player.
2. It is a source of style: articulation habits, strum patterns and chord voicings are extracted from real transcriptions into "style profiles" that steer the generative performer.

## 2. Findings: how the UG player works

These findings come from inspecting the UG tab page on 2026-09-29: the WebAssembly binaries, the service-worker cache and the strings in the compiled code. The design below follows the same architecture.

**Score engine (xtzmain.wasm, 5.8 MB).** Muse Group's "xtz" framework wraps MuseScore's engraving and playback code (mu::engraving, muse::), compiled to WebAssembly. The Guitar Pro file is imported into a MuseScore score. A MidiAdapter::fillTracks step converts the score's playback events into MIDI tracks. Strings in the binary include GuitarBend, palmMute, several vibrato variants ("Vibrato small, slow" … "Vibrato largest, slow") and "Slide in below". Much of the realism comes from this articulation-to-MIDI rendering.

**Synthesiser (xtzaudio.wasm, 0.8 MB).** FluidSynth runs inside an AudioWorklet and uses FluidSynth's built-in reverb (FDN) and chorus. libopus is included too, for the "Official" tabs that play recorded backing tracks. The Guitar Pro player does not use it.

**Sounds.** The bank is labelled XTZ_3.0.sf2. Its embedded licence is the GeneralUser GS licence (S. Christian Collins), a free General MIDI bank. For the web, the bank is split into meta.zip (the SoundFont structure) and one .ogg file per sample. SFBuilder::makeSF builds an in-memory SoundFont containing only the programs a song uses. For "Don't Look Back in Anger" that was about 1.7 MB: Overdrive Guitar, Drive Guitar slide samples, Steel Guitar, Finger Bass, Grand Piano, Hammond Organ, Alto Sax and a GM drum kit.

**Conclusion.** The samples are ordinary. The realism comes from (a) detailed articulation data and (b) careful conversion of that data into MIDI expression: per-string channels, pitch-bend curves, legato, strum spread, dynamics and let ring. The modules specified here reproduce (b) and generate (a).

## 3. Scope

### In scope

- A Guitar Pro 7/8 importer that reads .gp files (zip) and bare score.gpif XML.
- A performance renderer that converts notes plus articulations into timed MIDI-style events with expression.
- A generative guitarist that turns MPX harmony and notes into idiomatic guitar parts with articulations, steered by style profiles.
- A SoundFont renderer built on FluidSynth, with GeneralUser GS as the default bank and any user SF2 as an option.
- Style-profile extraction from Guitar Pro files.
- Standard MIDI file export of any performance, for checking in a DAW.

### Out of scope (first release)

- Notation display.
- Guitar Pro 3–5 (.gp3/.gp4/.gp5) and .gpx (GP6). Section 13 lists these as later work.
- Guitar Pro's RSE sound engine and effect chains. RSE data in the file is ignored apart from the MIDI program numbers.
- Vocals. Vocal tracks import as MIDI melody on a lead instrument and are muted by default.

## 4. Architecture

Four modules, following the MPX naming convention. Data flows left to right on MPX cables.

```
mpxScore ──MPX──┐
                ├──► mpxGuitar ──MPX(+articulations)──► mpxSound ──► stereo audio
mpxChart ─► mpxComp / mpxArp / mpxMelody ──MPX──┘                         (+ per-part outs)
```

| Module | Role |
| --- | --- |
| mpxScore | Loads a Guitar Pro file and plays the selected tracks as MPX, with all articulations attached. Also exports style profiles and MIDI files. |
| mpxGuitar | The generative guitarist. It takes MPX harmony and notes, fits them to a fretboard, applies a rhythm or picking pattern and adds articulations according to a style profile. Its output is MPX carrying string, fret and articulation data. With articulated input from mpxScore, it can pass the input through unchanged or vary it. |
| mpxSound | The performance renderer and synthesiser. It converts articulated MPX into FluidSynth calls using per-string channels, pitch-bend curves, legato, strum spread and humanisation, then renders audio. |
| fromMPX (existing) | Unchanged. It remains the route to ordinary polyphonic CV. |

The two jobs are split deliberately. mpxGuitar decides what is played; mpxSound decides how it sounds. mpxSound can also be driven by mpxComp directly, with no guitar logic, for piano, organ and bass parts. It then applies only the generic performance rules: dynamics, humanisation and legato.

### 4.1 MPX articulation extension (decision required)

The current MPX cable carries everything MPE carries, plus harmony, chord changes, per-note predefined durations and a random seed. This design needs extra per-note fields. Claude Code must read the MPX source (github.com/chrisgr99/MPX) before designing this extension, and propose it to Chris before implementing it.

Required per-note fields:

- `part` — part/track index (0–31), so one cable can carry a band arrangement.
- `string`, `fret` — guitar position (string 0 = lowest). −1 means none (non-fretted instruments).
- technique flags: hammer-on/pull-off destination, legato slide, shift slide, slide in from below/above, slide out down/up, let ring, palm mute, dead note, ghost, accent, heavy accent, harmonic (natural or artificial), tapped, staccato.
- `bend` — up to 4 points of (offset 0–100 % of the note's duration, value in cents).
- `vibrato` — none, slight or wide.
- `strum` — beat-level down, up or none, the stroke duration in ms and the note's index within the stroke.
- `velocity` (0–127) — the existing MPX velocity, if present.
- `grace` — none, before the beat or on the beat.

Required per-part data (sent once, then when it changes):

- The GM program, the tuning (MIDI pitch of each string), the capo fret, and whether the part is a drum kit.

If MPX cannot carry this at audio rate without breaking the existing modules, the fallback is an articulation side channel keyed by MPX note ID. The choice is Chris's.

## 5. mpxScore — Guitar Pro importer

### 5.1 File handling

- Accept .gp files (a zip archive; read Content/score.gpif) and bare .gpif or .xml files that contain GPIF.
- Unzip with miniz (single file, MIT licence), vendored into the plugin.
- Parse XML with pugixml (MIT licence). The Oasis test file's GPIF is 2 MB. Parse it on a worker thread, never inside process().
- Load files through Rack's osdialog, and store the file path plus a copy of the GPIF in the patch (dataToJson, compressed) so a patch still opens after the source file moves.

### 5.2 GPIF data model

GPIF (Guitar Pro 7/8 XML) is normalised: elements refer to each other by space-separated ID lists, and repeated material reuses the same IDs. Resolve references; never read elements in document order.

```
Score, MasterTrack (Tracks, Automations: Tempo ...), Tracks/Track[id]
MasterBars/MasterBar  → <Bars> = one Bar ID per track, in track order
                        <Time>4/4</Time>, <Key>, <Section>, <Repeat>, <AlternateEndings>, <Directions>
Bars/Bar[id]          → <Voices> = up to 4 Voice IDs (-1 = empty)
Voices/Voice[id]      → <Beats> = Beat IDs in order
Beats/Beat[id]        → <Rhythm ref>, <Notes> (IDs), <Dynamic>, <GraceNotes>, <Hairpin>, Properties
Notes/Note[id]        → Properties (String, Fret, Midi, Bend*, Slide, Hopo*, Muted, PalmMuted ...),
                        <Tie origin/destination>, <LetRing/>, <Vibrato>, <Accent>, <AntiAccent>
Rhythms/Rhythm[id]    → <NoteValue>, <AugmentationDot count>, <PrimaryTuplet num den>
```

Counts from the test file, which show the level of reuse: 193 MasterBars, 4,825 Bars, 4,706 Voices, 1,380 Beats and 900 Notes across 19 tracks.

### 5.3 Elements and properties to support

This list is taken from what the test file actually contains, plus the common remainder of the format. Items marked * appear in the test file.

**Track:** Name*, ShortName*, Sounds/Sound/MIDI/Program* (the GM program to use), Staves/Staff/Properties: Tuning/Pitches* (e.g. 40 45 50 55 59 64; bass 28 33 38 43), CapoFret*, PartialCapoFret, FretCount*, InstrumentSet/Type* (drumKit → percussion), drum Elements/Articulations with OutputMidiNumber*, PlaybackState* (mute/solo), Transpose*, UseOneChannelPerString*, LetRingThroughout*, PalmMute* (track-wide), AutoBrush*, AutoAccentuation*, Automations (Sound changes*, volume, pan).

**MasterBar:** Time*, Key*, Section* (letter and text, e.g. "Chorus"), Repeat* (start, end, count), AlternateEndings, Directions (Coda, Segno, D.S. al Coda and so on), Fermatas, TripletFeel.

**MasterTrack Automations:** Tempo* (value, beat unit, bar, position, linear or step). The test file has 163 2 (163 BPM, half-note unit) at bar 0 and bar 187, and 89 2 at bar 190.

**Beat:** Rhythm*, Dynamic* (PPP–FFF), GraceNotes* (BeforeBeat, OnBeat), Hairpin* (Crescendo, Decrescendo), PickStroke* (Down, Up), Brush* (Direction plus duration), Arpeggio, Tremolo (picking), Fadding (volume swell), Wah, Slapped/Popped, Whammy bar (WhammyBar* properties), Chord ID, FreeText, Tuplet via Rhythm*.

**Note:** String*, Fret*, Midi*, ConcertPitch*, Tie* (origin/destination), LetRing*, Vibrato* (Slight, Wide), Accent* (bit flags: 1 = staccato, 4 = heavy accent, 8 = accent; the test file has values 1 and 8; confirm the other bits against alphaTab), AntiAccent* (ghost note), Bended* + BendOriginOffset/Value, BendMiddleOffset1/2, BendMiddleValue, BendDestinationOffset/Value*, Slide* (Flags bit field), HopoOrigin*/HopoDestination*, Muted* (dead note), PalmMuted*, LeftHandTapped*, Tapped, HarmonicType/HarmonicFret, Trill, Ornament, InstrumentArticulation* (drum articulation index).

**Bend units** (checked against the test file). Values are 0–100 per whole tone: 100 = one whole tone = 200 cents, 50 = semitone, 75 = three-quarter tone. Offsets are 0–100 % of the note's duration. Examples from the file: origin 0/0, middle 12..12 at 50, dest 25 at 100 is a half-step pre-rise then a full bend; 0/0, 17 at 100, 89–99 at 0 is a full bend and release; 0/100 … 100 is a pre-bend held.

**Slide flags** (checked against the test file, which has flags 1, 2, 4, 8 and 16). 1 = shift slide to next note, 2 = legato slide to next note, 4 = slide out downwards, 8 = slide out upwards, 16 = slide in from below, 32 = slide in from above, 64 = pick slide down, 128 = pick slide up. Confirm the higher bits against alphaTab's GpifParser.

### 5.4 Timeline construction

1. Expand repeats, alternate endings and directions into a linear bar sequence (the "playback order"). Keep a map from playback bar back to written bar.
2. Build the tempo map from the Tempo automations: step or linear ramps, converting the beat unit to quarter notes.
3. For each track, voice and beat, compute the onset tick (use 960 ticks per quarter) and the duration from Rhythm, including dots and tuplets.
4. Merge tied notes into one event with the combined duration. Carry the first note's attack and the articulations of the whole chain; a bend on a tied continuation applies to the combined note.
5. Resolve grace notes. BeforeBeat takes time from the previous beat; OnBeat takes time from its own beat. Default grace length is 1/32 note, or 60 ms at fast tempos, whichever is shorter.
6. Emit MPX events with the per-note fields from §4.1.

### 5.5 Module behaviour

- Transport: play, stop, loop the selected sections, position by bar, and follow an external clock (Rack clock input, 24 PPQN or 1 pulse per beat) or run from the internal tempo map.
- Track selector: pick which tracks go out on the MPX output. Per-track mute and solo, and an option to follow the file's mute/solo state.
- Section display: current section name and bar number, in large text.
- Outputs: MPX out; clock out; end-of-song trigger; section-change trigger.
- Context menu: export a standard MIDI file of the rendered performance (via mpxSound's rules; §7.6); export a style profile (§6.5).

## 6. mpxGuitar — generative guitarist

### 6.1 Inputs, outputs and controls

- Inputs: MPX in (harmony plus notes from mpxComp, mpxArp or mpxMelody; or articulated notes from mpxScore); clock; reset.
- Output: MPX out, with per-note string, fret and articulation data.
- Controls: mode (Strum / Pick / Lead / Pass-through), style profile (context menu, loaded from JSON), density, complexity, articulation amount, humanise, position (low / open / high neck), tuning and capo (menu).
- Seeding: every random choice draws from a generator seeded by the MPX seed plus a per-module offset, so results are reproducible and change together when the seed at the generator changes.

### 6.2 Fretboard model

- Tuning (MIDI pitches), capo, fret count (default 22) and the maximum hand span (default 4 frets, 5 when open strings are involved).
- Chord voicing: given the chord symbol from MPX harmony, choose a voicing from a shape library (open chords, E-shape and A-shape barre chords, the CAGED shapes, power chords, triads on string sets, and common extensions: 7, maj7, m7, sus2, sus4, add9, 6, dim, aug, slash chords). Voicings are generated from the tuning rather than stored per chord, so alternate tunings work.
- Voicing choice minimises a cost that combines: hand movement from the previous voicing (the change in fret position), voice-leading distance between the voices, how well the voicing matches the style profile (e.g. the open-position preference in the test file's acoustic parts), bass-note correctness for slash chords, and a small random term drawn from the seed.
- Melody fingering: assign string and fret to a single-note line with a Viterbi search over candidate positions. The cost penalises position shifts, stretches, string skips, and open strings in high positions. It rewards keeping to one string for slides and hammer-ons when the articulation layer asks for them.

### 6.3 Rhythm layer

**Strum mode.** Strumming comes from a pattern library: one 16-step grid per bar with a direction (down/up), accent, muted "chuck" and rest at each step. Down strokes fall on even 16ths and up strokes on odd ones, as a real right hand moves; a skipped stroke is a rest rather than a change of direction. Density thins the pattern; complexity adds syncopation and chucks. Patterns are chosen per section according to the MPX section or intensity if present, otherwise per chord change.

**Pick mode.** Patterns for arpeggios and fingerpicking: Travis picking (alternating bass on the thumb), rolling arpeggios, and bass-plus-chord alternation. Let ring is on by default in this mode.

**Lead mode.** Takes a melodic line (from mpxMelody or mpxArp) and adds only fingering and articulations.

**Pass-through.** Leaves articulated input unchanged, apart from optional variation (§6.6).

### 6.4 Articulation layer

Each rule has a base probability, scaled by the articulation amount control and by the style profile. Examples:

| Situation | Articulation |
| --- | --- |
| Ascending step on the same string, second note short | Hammer-on |
| Descending step on the same string | Pull-off |
| Position shift between consecutive notes on the same string | Legato slide or shift slide |
| First note of a phrase after a rest | Slide in from below (1–3 frets) |
| Long final note of a phrase | Vibrato (slight; wide when the style calls for it) |
| Scale degree 2→3, 4→5 or ♭7→root in a lead line, on strings 1–3 | Bend from the lower note: semitone or whole tone, optionally with release |
| Driven eighth-note root patterns | Palm mute |
| Muted strum step | Dead note, all strings |
| Arpeggio or pick-mode note | Let ring until the same string is struck again |
| Beat 2 and 4 strum in rock styles | Accent |
| Passing notes in fast runs | Ghost note (lower velocity) |

### 6.5 Style profiles

A style profile is a JSON file extracted from one or more Guitar Pro tracks by mpxScore (menu) or a command-line tool (tools/gpstyle, C++ sharing the importer). Contents:

- The instrument (GM program), tuning and capo.
- The frequency of each articulation per note, broken down by interval to the next note, duration class and string; bend-amount histograms; vibrato frequency by duration.
- Strum patterns: 16-step grids with direction, accent and mute, counted per bar and weighted by frequency; brush and strum-duration statistics.
- The chord voicings used (as fret shapes), with counts; neck-position histogram.
- A velocity distribution by beat position; the use of dynamics.
- A timing-feel estimate if the file carries swing or triplet feel.

A profile describes how a player plays, never what they play. It contains no note sequences, so a profile taken from a copyrighted transcription does not reproduce the song.

### 6.6 Variation of imported parts

In pass-through mode with a variation amount above zero, mpxGuitar can change an imported part while keeping its style. It can re-voice chords (same chord, another shape, e.g. at a different neck position), substitute strum patterns drawn from the part's own profile, add or remove articulations according to the profile's rates, and add fills at section ends. All of these draw from the seed.

## 7. mpxSound — performance renderer and synthesiser

### 7.1 Engine

- FluidSynth 2.3 or later (LGPL-2.1), built as a static library for all four Rack platforms (Mac arm64, Mac x64, Windows x64, Linux x64). Build FluidSynth without audio drivers, MIDI drivers, libsndfile, glib threads, LADSPA and readline. Only SF2 support is needed. If SF3 (ogg-compressed) support is wanted later, add stb_vorbis in place of libsndfile.
- Default bank: GeneralUser GS (around 30 MB SF2; its licence allows use in software and redistribution). Either bundle it with the plugin or download it on first use into the Rack user folder; decision D3. The user can choose any SF2 per part.
- Licence note: FluidSynth is LGPL and the plugin is GPL-3.0, so static linking is permitted. Include the FluidSynth licence and source link in the plugin's LICENSE notes.

### 7.2 Real-time integration with Rack

- Create one fluid_synth_t per module instance, at Rack's sample rate. Recreate it on onSampleRateChange.
- Render in blocks of 32 frames inside process(): collect the events due in the block, apply them at their sample offsets (split the block at event boundaries, or accept ≤32-sample quantisation, which is 0.7 ms at 48 kHz), then call fluid_synth_write_float. Output samples come from a small FIFO.
- Load SoundFonts on a worker thread. Swap the new synth in atomically, and output silence during a load.
- Set synth.midi-channels to 64 and synth.polyphony to 256. Leave reverb and chorus on by default (FluidSynth's defaults, as in the UG player), with menu switches to turn them off so Rack effects can be used instead.

### 7.3 Channel allocation — the main source of realism

- Per-string channels for fretted parts. Each guitar or bass part gets one FluidSynth channel per string, all set to the part's program. A bend or vibrato on one string then leaves the others unaffected, and each string is monophonic. Guitar Pro's UseOneChannelPerString flag and MuseScore's playback do the same.
- String monophony. A new note on a string ends the previous note on that string, even when let ring is set. Let ring holds a note only until its own string is struck again.
- Keyboard and other parts use one channel each. Drum parts use channel 10 semantics (bank 128) with the GM note from the drum articulation's OutputMidiNumber.
- Set the pitch-bend range on every fretted channel to ±12 semitones (RPN 0), so two-tone bends and long slides resolve with 14-bit precision.

### 7.4 Articulation rendering rules

Write each rule as a separate, unit-testable function. The default values below are starting points; all of them should be adjustable in a rules table (JSON), so Chris can tune them by ear.

| Articulation | Rendering |
| --- | --- |
| Bend | A pitch-bend curve through the bend points (offset % × note duration, cents). Interpolate with a smooth curve (cosine) between points, updated every block. Reset bend to centre at the next attack on that string. |
| Pre-bend | Bend set before note-on, at the same sample offset. |
| Vibrato | A pitch LFO of ±25 cents (slight) or ±50 cents (wide) at 5.5 Hz, fading in over 150 ms after a 100 ms delay; for long notes, rate drift ±10 % from the seed. |
| Hammer-on / pull-off | No new attack. Glide the string's pitch bend to the new pitch within 8 ms, and lower CC11 slightly (−3 dB) to model the weaker sound. If the interval exceeds the bend range, fall back to FluidSynth legato mode (fluid_synth_set_legato_mode, retrigger 0) with velocity × 0.7. |
| Legato slide | A pitch glide from the source to the target note over the last 60 % of the source note (up to 250 ms), with no new attack. |
| Shift slide | As a legato slide, then a new attack of the target note at velocity × 0.8. |
| Slide in from below / above | Start 3 semitones below (or above) and glide to the pitch over 60 ms before the notated onset. The note-on is moved earlier by that amount. |
| Slide out down / up | Glide 5 semitones down (or 3 up) over the last 120 ms of the note while fading CC11 to 30 %. |
| Palm mute | Duration × 0.35 (capped at 150 ms), velocity × 0.85, CC74 (brightness) to 40 with a custom FluidSynth modulator mapping CC74 to filter cutoff. Option: switch that string's channel to GM 28 (Electric Guitar Muted) instead. |
| Dead note | 25 ms note at velocity × 0.6, on GM 28 or the program's lowest sample, plus optional GM 120 (Guitar Fret Noise). |
| Let ring | Sustain until the same string sounds again or the let-ring region ends; release no later than the end of the bar plus 2 s. |
| Strum / brush | Spread onsets across the strings in the stroke: down = low→high, up = high→low. Stroke length comes from Brush or from the style (default 18 ms down, 12 ms up, scaled ×0.7 at tempos over 140 BPM). The first string in the stroke is on the beat. Velocity falls by 4 % per string in stroke order, and by an extra 10 % for up strokes. |
| Accent / heavy accent | Velocity +15 / +25 (clamped). |
| Ghost note | Velocity × 0.5, duration × 0.8. |
| Staccato | Duration × 0.5. |
| Dynamics | Map PPP…FFF to base velocity 20, 33, 49, 64, 80, 96, 112, 124. Hairpins ramp CC11 across their span. |
| Natural harmonic | Pitch = the harmonic's pitch (12th fret +12, 7th fret +19, 5th fret +24 …). Program switch to a harmonics patch if the bank has one, otherwise velocity × 0.8 on the same program. |
| Tremolo picking | Repeated attacks at the notated subdivision, with velocity alternating ±5 %. |
| Whammy bar | Pitch-bend curve as for Bend, applied to every string of the beat. |
| Grace notes | Rendered as short notes (§5.4 step 5). A grace note tied to its main note by a slur is played as a hammer-on or pull-off. |

### 7.5 Humanisation (all driven by the seed)

- Timing: ±6 ms Gaussian by default. Strummed chords move as a unit. Bass and kick drum keep tighter timing (±3 ms).
- Velocity: ±6 % Gaussian, plus a beat-position accent curve (downbeat +4 %, backbeat +2 % in 4/4).
- Duration: ±5 % on non-legato notes.
- The Humanise control scales all three from 0 to 200 %. At 0 % the output is deterministic and quantised, for testing.

### 7.6 MIDI file export

Export any performance as a Standard MIDI File, type 1, with one track per part and the per-string channels kept, including all pitch-bend, CC and program events. This is the main check: a rendered part can be opened in a DAW and its expression data inspected.

### 7.7 Panel

- Outputs: stereo mix out; 4 assignable part outputs (each a stereo pair, set in the menu).
- Controls: master volume, reverb amount, chorus amount, humanise; per-part volume and pan in the context menu.
- Indicators: one activity indicator per part, and a SoundFont-loading indicator.
- The bank name is shown on the panel in large text.

## 8. Accessibility and house conventions

- Write help for every param, port and module as `//?` comments after configParam / configInput / configOutput, and `//?module` above each module struct, then run tools/helpscan.py so the modules ship with Help data.
- Use "indicator" (never "light") in all names, labels and help text.
- MPX ports use the existing MPX port type, which refuses connections to ordinary ports. Clarity colours them magenta.
- Text on the panel and in displays is as large as the panel permits; displays use high contrast. File lists and menus use the system font size.
- Do not use tooltips as the only place where information appears.

## 9. Code organisation

```
src/
  gpif/        miniz + pugixml wrappers; GpifDocument (ID resolution); Timeline (repeats, tempo, ties, grace)
  perform/     Articulation types; RulesTable (JSON); Renderer (notes → timed synth events); Humaniser
  guitar/      Fretboard; VoicingSearch; FingeringViterbi; PatternLibrary; ArticulationPolicy; StyleProfile
  synth/       FluidEngine (thread-safe load, block render); ChannelAllocator
  mpx/         the articulation extension (shared with the MPX repo)
  modules/     mpxScore.cpp, mpxGuitar.cpp, mpxSound.cpp
  midi/        SMF writer
tools/
  gpstyle/     command-line style-profile extractor
  gprender/    command-line renderer: .gp → .mid and .wav through the same pipeline (for tests and A/B listening)
res/           panels, default rules table, starter style profiles, pattern library
tests/
```

Put the importer, renderer and guitar logic in libraries with no Rack dependency, so the command-line tools and the unit tests build without Rack.

## 10. Testing

### 10.1 Reference file

`Oasis - Dont Look Back In Anger (UG ver 3) score.gpif.xml` (Chris has it locally; do not commit it to the public repository). Assertions:

- 19 tracks; 193 master bars; time signature 4/4 throughout; key signature with 0 accidentals.
- Section starts at bars 1, 9, 25, 49, 63, 71, 87, 111, 127, 151, 167 and 177 (Intro, Verse 1, Pre-Chorus, Chorus, Bridge, Verse 2, Pre-Chorus, Chorus, Solo, Chorus, Chorus, Outro).
- Tempo 163 (half-note unit) from bar 1, with a change to 89 at bar 191 (0-based bar 190).
- Guitar tunings 40 45 50 55 59 64; bass 28 33 38 43; tracks 10, 16, 17 and 18 are percussion.
- Counts: 37 bent notes, 43 slides, 57 hammer-on/pull-off endpoints (26 origins, 31 destinations), 41 dead notes, 7 palm-muted notes, 129 let-ring notes, 9 vibrato notes, 328 pick strokes (225 down, 103 up), 14 brushes.
- The expanded playback order accounts for the 2 Repeat elements.

### 10.2 Unit tests

- One test per articulation rule, checking the event stream it produces (times, pitch-bend values and velocities) at humanise 0.
- Bend conversion: GPIF value 100 → +200 cents; 50 → +100.
- Channel allocation: two notes on the same string never overlap; a bend on string 3 produces no pitch-bend events on the other strings' channels.
- Voicing search: every voicing is playable (span within the limit, one note per string, correct chord tones); switching tuning keeps the voicings valid.
- Determinism: the same seed gives byte-identical MIDI export.

### 10.3 Listening checks

- gprender renders the reference file to WAV. Chris compares it with the UG player at the same tempo. Mismatches are fixed by adjusting the rules table, not the code, where possible.
- Render one MPX-generated progression in each mode (Strum, Pick, Lead) with the profile extracted from the reference file's acoustic guitar tracks.

## 11. Milestones

1. Importer and command-line tool. GPIF parser, timeline, and gprender producing a MIDI file of the reference song with plain notes. Tests from §10.1.
2. Renderer. Per-string channels and all §7.4 rules in the MIDI export, then FluidSynth rendering to WAV in gprender. First listening check.
3. mpxSound and mpxScore in Rack. Real-time engine, transport, track selection, panel. The MPX articulation extension agreed with Chris (§4.1) before this milestone starts.
4. mpxGuitar: Strum and Pick modes. Fretboard, voicing search, pattern library, seed handling, driven from mpxChart and mpxComp.
5. Style profiles. Extraction, the gpstyle tool, and profile-driven articulation policy; Lead mode with fingering search.
6. Variation of imported parts (§6.6) and refinement of the rules table by ear.

## 12. Decisions for Chris before implementation

- **D1.** How articulations travel: extend the MPX cable (§4.1) or use a side channel.
- **D2.** Whether mpxGuitar and mpxSound are separate modules, as specified, or one combined "guitarist" module.
- **D3.** Bundle GeneralUser GS in the plugin, or download it on first use.
- **D4.** Plugin home: a new MPX-family plugin, or inside the existing MPX repository.

## 13. Later work

- Guitar Pro 3–5 import (binary format; see PyGuitarPro and alphaTab for format references) and GP6 .gpx (a custom BCFZ container around the same GPIF XML).
- MusicXML import, using the same timeline code.
- MIDI output module, to drive an external sampler (e.g. a multi-sampled guitar library with keyswitches) from the same performance events.
- Per-articulation sample support: SF2 banks that contain separate palm-mute, slide and harmonic samples, selected by the renderer through keyswitch or bank/program mapping.

## 14. References

- alphaTab GpifParser (MPL-2.0) — the most complete public reading of GPIF element and property names, bend and slide units, and repeat handling. Read it; don't copy code wholesale.
- MuseScore 4 source (GPL-3.0): src/importexport/guitarpro/ (GPIF import) and the playback and articulation code in src/engraving/ (playback/, compat/midi/). This is the code the UG player runs, and the best guide to how each articulation should turn into MIDI.
- FluidSynth API docs: fluid_synth_write_float, channel/RPN handling, fluid_synth_set_legato_mode, custom modulators (fluid_mod_*, fluid_synth_add_default_mod).
- GeneralUser GS by S. Christian Collins (schristiancollins.com), with its licence file.
- PyGuitarPro (LGPL-3.0) — a quick way to inspect GP3–5 files during later work.
