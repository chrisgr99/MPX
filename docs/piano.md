# mpxPiano

Status: first draft built.

A sampled acoustic piano in the MPX plugin, playing the Salamander Grand Piano. It takes only an MPX cable. Ordinary Rack signals reach it through mpxIn, which turns pitch, gate, level and timbre into notes and carries the two pedals; that keeps this module to the piano itself.

## Why it is in the MPX plugin

An MPX cable is a real Rack cable, but the notes travel through NoteBus, a table inside this plugin. A plugin is its own compiled library with its own copy of anything static, so a module in another plugin sees the cable arrive and cannot read the table behind it. In this plugin, the piano reads MPX directly, including the bend, pressure and timbre lanes. The alternatives were a separate plugin fed through mpxOut, which loses everything MPX carries beyond pitch, gate and velocity, or a separate plugin that looks up an exported function in this one at run time, which breaks quietly when either is updated without the other.

Slug and name `mpxPiano`, in the pattern of the other modules.

## The samples

The Salamander Grand Piano, recorded by Alexander Holm and released under Creative Commons Attribution. Every minor third from A0 to C8, thirty pitches, with sixteen velocity layers, release samples, and hammer and pedal noise. The notes between samples are played by pitching the nearest sample.

Superdough's piano is 29 of those pitches at a single velocity layer. This module uses the full set.

The licence requires credit. Alexander Holm is named on the panel and in the manual.

### Getting them

The plugin does not contain the samples. The full set is several hundred megabytes, which would make it by far the largest download in the library.

- **Nothing is fetched until the user presses Download.** Until then the panel says the piano is not installed, with the button, the size, where the files come from, and the credit.
- **The download uses Rack's own `requestDownload`**, which reports progress; a bar on the panel shows it. No networking code of the plugin's own.
- **The files go into the Rack user folder, not the plugin's folder.** Rack replaces a plugin's folder when it updates, and several hundred megabytes fetched again on every update would be unacceptable.
- **The files are checked once they arrive**, so a download cut off partway reports that rather than playing half a piano.
- **One copy on disk**, shared by every mpxPiano in every patch.
- The menu can re-download the set, show where it is, or remove it.

**The release is the 16-bit, 44.1 kHz WAV set** from freepats.zenvoid.org, listed by the SFZ Instruments project: all sixteen velocity layers, a 394 MB download. A third of the size of the 24-bit, 48 kHz set, for detail that is below anything audible once the piano is in a mix, and at the most common engine rate it needs no resampling at all. Plain WAV means the loader reads one simple format of its own, with no FLAC decoder in the plugin.

### Memory

The samples are loaded once, into memory shared by all instances, and freed when the last mpxPiano leaves the rack. A second or third instance costs only its own voices. Its use cannot be limited to one instance, and this makes that unnecessary: nobody is penalised for adding a second.

The menu chooses how many velocity layers to load: 4, 8 or all 16, default 16. All sixteen cost the set's full uncompressed size once per session, held as 16-bit samples rather than converted to floating point; four is enough on a machine where that is too much. The ear hears the difference between one layer and four far more than between eight and sixteen.

**Loaded into memory, not streamed from disk.** Every sample is read in when the first mpxPiano starts and played from memory. Streaming — holding only the start of each sample and reading the rest from disk as a note plays — is how commercial samplers carry multi-gigabyte pianos, but it needs a thread reading ahead of every sounding note, and a note whose data arrives late glitches. The layer choice in the menu is the way to save memory instead.

## Notes in

- **MPX in**: notes with pitch, level and duration, and the bend, pressure and timbre lanes. Bend moves pitch. **Timbre moves each note's own brightness**, on top of the Brightness knob — timbre means tone everywhere else in MPX, and a piano's tone is the one thing about a note it would make sense to vary. **Pressure is ignored**: nothing on a piano responds to it once a key is down.
- **Up to four MPX cables into the one input**, merged: a piano played by an MPX phrase can have notes added from a keyboard through mpxIn.

### What is written over the note

A piano has one way to sound a note — a hammer at a speed — so every mark a score carries reaches it as some mixture of how hard the key is struck and how long the finger stays down, and nothing else.

### Balancing a chord against a single note

Three notes struck together are 4.8 dB louder than one and six are nearly 8, which is why a line alternating single notes and chords comes out lopsided — in the Ballade a single note sits next to a chord a thousand times.

