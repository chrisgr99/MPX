# Changelog

Newest first.

## 2.0.2 — 17 October 2026

### Changed
- The help for mpxScore, mpxViolin, mpxViola, mpxCello, mpxGuitar and mpxPiano, shown by Dreamer Help, is rewritten to say directly what each control does. mpxScore and mpxPiano have help for the first time, and mpxGuitar's Strum speed knob has an entry.
- mpxScore's built-in General MIDI player is called the sample player: on the panel, where its outputs are labelled SAMPLER, in its output names and in its help. It is intended only for auditioning parts for which no instrument module is available.
- The package includes the licences of FluidSynth (GNU LGPL 2.1), Verovio (GNU LGPL 3.0, with the GNU GPL 3.0 it adds to) and Verovio's music fonts, Bravura and Leipzig (SIL Open Font Licence).

## 2.0.1 — 17 October 2026

The first full release. It adds a score player, modelled bowed strings and a modelled guitar. Several more modules are included as work in progress.

### Added
- **mpxScore** — plays a MusicXML or Guitar Pro file, each part on its own MPX cable with a mute and a gain, in six or twelve rows chosen from the menu. A built-in General MIDI band plays a part on the stereo mix only when nothing is connected to that part's MPX output, so a part patched to another module is played by that module alone. The band plays GeneralUser GS, the General MIDI SoundFont by S. Christian Collins, which comes with the plugin. A window shows the score engraved as one continuous line, with a playhead and bars you can loop.
- mpxScore plays what the score marks: dynamics written as signs or as words, hairpins, legato, trills, mordents, turns, rolled chords, fermatas, tremolos and swing. Ornaments are played in the style of the composer, worked out from the file and shown in the menu, where it can be changed. A piano part is pedalled from the harmony wherever the score leaves the pedal unmarked; the menu turns this off. Each note is announced on the cable 4.5 seconds before it sounds, so a player that reads ahead can prepare it.
- **mpxViolin**, **mpxViola** and **mpxCello** — each plays one MPX part on a physically modelled instrument: four strings bowed through a model of rosin's friction, on one bridge and one body, with each note's bowing worked out ahead of it. They play pizzicato, left-hand pizzicato and double stops, and the notes of a chord share one dynamic.
- **mpxGuitar** — plays one MPX part on a modelled guitar, nylon, steel-string or electric: waveguide strings picked, hammered, slid, palm-muted, bent and touched for harmonics as the part says, through an acoustic body or a pickup and amplifier, in stereo.

### Work in progress
These modules are included and usable, but are still being developed. Their panels and sound may change in later releases.

- **mpxTrombone** — plays one MPX part on a modelled tenor trombone: an air column with a bell, buzzed by a model of the lips, with each note's slide position, breath and start worked out ahead of it.
- **mpxPiano** — the Salamander Grand Piano by Alexander Holm, played from an MPX cable, with sustain and soft pedals from the cable or the panel. The samples, 542 MB, are fetched once with the module's Download button.
- **mpxGuitarist** — plays an articulated guitar part as control voltage, with the bends, slides and hammer-ons in the pitch, gate and level.
- **mpxGuitarArticulation** — plays one MPX part through any polyphonic oscillator, with each string's own envelope and level.
- **mpxFingerPicker** — plays the chords on an mpxChart cable finger-style for mpxGuitar.
- **mpxFluidSynth** — six MPX parts through one FluidSynth and one SoundFont: the first in the DreamerMPX/banks folder in your Rack user folder, or GeneralUser GS, which comes with the plugin.
- **mpxChordVoicing** — decides where the notes of the chord on a cable go and publishes that voicing for everything downstream.
- **mpxPattern** — a rhythm written as text: where the notes fall, how strong each is and how high it aims.
- **mpxMelody** and **mpxVoice** — melodic lines drawn against the harmony on the cable, a master and one expander per line.
- **mpxSprites** — sprites move over a picture, pushed by a force field read from its colours, each sending notes as it goes.
- **mpxPathSprites** — eight reading heads running drawn lines and circles over a picture; what each passes over becomes a voltage.
- **mpxPathSpritePlayer** — sprites on drawn paths that play the notes a rhythm asks for, the pitch taken from the chord sounding.

### Changed
- toMPX and fromMPX are now mpxIn and mpxOut. Their slugs changed with their names, so a patch saved with 2.0.0 will not find them.
- mpxIn takes a sustain and a soft pedal and puts them on the cable.
- mpxOut can send level as a velocity and the soft pedal on its timbre output, and has a sustain output, for driving a MIDI instrument through VCV Host.
- A control's tooltip gives its name and value; the explanations moved to the help.
- The licence is MIT. 2.0.0 was GPL 3.0 or later.

### Removed
- mpxEuclid, mpxProgression and mpxComp. A patch that uses them opens without them.

## 2.0.0 — 10 September 2026

First release, as a pre-release. The modules work; the protocol is still a proposal, and the part
that would let other plugins speak it has not been written yet.

### Added
- **mpxChart** — reads iReal Pro playlists and plays their charts onto an MPX cable. A library of chord progressions is included.
- **mpxArp** — an arpeggiator on the chord from the cable, with a length that can be pushed into overlap.
- **mpxRand** — varies one attribute of the notes on a cable, by white noise, a random walk or Perlin noise.
- **mpxComp** — chordal accompaniment: voices a chord and leads the voices between changes.
- **mpxEuclid** — four Euclidean voices on one cable, each with its own steps, pulses and offset.
- **mpxProgression** — a dozen chord progressions everybody knows.
- **mpxMonitor** — shows what is on a cable and passes it on unchanged.
- **toMPX** — gathers ordinary control voltages into a cable.
- **fromMPX** — takes a cable apart into ordinary polyphonic control voltages.
- **polyToStereo** — a channel strip per voice: two level stages multiplied, a pan, and a stereo pair. Not an MPX module; any polyphonic patch can use it.
- A master random seed and a pass counter on the cable, so everything downstream that makes a random choice starts from the same number and a take can be reproduced.
