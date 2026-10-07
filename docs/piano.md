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
- **mpxOut** sustain and soft outputs, so any voice in the rack can be pedalled from an MPX cable, and **mpxMonitor** showing the pedals, are not built yet.
- **mpxPhrase** may later write pedalling of its own, lifting the pedal at chord changes. Not part of the first version.

This is a change to the transport; `design.md` has still to be updated with it.

## Controls

### On the panel

For what is adjusted while playing.

- **Volume.**
- **Release and Hammer**: the levels of two of the mechanical components, so the piano can be made realistic or clean. The strings are the main sound, covered by volume. The pedal mechanism's own level is in the right-click menu, where the knob that used to set it has been taken for the pedalling.

  **A release follows the note it ends.** When a key comes up, the release sample is scaled by how loud that note still is at that moment — loud when the string is still ringing hard, faint when it has died away, as the damper sounds on a real piano. The Release knob sets the level on top of that. The note's loudness is already tracked for choosing which note to take when polyphony runs out, so this costs nothing further.
- **Brightness**: a tone control, darker or brighter than the recording. Modulated per note through the timbre lane, from mpxIn's timbre input.
- **Dynamics**: how far the soft and loud layers differ. Down, every note sounds much alike whatever its velocity; up, the full range of the instrument.
- **Damping**: how fast a note dies once its key is released, with the sustain pedal up. Short is dry and detached; long is closer to a half-held pedal.
- **Pedalling**: how the piano pedals a passage its score does not mark, from off through a lift every beat, every half bar, every bar, every two bars, to held down. See below.
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
- **Sympathetic resonance**, the other strings ringing with the pedal down. The samples do not contain it, and it would have to be synthesized. A candidate for a later version.
- **Transpose.** V/oct already does it.