A pianist does not answer this by dividing by three, because a chord really is louder than a single note. Two other things hold it back. The arm's weight is shared out among the fingers, so the same gesture across three keys gives each key less than one finger takes alone. And a pianist **voices** a chord: the note carrying the tune is played firmly and the inner notes are held back, which is one of the most characteristic things about good playing and the reason a real chord does not swamp the line.

So the top note keeps its level, the lowest keeps most of its weight since the bass is the harmony's foundation, and everything between is held back:

| struck together | was | now |
|---|---|---|
| two notes | +3.0 dB | +2.1 dB |
| three notes | +4.8 dB | +3.0 dB |
| four notes | +6.0 dB | +3.7 dB |
| six notes | +7.8 dB | +4.9 dB |

Still louder, which is right, but no longer drowning what it is answering.

### Balancing the hands

The voicing above balances notes struck together, which is half of it. A left-hand chord falling between two melody notes is a group of its own, so its top note was taken for the tune and given full weight with two or three more notes underneath: a four-note accompaniment chord came out 3.7 dB ABOVE a melody note at the same written dynamic, where a pianist puts it five to ten below.

A pianist balances lines, not notes. The melody is projected and everything under it is held back, all the time rather than only when they sound together.

The melody is the top of the texture, which needs nothing the cable does not already carry. Measured on the Ballade, of 462 moments where only the left hand plays, 455 are entirely below the melody last heard — two are not. The top is kept as a decaying maximum rather than a high-water mark, so a passage with no right hand becomes its own melody after a few beats and a bass line alone is not crushed by a tune that stopped playing.

**It is forgotten in music, not in seconds.** A second is four beats of a fast piece and less than one of a slow one, so a fixed rate per second forgets a Largo's melody in the middle of its first note and holds a Presto's long after it has moved on. The cable carries the played position in quarter notes, so the fall follows how far the MUSIC has moved: three semitones a quarter. It tracks a tempo change, and it stops when the transport stops, which is right — a melody does not fade from the ear because somebody pressed pause. Where nothing publishes a beat, as when a keyboard is played by hand, the clock is all there is and it falls an octave in two seconds.

Of the Ballade's 2,546 onsets, the number treated as carrying the melody:

| | half speed | as written | double speed |
|---|---|---|---|
| by the clock | 1,711 | 1,325 | 862 |
| by the beat | 1,451 | 1,451 | 1,451 |

Over all 2,546 onsets of the Ballade, where 1,221 are accompaniment with no melody in them:

| Melody balance | groups holding the melody | accompaniment-only groups |
|---|---|---|
| Off | +2.1 dB | 0.0 dB |
| Normal | +0.3 dB | −5.7 dB |
| Strong | +0.1 dB | −9.6 dB |

**Off is a real setting and not the absence of one.** A fugue has no accompaniment: its voices are equal, and holding the lower ones back is simply wrong, however right it is for a nocturne. Nothing in the notes says which a piece is, so it is in the right-click menu.

It will misjudge the 1% where the left hand takes the tune, and an inner melody with figuration above it, which Brahms and Schumann write constantly. MusicXML marks the staff on every note and the model discards it; carrying it would make "accompaniment" mean "the other hand" rather than "lower than the tune", and settle both cases.

### And spread in time

A pianist does not strike a chord at one instant, and the way they miss is not random. The melody note sounds **before** the rest, by twenty to thirty milliseconds. It is the best documented asynchrony in piano playing and it is not untidiness: a note arriving a little early is heard as a line of its own rather than as part of a block, which is how a player makes a tune stand out of a chord.

It also follows from how hard each note is struck. A key pressed harder accelerates its hammer faster and reaches the string sooner — the time from key to string runs from a few tens of milliseconds at a loud attack to well over a hundred at a soft one — so the notes played hardest arrive first whether the player means it or not, and the measured lead tracks how much louder the melody is than what is under it.

Which means this is not a separate invention. The chord is already voiced, so the order falls out of the levels themselves:

| struck together | melody | bass | inner notes |
|---|---|---|---|
| two notes | 0 ms | 10 ms | |
| three notes | 0 ms | 10 ms | 20 ms |
| four notes | 0 ms | 10 ms | 22, 23 ms |
| six notes | 0 ms | 10 ms | 22 to 26 ms |

