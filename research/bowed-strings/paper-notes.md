# Notes towards a paper on the bowed-string player

Notes, not a draft: what was built, what was asked, what was found, with the numbers and where the evidence is, so that a paper can be written later without the work being reconstructed from memory. Gathered on 12 October 2026 from the work of 10 to 12 October 2026 in this repository (DreamerMPX, the bowed-string modules mpxViolin, mpxViola and mpxCello). The design as it now stands is in docs/bowed-strings.md; the literature found is in this folder.

## Possible framing

A physically modelled bowed string played by a modelled player, in real time inside a modular synthesiser. The model is conventional (digital waveguide strings, thermal friction, a modal body). The player is the subject: how it decides each note's start and bow change, why it must try them on a copy of the instrument before playing them, what that costs, and how much of the trying can be removed while the result still sounds right to a listener. Along the way: why a lookup table of starts fails for starts from the string and succeeds for bow changes; why one fixed start cannot be both prompt and smooth; what the literature says players do instead of correcting; and a slurred-interval artefact that is a property of hearing, not of the physics.

Two recent papers are close in kind and could set the form:

- M. van Walstijn, V. Chatziioannou, A. Lampis and E. Matusiak, "A thermal elasto-plastic friction model for bowed-string simulation", Acta Acustica 10, 47 (2026), doi 10.1051/aacus/2026042. Its friction model was implemented here and tested (finding 11).
- H. Beigi and G. Conneely, "Learned bow control on a measured bowed-string model: a revised minimum-bow-force law, a recurrent controller, and the domain of a supervision ceiling", arXiv preprint (2026), doi 10.48550/arxiv.2609.14990. Abstract only read. Its finding that no learned controller beat the lookup rule that produced its labels, where that rule was right, sits beside findings 3, 7 and 8 here.

Venues to consider: arXiv (eess.AS or physics.class-ph) first; then Acta Acustica, where the friction paper appeared, or JASA Express Letters for a short paper on one finding.

## The system, briefly

- **Strings**: digital waveguides, one per string, with torsion as a second waveguide (ratio 3.27), the string's own loss shared between its ends, oversampled to at least 300 samples a period (up to 16 times 48 kHz). Friction at the bow: Woodhouse's thermal model (2003), contact radius 600 µm, the default; the friction curve and van Walstijn et al.'s rosin model selectable. See docs/bowed-strings.md phases 1 to 4.
- **Bridge and body**: the bowed string plays against a still bridge (chosen by ear); the other strings ring in sympathy through the body's signature modes; the sound is the bridge force through a modal body. The violin's modes from measurements (A0 275 Hz, CBR 407, A1 469, B1− 475, B1+ 541, dense modes above 630 Hz); viola and cello from the literature and by ear. Phases 5 and 6.
- **Player**: decides string, bow direction, speed, force, distance from the bridge, bow changes, vibrato and the start of every note. Dynamics by bow speed, force following speed. Starts from the string are bites (a force held then eased, with an acceleration) or glides (force rising with speed); bow changes slow the bow to a weightless turn and speed it up again. Phase 7.
- **Rehearsal**: each start and each bow change tried on an exact copy of the whole instrument, run up to the note, before it is played; the best by a fixed order of preference played. Deterministic: a rehearsal is what is played.
- **Engine**: a playing copy on the audio thread and a planning copy on a worker thread, with a crew of helper threads; notes are announced three seconds ahead over the MPX cable (a protocol extension made for this: `Coming` events in src/NoteBus.hpp), so the planner rehearses while the music plays; decisions are numbered and handed over; a patient engine plays sample for sample what the player alone plays. Phase 8.
- **Settings**: Economy (strings at half the oversampling, unbowed strings resting once rung down: about half the computing, judged by ear "quite acceptable"); Rehearsal at three levels (finding 15).

## Findings

Numbered for reference. Each says what was asked, how it was measured, and what came out.

