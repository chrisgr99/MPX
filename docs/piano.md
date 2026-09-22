# mpxPiano

Status: first draft built.

A sampled acoustic piano in the MPX plugin, playing the Salamander Grand Piano. It takes only an MPX cable. Ordinary Rack signals reach it through toMPX, which turns pitch, gate, level and timbre into notes and carries the two pedals; that keeps this module to the piano itself.

## Why it is in the MPX plugin

An MPX cable is a real Rack cable, but the notes travel through NoteBus, a table inside this plugin. A plugin is its own compiled library with its own copy of anything static, so a module in another plugin sees the cable arrive and cannot read the table behind it. In this plugin, the piano reads MPX directly, including the bend, pressure and timbre lanes. The alternatives were a separate plugin fed through fromMPX, which loses everything MPX carries beyond pitch, gate and velocity, or a separate plugin that looks up an exported function in this one at run time, which breaks quietly when either is updated without the other.

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
- **Up to four MPX cables into the one input**, merged: a piano played by an MPX phrase can have notes added from a keyboard through toMPX.

### Pedals

- **Sustain** and **soft** arrive on the MPX cable, from toMPX's two pedal inputs, and a panel button for each does the same by hand and lights while down. Whichever is further down wins.
- **Sustain holds the dampers off**: notes ring on after their key is released until the pedal comes up.
- **Soft is the una corda.** The set has no una corda recording, so it is imitated by favouring the softer layers and darkening the tone.

## Pedals on the MPX cable

So that a phrase can carry its own pedalling rather than needing a gate patched alongside it.

**As state, not events**, for the reason the harmony is state: a module that starts listening while the pedal is already down would otherwise not know until the pedal next moved. The bus carries a sustain value and a soft value, each 0 to 1, readable at any sample; 1 is fully down, and values between allow half-pedalling. A processor forwards them as it forwards the harmony, by copying.

What each module does with them:

- **toMPX** has sustain and soft inputs and puts them on the cable: a gate is a pedal down, and 0 to 10 V is part way, for half-pedalling.
- **Every processor passes them through unchanged**: mpxComp, mpxArp, mpxRand, mpxMonitor, mpxPhrase and mpxMelody's voices each forward them as they forward the harmony.
- **fromMPX** sustain and soft outputs, so any voice in the rack can be pedalled from an MPX cable, and **mpxMonitor** showing the pedals, are not built yet.
- **mpxPhrase** may later write pedalling of its own, lifting the pedal at chord changes. Not part of the first version.

This is a change to the transport; `design.md` has still to be updated with it.

## Controls

### On the panel

For what is adjusted while playing.

- **Volume.**
- **Release, Hammer, Pedal**: the levels of the three mechanical components, so the piano can be made realistic or clean. The strings are the main sound, covered by volume.

  **A release follows the note it ends.** When a key comes up, the release sample is scaled by how loud that note still is at that moment — loud when the string is still ringing hard, faint when it has died away, as the damper sounds on a real piano. The Release knob sets the level on top of that. The note's loudness is already tracked for choosing which note to take when polyphony runs out, so this costs nothing further.
- **Brightness**: a tone control, darker or brighter than the recording. Modulated per note through the timbre lane, from toMPX's timbre input.
- **Dynamics**: how far the soft and loud layers differ. Down, every note sounds much alike whatever its velocity; up, the full range of the instrument.
- **Damping**: how fast a note dies once its key is released, with the sustain pedal up. Short is dry and detached; long is closer to a half-held pedal.
- **Sustain and Soft buttons.**

### Jacks

- In: MPX.
- Out: stereo left and right.

### In the right-click menu

For what is set once.

- **Velocity curve**: soft, normal or hard, to match the controller or sequencer.
- **Reference pitch**: A440 by default.
- **Stretch tuning**: on by default. Pianos are tuned slightly wide at the extremes, and an exactly tempered piano sounds faintly wrong.
- **Velocity layers loaded**: 4, 8 or 16.
- **Polyphony**: how many notes sound at once. When they are all in use, the QUIETEST is taken for the new note: a piano's notes decay, so the one that has faded most is the one least missed.
- **The samples**: download, location, remove.

**16 HP to start**, drawn with the same layout tools as the other MPX modules. With the inputs moved to toMPX it has room to spare, and a narrower panel may follow once the layout has been tried.

## Left out

- **Microphone position.** The set was recorded with one pair.
- **Sympathetic resonance**, the other strings ringing with the pedal down. The samples do not contain it, and it would have to be synthesized. A candidate for a later version.
- **Transpose.** V/oct already does it.