Nothing moves that is not in a chord. The delay is measured from the loudest note, so that note falls exactly where it was written and a single note is never moved at all: the rhythm stays put and only the inside of a chord is rearranged. A note that speaks late still stops when it was written to, so the delay comes off its length rather than pushing its end along. A rolled chord arrives already spread and is left alone.

Nothing here is random. If the structure alone sounds human, randomness would only be noise on top of it. A chord is whatever is struck in the same sample; a rolled chord arrives spread over tens of milliseconds and is left alone, which is also right, since it is played note by note.

- **An accent is a faster hammer**, a quarter louder, and brighter with it because the recorded layers are brighter the harder they were struck. A strong accent is nearly half again.
- **A staccato is the same hammer and an early release**, taking the note to under half its written length.
- **A note under a slur is held forty milliseconds past its length**, which is the overlap between one finger and the next that makes a line sound joined rather than typed. An overlap of TIME and not of proportion: a pianist does not hold a minim ten times as far past the next note as a quaver, and the written notes of a score provide none of it — in the Ballade consecutive melody notes butt exactly end to end. It is capped against the note's own length so a short note cannot run past its neighbour, and mpxScore takes the slur off a note that has a real rest after it, since about silence the notes are a better authority than the engraver's phrasing.
- **Tenuto is the written length with nothing taken off it**, which is what a note gets anyway, so it changes nothing here and is carried for the modules that will want it.

### The pitch a recording was made at

Every note is pitched by how far it is from the pitch its recording was made at, so that one number decides whether the instrument is in tune.

**The map gets it wrong for middle C.** The Salamander's SFZ gives `pitch_keycenter` on every region except the sixteen for C4, where it is left out because sixty is the default — and whatever produced `map.json` took the bottom of the key span instead, writing fifty-nine. So B3, C4 and C sharp 4 were all pitched from a centre a semitone too low and played a semitone sharp. That span sits exactly where the two staves of a piano meet, so a hand in each clef was a semitone apart and anything crossing between them jumped.

The names are exact and the map is derived from them, so `keyFromName` wins where the two disagree and the name lands inside the span the region covers. Sixteen regions are corrected and nothing else is touched; a disagreement is written to the log, so a set that needs more than this says so rather than quietly detuning.

### One key is one string

A key struck again while it is already sounding does not get a second string: the sound already on it is faded out over four milliseconds and the new note starts on a voice of its own.

**Faded and not cut.** Rewinding the recording to the start of a fresh attack jumps the output from wherever the waveform happened to be, and a step in a waveform is a click. It is loudest where re-strikes are closest — an inverted mordent in the Tempest strikes F5 twice inside a fifteenth of a second. Four milliseconds is long enough to be a fade and short enough to be gone before the next note of an ornament arrives, so the string still does not accumulate: a trill holds two voices rather than twenty-three. There is one string per key on a piano, and a hammer meeting a string that is already moving partly re-excites and partly disturbs the motion that is there; what comes out is about another strike's worth of amplitude, not another strike added to the last eleven.

What is still ringing is carried into the new strike rather than thrown away, so a repeated note under the pedal does grow, up to half again as loud as a single strike would be. A string driven over and over settles at a steady amplitude; it does not keep climbing.

This matters most where it was found. The pedal holds the dampers off, so a note whose written length has run out keeps its voice and goes on ringing, and the trill in bar 23 of the Chopin Nocturne strikes two pitches twenty-three times in under two seconds. Taking a fresh voice each time left twelve copies of G5 and eleven of F5 sounding at once, thirty of the thirty-two voices gone, and the two small notes that finish the trill struck underneath all of it — inaudible, and reported as the ornament not being played at all. The copies do not even sum cleanly: one recording started at eighty-millisecond intervals combs against itself, so a trill came out as a blur rather than as a trill.

The bloom that a pedalled passage should have belongs mostly to the *other* strings rather than to the one being struck. See **Left out**.

### A note too new to have been heard is not taken

When every voice is in use the quietest is taken, since a piano's notes decay and the one that has faded most is the one least missed. A note that has only just been struck has made no sound yet, so by that measure it is the quietest voice in the pool and the first one taken — a note was being judged on its loudness before it had produced any. Measuring a run of the Nocturne showed the melody note after the trill taken ten milliseconds after it started, by an accompaniment note at a third of its level.

A voice is therefore passed over for its first fifty milliseconds, unless every voice in the pool is that new and something has to go.