1. **Note starts are chaotic, and a fixed rule fails** (phase 3, 2026-10-10). One bow acceleration for every note, scaled as Guettler's limits scale: about a quarter of 210 test notes started within the listeners' limits, the rate Galluzzo's simulations of the friction curve found. A player that listened and corrected as the slips came was no better. A player that rehearsed 168 attacks on a copy of the string (more at other bow positions where none started within three periods) started all 210 within ten periods and 209 within five. An attack's acceleration mattered in the fourth significant figure.

2. **Bow changes on a rigid bow fall into multiple slipping** (phase 7). A bow that slows clamps the string; the segments either side ring on and the new stroke slips several times a period for up to 300 ms. Rehearsing each change (nine gestures), a weightless turn, vibrato and the bow following the note's place made them clean by ear. Bristles with hair compliance also cured it but were rejected by ear.

3. **A table of starts works for bow changes and not for starts from the string** (2026-10-12). 15,362 rehearsed decisions were logged (`BowedPlayer::setLog`): every way tried, how each came out, the one played, and the situation. Tabled by situation (instrument, kind, string, note in steps of three semitones, marking, how strongly the string rang): the table's single bow change matched rehearsal's best 86 per cent of the time (its best three 96 per cent); for starts from the string its best start 20 per cent, its best six 40 per cent. In music, on pieces not in the data: the Swan's cello 10 bad starts against 2 rehearsed, the Brahms viola sonata 10 against 0; one fixed glide gave 12 and 12. Rehearsal tried about 18 starts per note from the string and 3 per bow change; a start that spoke within three periods existed in only about 30 per cent of cases, and then it was about the fourth tried. Caveat: an option appears in the logs only when rehearsal's shortlist offered it, which biases offline comparisons; the bench is the fair test.

4. **Players do not correct an attack; they prepare it** (literature, attack-control-sources.md). Whether an attack succeeds is settled within about four periods of the first slip (5 to 18 ms on a violin's open G, 14 to 46 ms on a cello's open C); the fastest human loops are slower (fingertip grip 74 ms, arm reflexes 20 to 45 ms to the muscle and about 50 ms more to the force, hearing 100 ms and more). Professionals' attacks were perfect 20 to 50 per cent of the time and within listeners' limits 80 to 90 per cent (Guettler and Askenfelt 1997; 44 per cent perfect of 1,694 in Guettler 2002). A bowing machine with force and acceleration held within 2 per cent scattered from one stroke to the next (Galluzzo 2004: 3 clean of 10 identical strokes); Lampis, Mayer and Chatziioannou (2024) found 71 to 85 per cent of machine starts in the playable region settled within 20 periods. Both groups say they cannot explain why players find strings more dependable than machines do.

5. **Preparing the bow did not help the model** (2026-10-12). The bow set on the string still for 30 ms before it moved, pressed at the bite's force or touching at a fifth of the note's then pressing: no measurable change, because the starts it applies to are from a barely ringing string, which a still bow simply grips. A real bow's hair gives and absorbs; this model's bow is rigid at the contact.

6. **One fixed bite starts every note promptly and is heard as harsh** (2026-10-12). Swept in music over bite force and acceleration: the violin and viola best at 2.52 times the note's force with an acceleration e^(−29/12) of Guettler's limit, the cello at 3.17 (0, 2 and 0 bad starts of 90, 116 and 82). On music not used to choose it: the Swan 0 bad starts (full rehearsal 2), the viola sonata 0 (0), the Brahms trio's violin 0 (1), and more of them spoke within 50 ms than with rehearsal (76 against 46 per cent; 100 against 74). By ear, about a quarter of those starts were noticeably harsher and a tenth quite bad. The measures counted only whether a note settled into one slip a period; what was heard was roughness, squeak and click in its first 150 ms.

