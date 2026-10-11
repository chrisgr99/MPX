# Pedalling the passages a score leaves unmarked

Most scores mark the sustain pedal in some places and not others. The Chopin Ballade marks 189 of its 262 bars; the arrangement of Finlandia here marks bars 1 and 3 and then nothing until bar 43. Played literally, the unmarked bars are dry: every note is stopped a fraction of a second after its written end, which is not how anybody plays a piano.

So the score module writes pedalling of its own into those passages, the way it writes out ornaments and tremolos: in the played timeline, before anything goes down the cable. Every instrument on the cable is then pedalled the same way, mpxPiano and a VST piano through mpxOut alike. The code is `src/Pedalling.cpp`; the measurements are `make pedaltest`.

## Which passages

A passage is unmarked when the score's own marks leave the pedal up for two bars or more, including before the first mark and after the last. A shorter gap between two marks is the writer lifting on purpose, and is left dry. The score's own marks are always played as written.

Only a piano's notes are listened to: a track whose General MIDI instrument is one of the six pianos, acoustic or electric. A harpsichord and a clavinet have no sustain pedal, and a song with no piano in it gets no pedalling.

## Where the foot changes

**Where a lift can go comes from the metre, and whether one is needed from the harmony.** Neither does the job alone. Lifting wherever the bass note changes catches nearly every real lift and lifts several times too often, because a left hand arpeggiating one chord changes its bass note all the time. Changing on every bar line holds through every chord that changes inside a bar.

**Where the beat falls matters most.** Across ten pedalled scores, with the pedal held from each real lift to the next:

| Beat | Beats | Carrying a real lift |
|---|---|---|
| First beat of a bar | 1,262 | 76% |
| Half way through a bar | 485 | 46% |
| Any other beat | 2,629 | 3% |

Nearly every real lift is on a beat: changing on every beat finds 96 to 98% of them. The candidates are therefore the beats, and at each one the rule asks how much what is about to be struck clashes with what is still ringing.

**Clash is roughness between pitch classes**, one ringing and one newly struck: a semitone 1, a whole tone and a tritone 0.5, a minor third 0.15, a major third 0.1, a fourth, a fifth and a unison 0. Each pair is weighted by how loud the two notes are, and the sum is set against the louder of the two groups, squared, so a faint ring under a loud chord, or one quiet note over a held chord, counts for little. A note's weight is its level times how low it is: a low string is louder for longer and its partials crowd everything above it, so the weight halves for every octave up from middle C and doubles for every octave down, between 0.15 and 3. A short note counts for less of the harmony than a long one. What is ringing fades with time since it was struck, over about three seconds an octave below middle C, longer lower and shorter higher.

There are two measures:

- **New clash** counts only pitches not already ringing, so an arpeggio of the chord held scores nothing. A pitch struck again counts only where it is struck at three times the strength of its ring.
- **Every pair** counts every pitch struck, and only what rings because the pedal is down — a note whose key is still held sounds whatever the foot does. This is what catches a bar that strikes the same chord again over the passing notes left ringing from the bar before, which a pianist clears.

**A bass of its own**: the beat's lowest note at or below anything left ringing with its key up, give or take a tone. Lifting then clears the old sound and loses nothing underneath. Lifting without one takes away the bass the harmony stands on, and a pianist holds the pedal for it. On first beats with nothing clashing, editors lift 36 to 50% of the time when the bar has its own bass and 10 to 28% when the old bass is left ringing.

**The rule:**

- On the **first beat** of a bar, change for any new clash at all, for every-pair clash above 0.03, or, with nothing clashing, when the bar strikes a bass of its own.
- **Half way** through a bar of an even number of beats, change for new clash above 0.02.
- On **any other beat**, change only for new clash above 0.08, after the pedal has been down at least 0.6 seconds, over a bass of its own.
- After **eight seconds** without a change, the next first beat changes whatever the harmony is doing.

A change is a lift and a retake on the spot: the foot comes up for 50 milliseconds as the new beat sounds and goes straight back down under it.

**The pedal stays up** for a rest in every voice, coming up where the sound stops and going down with the next note, and through a bar in which every note is staccato. A staccato beat inside a pedalled bar is not dry: a waltz marks its second and third beats short and pedals them with the bass on the first.

## How well it agrees with real marks

`make pedaltest` takes every stretch a score pedals for itself and plays it again as though it were unmarked. It counts the real lifts the rule finds, within 0.3 of a quarter note, and the share of its own lifts that are real. A lift followed within a beat and a half by the pedal going down again is one clearing, counted where it goes down; a mark repeated on a second staff is not counted. The rule was chosen on ten scores and checked on twelve more that were not looked at while choosing it:

| Rule | Tuning: found | Tuning: real | Checking: found | Checking: real |
|---|---|---|---|---|
| This rule | 90% | 73% | 81% | 51% |
| Every beat | 98% | 29% | 96% | 24% |
| Every half bar | 91% | 67% | 86% | 45% |
| Every bar | 74% | 76% | 77% | 56% |
| Every two bars | 38% | 75% | 33% | 47% |

A missed lift is the bad error, since it holds the pedal through a change of harmony and smears it; an extra one only sounds a little drier. Where the rule lifts and the editor did not, the clash is usually real and the lift is a reasonable choice. The scores that agree least are pedalled by their editors in ways no rule from the notes can find: the Waldstein finale's long pedals held through changes of harmony on purpose, the Moonlight finale's short pedals on accented chords, and Finlandia's own marks holding one pedal over five bars.

`make pedaltest ARGS="features=tuning"` prints how well each measure separates the beats that lift, and the tables the rule was read from. `make pedaltest ARGS="show=a-score.mxl 20 30"` lists every real and generated lift in a range of bars.

## Turning it off

The score module's right-click menu has **Pedal the passages the score leaves unmarked**, on by default and saved with the patch. Off, only the score's own marks are sent. **Send the score's sustain pedal** above it turns the pedal off altogether.

With it on, the score decides the pedal throughout, up as well as down, and says so on the cable. mpxPiano's Pedalling knob then stays out of the way, since a pedal left up is up on purpose. See `piano.md`.