### An ornament the writer has already written out

Scores say the same ornament twice. The visible note carries the sign, for the reader, and a voice of its own carries the notes it stands for, marked not to print, for the player. **183 of the 203 ornaments in the scores in use are engraved that way**, so playing both is not an edge case — it was happening to nine ornaments in ten.

Theirs wins. They chose the speed, where this would have used its own fixed figure, and `GpTimeline.cpp` drops the sign's realisation when it finds one.

**What proves it is a realisation is the neighbour.** Hidden notes are written for all sorts of reasons — a tie across a system, a voice that should sound and not show — so one merely falling inside an ornamented note is no evidence. What every mordent, trill and turn contains is the step above or below, and a hidden note a semitone or a tone away inside the span of an ornamented note is not there by accident.

**The written note is then cut back to where theirs begins**, which is what makes one rule cover the two ways engravers do it. In bar 44 of the Tempest the hidden voice holds only the upper note and the resolution, and the visible note is itself the first note of the figure: it has to stop when the hidden G arrives rather than ring on for its full written eighth. In bar 43 the hidden voice holds all three and starts on the same instant, so the cut takes the written note to nothing and it goes.

### Rolled chords

The wavy stroke down the front of a chord says to play it from the bottom up, or from the top down where the arrow points that way. It overrides the spread entirely, because the two disagree: the melody lead puts the top note first and a roll puts it last.

Thirty-five milliseconds a note, so a four-note roll takes a little over a tenth of a second. The score does not say — MusicXML's arpeggiate sign carries a direction and nothing else, no speed — so how long the hand takes over it is the player's, which is where it belongs.

**By the module's own pitch order, not by the index on the cable.** That index is counted within one voice of one staff, and a pianist rolls a chord through both hands: the two staves arrive as separate beats, each numbering its own notes from nought, so the published index starts again halfway up the chord. What is wanted is the order across the marked notes struck at that instant, which is the order the notes have already been sorted into.

**Only the notes the sign is written on are rolled.** A left-hand chord rolled under a right-hand melody note, as in the seventh bar of Chopin's first Ballade, has the sign on the left hand's notes alone, and the melody note sounds with the first of them. Anything struck at the same instant without the sign is played then, unspread. A roll written through both hands has the sign on the notes of both, and so goes through both.

### Where the weight of the bar goes

Nothing in a score says this and every player does it. A score says forte once and means it for twenty bars; what happens inside those bars is that a player leans very slightly on the strong positions. Its absence is a good part of why a sequenced performance sounds mechanical even when every marked dynamic is right.

The ladder is the one `Phrasing.cpp` already uses to decide where a phrase may break — a downbeat counts one, the middle of an even bar seven tenths, an ordinary beat a half, an eighth off the beat three tenths, anything finer a fifth. An ordinary beat is the neutral point and the emphasis is measured either side of it.

**The knob is two-sided.** Clockwise leans on the bar: at the end of its travel a downbeat is two decibels up and held four per cent longer, the middle of an even bar a little less, and the positions between beats fall back. Anticlockwise does the reverse and leans on what falls *between* the beats, which is what a syncopated line wants and what makes the same control useful for music written against the metre rather than along it. In the middle it does nothing.

**Both weight and time.** Players give a strong beat a little more length as well as a little more force, and the agogic half is arguably the larger part of what stops a performance sounding typed. Only the length is touched, never the onset: moving a note in time would walk away from the score's own cursor and from every other instrument reading the same bar.

**It does not argue with the score.** A note the composer has already accented is left out of it entirely. The weight has been placed by hand there, and a rule that knows only the bar line has nothing to add and a good deal to spoil — on a syncopation, which is exactly where accents get written, it would be pulling the other way.

It is deliberately small beside the voicing and the melody lead. Heard as a pulse rather than felt as one means it is turned up too far.

### Pedalling what the score does not mark

Most scores are under-marked. The Chopin Ballade marks 189 of its 262 bars and leaves the rest bare, and played literally those bars are dry: every note is stopped a fraction of a second after its written end, which is not how anybody plays a piano. The Pedalling knob fills those passages in.