7. **A measure that hears what the listener heard** (2026-10-12). Rehearsal's own judgements of a trial — squeak in the first 150 ms, click (first 20 ms against the rest above 4 kHz), roughness from the fourth period to 0.33 s — applied to every note as actually played (`BowedPlayer::hearPlayed`), each note heard no further than the next one's start. On the Swan: starts rougher than −27 dB, rehearsed 0 per cent, firm bite 35 per cent; the two notes the listener named were −24 and −23 dB against −30 and −31 rehearsed. Thresholds do not carry from piece to piece; comparing note against note with a rehearsed render does, with the chance level found by rendering again at another random seed: 0 to 2 notes in a passage differ by 4 dB or more.

8. **No single start is both prompt and smooth** (2026-10-12). Re-swept for the cello with the heard measures: firm bites settled but were rough, soft bites were smooth but a third failed to settle; the best compromise still had a fault in about half its starts. The right start depends on the occasion. This is the case for trying candidates on the instrument rather than looking one up.

9. **Where rehearsal's time goes** (2026-10-12). The shortlist on the string alone is about two thirds of all the computing; it tries 168 attacks, and 2,080 more at four bow positions when the best of the first is not regular within three periods (about one start in six, and two thirds of the shortlist's work); the trials on the instrument most of the rest; bow changes little. Cheaper shortlists (coarser grids, attacks given up at 50 ms without a slip) chose starts rougher than chance allowed on every passage (9 to 27 notes against 0 to 2), and saved no time.

10. **Two starts tried is very nearly as good as full rehearsal** (2026-10-12). The two are the glide and the bite full rehearsal chose most often over the logged decisions (a glide over 20 ms from a fifth of the force; for violin and viola a light bite at 1.4 of the planned distance, fast; for cello a moderate bite at the planned place), tried on the instrument and the better played. Note against note: the Swan within chance; the viola sonata 6 rougher and 9 smoother. By ear: "very nearly as good", "in some ways better" on the Swan, the best of all on the viola sonata, "possibly even better" than full rehearsal; on the Fauré's violin very nearly as good, failing slightly more noticeably in the same places. Four or six candidates were no better than two; adding a firm bite as a third brought the harshness back, because the order of preference takes any start that speaks within three periods first, and with few candidates that is often the firm one.

11. **A friction model with the hair's give did not make starts forgiving** (2026-10-12). van Walstijn et al.'s thermal elasto-plastic rosin with the hair as a mass on a spring and damper: a guessed start was further from its rehearsed one than under the thermal model, the rehearsed playing itself rougher, and the computing three to six times greater. So the hair's compliance, as modelled there, is not what lets a player's single guess work.

12. **The cost in actual playing** (2026-10-12, Apple M4 performance core). Rendering the Swan (175 s) alone: full rehearsal 42.5 s of computing, two starts tried 14.6, starts guessed 13.5, playing with no rehearsal 9.9; the rosin model rehearsed 238. In a module the instrument is simulated twice (playing and planning copies), so about 30, 14 and 13 per cent of a core respectively. On busier music the saving is larger: the viola sonata about 147 s against 29 for two starts tried. Rack's meter shows only the playing thread, about 6 to 10 per cent per instrument at every level; Rack showed about twice the bench's figures because of where the system schedules the thread (on the efficiency cores the bench measured 32, 43 and 30 per cent).

13. **A slurred change of a large interval is heard as a blip of noise, dry** (2026-10-12). Named by the listener on one slurred note (D sharp to B, the A string, Fauré first movement): "a blip of pink noise". Ruled out in turn, each by ear and by measurement: the start choice (identical at all three levels: slurs are not rehearsed); the finger's 6 ms slide (the same instant, at 2 ms and 6 ms; softer at 40 ms); the waveguide's handling of a length change (a content-preserving finger change, below, cut the measured burst from −11 to −18 dB without changing what was heard); the bow's place moving 12 mm (held or moved over 300 ms, unchanged); the bow's speed and force changing (held or changed over 300 ms, unchanged); the body's resonances (the strings' bridge force alone, with no body, had it too); the string's slips (regular from 5 ms after the change); the pitch (the same a semitone higher and lower). An ideal tone with a bowed string's spectrum, switched from D sharp to B at once with its phase unbroken, reproduced it; slid over 40 ms it did not, but the slide was heard. Spectral splatter of an abrupt large pitch change, heard without a room or a body's long resonances to blur it. Chosen by ear among smoothings: a 15 ms raised-cosine slide with the old note lingering, an unbowed copy of the string heard fading out over 40 ms as the string fades in (only what is heard is faded). Now the default for changes of three semitones or more.

