# MPX — VCV Rack modules

**Modular Polyphonic Expression.** One cable carries a whole instrument, and every note on it keeps its own pitch, level, duration, pan, bend, pressure and timbre.

Playing one note in a modular rack takes several cables: pitch on one, gate on another, velocity on a third, and anything expressive after that. They have to be kept in step by hand.

An MPX cable carries everything about a note, and carries several notes at once.

The idea comes from MIDI Polyphonic Expression, which gives every sounding note its own bend, pressure and timbre. Nothing here reads or writes MIDI.

## The modules

**mpxScore** plays a MusicXML or Guitar Pro file. Each part goes out on its own MPX cable with a mute and a gain, in six or twelve rows chosen from the menu. A built-in General MIDI band plays a part on the stereo mix only when nothing is connected to that part's MPX output, so a part patched to another module is played by that module alone. The band plays GeneralUser GS, the General MIDI SoundFont by S. Christian Collins, which comes with the plugin. A window shows the score engraved as one continuous line, with a playhead and bars you can loop.

**mpxViolin**, **mpxViola** and **mpxCello** each play one MPX part on a physically modelled instrument: four strings bowed through a model of rosin's friction, on one bridge and one body, with each note's bowing worked out ahead of it. They play pizzicato, left-hand pizzicato and double stops.

**mpxGuitar** plays one MPX part on a modelled guitar, nylon, steel-string or electric: strings picked, hammered, slid, palm-muted, bent and touched for harmonics as the part says, through an acoustic body or a pickup and amplifier, in stereo.

**mpxIn** gathers ordinary control voltages into one MPX cable. Gate, pitch, level, duration, pan, pressure and timbre go in as polyphonic signals, beside a sustain and a soft pedal. Pitch, level, duration and pan are read at the gate's rising edge and held for the note's life; a pitch that moves after that is sent as a bend, and pressure and timbre are followed while the note sounds.

**mpxOut** takes one MPX cable apart into ordinary polyphonic control voltages: gate, pitch, level, bend, pressure, timbre and pan, and the sustain pedal on one channel. It allocates the notes among its voices, one channel per voice, so a polyphonic oscillator, envelope and amplifier patched to it play the notes with no adapter in between.

mpxIn and mpxOut are adapters between MPX and ordinary control voltages. A source such as mpxScore or mpxChart puts notes on an MPX cable itself, and a player such as the bowed strings or mpxGuitar reads one itself, so neither adapter is needed between them.

The plugin has more modules, several of them still in progress. [CHANGELOG.md](CHANGELOG.md) lists them all.

## How the cable works

An MPX cable is a real Rack cable — Rack draws it, saves it with the patch and removes it when either module goes — but the notes do not travel along it. Rack cables carry a float per channel per sample, and a note is an event with a name, a start and an end. The events travel through a table inside the plugin, and the cable is what says which source is joined to which destination. A cable from an MPX output to an MPX input is drawn magenta.

[docs/design.md](docs/design.md) has the whole of it, including why this is not sixteen channels of voltage and not an expander.

[docs/ideas.md](docs/ideas.md) is a separate thing: a record of a design conversation about harmony, melody and the sources that drive them, held before most of the modules were built.

## Building from source

Set `RACK_DIR` to your Rack SDK. It needs a C++20 compiler, git and CMake. Run `make dep` once first: it fetches FluidSynth and Verovio and builds them into `dep`.

```
RACK_DIR=/path/to/Rack-SDK make dep      # once, before the first build
RACK_DIR=/path/to/Rack-SDK make          # build
RACK_DIR=/path/to/Rack-SDK make install  # build and copy into the plugins folder
```

## Licence

MIT, so that anything here — the protocol in particular — can be adopted by a plugin under any licence. No artwork ships with the plugin: every panel is drawn in code.

The fonts in `res` are not covered by the MIT licence. Petaluma and Petaluma Script are Steinberg's, under the SIL Open Font Licence; Hack is the Source Foundry Authors', under its own MIT licence. Each licence travels with its font. The music fonts in `res/verovio` come with Verovio. GeneralUser GS, in `res/banks`, is S. Christian Collins's, under his licence, which allows it in software projects and travels beside it.

The plugin is built with FluidSynth, which plays SoundFonts, and Verovio, which engraves the score window. Both are under the GNU Lesser General Public Licence.