**Harmony does not decide where the foot goes.** Measured against the real pedal marks of the Ballade and the Nocturne: a bass-note change finds 98% of the lifts but fires seven times too often, because the left hand arpeggiates inside one harmony; chroma novelty, the standard harmony-change detector, found 57% of them with a quarter of its guesses right. Both were beaten by changing the pedal on every bar line and nothing else. The metre does the work — 97% of the real lifts land exactly on a beat, and they come at one every two seconds in both pieces whatever the harmony is doing.

So the knob is an axis of how often, from driest to wettest, and the ear picks the point. What each position caught of the real lifts, over 497 seconds of the Ballade and 192 of the Nocturne:

| | Ballade | Nocturne |
|---|---|---|
| every beat | 94% | 90% |
| every half bar | 89% | 80% |
| every bar | 54% | 49% |

A missed lift is the bad error, since it holds the pedal through a harmony change and smears it. Lifting too often only sounds drier than intended. So the axis is safe at one end and muddy at the other, and every half bar is the closest match to real pedalling.

**No lookahead is needed.** A pianist's foot is reactive: it lifts as the new harmony sounds and takes the pedal again straight after. A rule that only ever acts at the moment a beat arrives can reach the same places as the marks, so this needs nothing but the beat, which arrives on the cable as state. It therefore works for anything publishing a beat and not only for a loaded file. Where nothing publishes one, the knob does nothing rather than guessing.

**It stays out of a pedalled score's way.** While the cable's pedal is moving, the score is pedalling for itself and knows better than any rule here. Once the cable has left the pedal up for two bars, it plainly is not pedalling that passage and the knob takes over. The Ballade needs exactly this and needs nothing set: Chopin's own marks from bar 32 on, and pedalling of our own through bars 9 to 25.

### Pedals

- **Sustain** and **soft** arrive on the MPX cable, from mpxIn's two pedal inputs, and a momentary panel button for each does the same by hand. Whichever is furthest down wins, counting the Pedalling knob.
- **Sustain holds the dampers off**: notes ring on after their key is released until the pedal comes up.
- **Soft is the una corda.** The set has no una corda recording, so it is imitated by favouring the softer layers and darkening the tone.

## Pedals on the MPX cable

So that a phrase can carry its own pedalling rather than needing a gate patched alongside it.

**As state, not events**, for the reason the harmony is state: a module that starts listening while the pedal is already down would otherwise not know until the pedal next moved. The bus carries a sustain value and a soft value, each 0 to 1, readable at any sample; 1 is fully down, and values between allow half-pedalling. A processor forwards them as it forwards the harmony, by copying.

What each module does with them:

- **mpxIn** has sustain and soft inputs and puts them on the cable: a gate is a pedal down, and 0 to 10 V is part way, for half-pedalling.
- **mpxScore sends the pedal the score is marked with**, read from the file's pedal brackets, and lifts and retakes it where two brackets touch. A right-click item turns it off for a patch that would rather pedal by hand.
- **Every processor passes them through unchanged**: mpxComp, mpxArp, mpxRand, mpxMonitor, mpxPhrase and mpxMelody's voices each forward them as they forward the harmony.
- **mpxOut has a sustain output**, nought to ten volts on one channel, so any voice in the rack can be pedalled from an MPX cable — a VST piano in Host through its sustain pedal parameter, for one. **The soft pedal comes out of its timbre jack** when the right-click menu says so, since una corda is a change of tone. **mpxMonitor** showing the pedals is not built yet.
- **mpxPhrase** may later write pedalling of its own, lifting the pedal at chord changes. Not part of the first version.

This is a change to the transport; `design.md` has still to be updated with it.

## Controls

### On the panel

For what is adjusted while playing.

- **Volume.**
- **Release**: how loud the strings' release resonance is, so the piano can be made realistic or clean. The strings themselves are the main sound, covered by volume.
- **Emphasis**: where the weight of the bar goes. See below. The knob was the key noise's, which is in the right-click menu now beside the pedal mechanism's own level — a mechanical noise is a hundredth of what the metre does to a performance, and nobody sets it twice.

  **A release follows the note it ends.** When a key comes up, the release sample is scaled by how loud that note still is at that moment — loud when the string is still ringing hard, faint when it has died away, as the damper sounds on a real piano. The Release knob sets the level on top of that. The note's loudness is already tracked for choosing which note to take when polyphony runs out, so this costs nothing further.