14. **A content-preserving finger change for a waveguide** (2026-10-12, built, tested, then removed as not needed). Retuning a waveguide by reading its delay lines at a new length re-reflects and re-delivers the stretch of wave the length changed by. Moving the end physically instead — for a pull-off, shifting the returning line so its wave keeps its distance from the bow and zeroing the new stretch; for a hammer-on, discarding what lies beyond the new finger — keeps the wave in flight. Measured, it removed most of the artefact's off-harmonic energy; heard, it was not the cause of the blip. Worth a short note as a technique.

15. **Fidelity as a user setting** (2026-10-12, built). Four levels in each module's right-click menu, saved with the patch: 1 full rehearsal, 2 two starts tried, 3 starts guessed with bow changes rehearsed (the default, judged very good by ear in a string quartet and a piano quartet), 4 live. 

16. **A live player that needs no knowledge of the future** (2026-10-12, built). Level 4 rehearses nothing: the guessed bite for starts from the string, and for bow changes the turn the instrument's rehearsal chose most often (no extra press; 10 ms on the violin and viola, 20 ms on the cello, which rehearsal chose for 59, 42 and 43 per cent of changes). No planner thread, no second copy of the instrument, nothing announced ahead. Slightly rougher than level 3: on the Fauré violin 7 bad starts and 6 bad bow changes against 4 and 4; on the Swan 1 and 4 against 0 and 0.

17. **A lighter load can read as a heavier one** (2026-10-12). Flat out, levels 3 and 4 cost the same on the playing thread (5.2, 7.1 and 4.9 per cent of a core for violin, viola and cello against 5.4, 7.3 and 5.1). Paced to the clock, as an audio interface paces it, level 4's playing thread took about twice as long (the violin alone 16 per cent against 8), because with no planner threads busy the operating system ran it on a slower core or at a lower clock. A plugin's CPU meter measures time, not work: the level that does least can read highest. This is likely also why Rack's meter read about twice the bench's figures throughout.

18. **Where the playing thread's time goes** (profiled at level 3 on the violin, Apple M4): the bowed string's friction with its heat 28 per cent, of which the heat model's twelve-layer solves 24 per cent (once a sample, at 768 kHz); the four strings' waveguides 16 per cent, of which re-working the tuning for vibrato and bow moves 6 per cent; the body 3 per cent; the player's bookkeeping 5 per cent.

Earlier findings that belong in a fuller paper (2026-10-10 and 11, docs/bowed-strings.md and research/bowed-strings/bow-changes.md):

- Measuring brightness at a note's nominal harmonic frequencies is wrong for a bowed string: every friction plays a few cents flat, so the bins miss the partials; a high-pass share or slip rise time showed brightness equal across dynamics.
- A quiet rasp at pianissimo came from the bow too far from the bridge for its speed; a bow-place line by dynamic cured it (β 0.16 at pp on the violin).
- A start judged against its own later level counts a note that stays weak as at full level at once; judging against the loudest candidate's level cured uneven quiet notes.
- A pluck at a fresh note on a ringing string came from the bow landing in one sample and the finger in the same sample; a 4 ms landing and the finger going down first cured it.
- The body's DC drag, not its vibration, dominated the bridge force: high-pass before judging vibration level.

## Methods worth stating

- **Listening**: one listener (the author of the modules), at a fixed level within each comparison set, files with plain descriptive names, one per message, no spoken announcement; short excerpts (one second around a named note) to locate a fault; ideal synthetic tones to separate what the ear does from what the model does.
- **Measures**: bad notes (not one slip a period, or under 80 per cent regular gaps, from 60 ms to the end); time to speak (four regular gaps running from the bow's moving); heard squeak, click and roughness (finding 7); the non-repeating part of the sound by band (the signal minus itself a period later, band-passed, against the band's energy); roughness as one less the best correlation of four periods with the next four; spectrograms of the string's own bridge force.
- **Chance level**: a second render at another random seed, compared note against note (0 to 2 notes in a passage differ by 4 dB or more).
- **Held-out music**: settings chosen on the Fauré quartet's first and third movements and the Brahms trio's first two, tested on the Saint-Saëns Swan, the first movement of Brahms's viola sonata and the third of the trio.
- **Exactness**: every engine change checked bit for bit against the solo player (renders compared to the last bit with a 32-bit float output).

## What may be new

To be checked against the literature before claiming:

- A model player that rehearses every start and bow change on an exact copy of the instrument in real time, from notes announced ahead over a synthesiser's cable, and plays sample for sample what the rehearsal found.
- The measured split: a table by situation suffices for bow changes and not for starts from the string; the occasion, not the situation, decides a start.
- That a fixed start cannot be both prompt and smooth, and that two well-chosen candidates tried on the instrument recover nearly all of full rehearsal by ear at a fraction of the cost.
- A listener-matched measure of a start's harshness built from the rehearsal's own criteria applied to the played notes, with a seed-based chance level.
- The slurred-interval blip as spectral splatter heard dry, and a smoothing chosen by ear.
- The content-preserving finger change for waveguides.

## Limitations

- One listener; no blind trials; small numbers of passages.
- The model's bow is rigid at the contact by default; the one model with hair compliance tried was not tuned to the player.
- The two starts and the guessed bite were chosen from logged decisions on a few pieces; their generality across repertoire is not established.
- CPU figures are from one machine (Apple M4), the bench's solo player, and estimates for the module.
- The bad-note measure and the listener disagreed (finding 6); the heard measures agreed better but their thresholds do not carry across pieces.

## Open questions

- Why real strings are more forgiving than the model and the machines: the hair's give tuned to the player, a bow of finite width, or the player's force and acceleration settings themselves.
- Whether the table could choose the two candidates per situation rather than one pair for all (the Fauré cello part still had 11 bad starts with the fixed pair).
- A perceptual model of starts better than the measures here.
- Whether the slurred-interval smoothing is still needed through a room's reverberation.
- The playing thread's occasional single samples of up to 17 ms when the planner falls behind as strings enter.

## Where the evidence is

- Code: src/BowedString, BowedInstrument, BowedBody, BowedPlayer, BowedRehearsal, BowedEngine, BowedModule, BowedTable; test/bowplayer.cpp (`make bowplayer ARGS="score FILE PART SECONDS violin|viola|cello ..."` with `log=`, `table=`, `tableonly`, `guessonly`, `guessstarts`, `guessbite=`, `guessaccel=`, `few=`, `fewglide=`, `seed=`, `transpose=`, `noslursmooth`, `friction=`, `economy`), test/stringbench.cpp (`rehearsal=full|two|guessed`), test/starttable.py.
- Logged decisions: build/decisions/*.jsonl (15,362), with build/decisions/table.json and res/bowed-starts.txt.
- Literature: research/bowed-strings/attack-control-sources.md, thermal-elasto-plastic-2026.md, bow-changes.md, sources.md.
- Listening files sent: build/listen/ (Swan, Brahms viola, Fauré violin, Brahms trio violin, ideal tones), each named for what it is.
- Lost and to be rebuilt if the paper goes ahead: the analysis scripts used for findings 6 to 13 lived in the session's scratch folder (the sweep runner and reader, the note-against-note comparison, the band-split residual, the four-period roughness, the spectrogram images, the ideal tones). Their methods are described above.