- **Brightness**: a tone control, darker or brighter than the recording. Modulated per note through the timbre lane, from mpxIn's timbre input.
- **Dynamics**: how much of the written dynamic range to use. The setting *is* the fraction of it — fully up is the full 18.5 decibels from ppp to fff, and 0.7 gives thirteen. It moves the floor and never the ceiling, so nothing gets quieter than it was, and it leaves the velocity layer alone, so a pianissimo keeps the tone of a pianissimo and only becomes easier to hear. See `docs/dynamics.md`.
- **Damping**: how fast a note dies once its key is released, with the sustain pedal up. Short is dry and detached; long is closer to a half-held pedal.
- **Pedalling**: how the piano pedals a passage its score does not mark, from off through a lift every beat, every half bar, every bar, every two bars, to held down. See below.
### As recorded

A right-click item, first in the menu: **As recorded: no level shaping, no tone filter.** It plays the recordings as the set's own SFZ asks for them and nothing else, so that when the piano sounds wrong beside another instrument it can be told whether the fault is in the samples or in what is done to them.

With it on, the velocity picks the layer and the amplitude follows the velocity at the SFZ's seventy-three per cent; each kind of recording plays at its group's volume. Gone are the marking aimed at a number of decibels, the crest blend, Dynamics, Emphasis, Brightness, the soft pedal's imitation, the chord's share, its voicing and spread, the melody lead, the rolled chord, what a written accent or staccato is turned into, the bloom of a re-struck string and the soft clip. Volume, Release, Damping, Pedalling and the sustain pedal still act. The limiter stays as a guard but moves out to ten volts, so below where the interface clips nothing is done to the level.

The velocity is the straight one, one plus 126 times the level, whatever the velocity curve menu says. That is what a MIDI instrument receives from mpxOut's level output, so with this on, the piano and an instrument fed from mpxOut on the same score are being sent the same notes, at the same velocities, at the same moments.

It is a check and not a way to play. Quiet passages come out very much quieter than the module normally plays them, because nothing lifts them: the SFZ's own pianissimo is about twenty decibels under its own forte.

- **Sustain and Soft buttons**, momentary: they hold while held, as a pedal does, so the pedal can be played by hand.

### Jacks

Four across the foot of the panel, evenly spaced.

- In: MPX.
- Out: stereo left and right.
- **Out: MPX, what the piano played.**

## The playing, out on a cable

This module is a pianist as well as a piano. Voicing a chord, letting the melody lead the notes under it, the legato overlap and pedalling by the metre are all decisions about PLAYING, and until the MPX output they could only be heard through these samples.

Published, they are notes like any others. Through mpxOut they drive an oscillator; mpxMonitor shows them; any processor can vary them.

**What goes out is what was played, not what arrived.** Each note is published at the moment it actually speaks, so the twenty milliseconds by which an inner note follows the melody are in the timing itself rather than described, the level is the voiced one and the length is the one the legato gave it. The pedals go with them — the button, the cable in, and whatever the PEDALLING knob is doing — so an instrument on the far end is pedalled the way this one is.

**The marks do not go out**, because they have already been applied. A note that arrived accented leaves louder; it does not also leave carrying the accent, which a second renderer would apply again.

### In the right-click menu

For what is set once.

- **Velocity curve**: soft, normal or hard, to match the controller or sequencer.
- **Reference pitch**: A440 by default.
- **Stretch tuning**: on by default. Pianos are tuned slightly wide at the extremes, and an exactly tempered piano sounds faintly wrong.
- **Velocity layers loaded**: 4, 8 or 16.
- **Polyphony**: how many notes sound at once. When they are all in use, the QUIETEST is taken for the new note: a piano's notes decay, so the one that has faded most is the one least missed.
- **The samples**: download, location, remove.

**16 HP to start**, drawn with the same layout tools as the other MPX modules. With the inputs moved to mpxIn it has room to spare, and a narrower panel may follow once the layout has been tried.

## Left out

- **Microphone position.** The set was recorded with one pair.
- **Sympathetic resonance**, the other strings ringing with the pedal down. The samples do not contain it, and it would have to be synthesized. This is where a pedalled passage's bloom actually comes from: every undamped string in the instrument picking up energy from the ones being played, and from their partials, which is broadband and spread across the whole instrument rather than concentrated on the pitches being struck. The deprecated piano model showed it is cheap once the strings share a bridge, so it is a known quantity rather than a research problem. The next piece of work on this module.
- **Transpose.** V/oct already does it.
