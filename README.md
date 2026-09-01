# MPX — VCV Rack modules

**Modular Polyphonic Expression.** One cable carries a whole instrument, and every note on it keeps its own pitch, level, duration, pan, bend, pressure and timbre.

Playing one note in a modular rack takes several cables: pitch on one, gate on another, velocity on a third, and anything expressive after that. They have to be kept in step by hand.

These two modules carry one note's worth of everything on one cable, and carry several notes at once on that same cable.

The idea comes from MIDI Polyphonic Expression, which gives every sounding note its own bend, pressure and timbre. Nothing here reads or writes MIDI.

## The modules

**toMPX** gathers ordinary control voltages into one voice cable. Gate, pitch, level, duration, pan, pressure and timbre go in as polyphonic signals, and the cable carries every note they describe. Pitch, level, duration and pan are read at the gate's rising edge and held for the note's life. Bend, pressure and timbre are followed while the note sounds. A polyphonic gate makes several notes at once, all on the one cable.

**fromMPX** takes one voice cable apart into ordinary polyphonic control voltages: gate, pitch, level, bend as a control signal and as volts per octave, pressure, timbre, pan and duration. It allocates the notes among its voices, one channel per voice, so a polyphonic oscillator, envelope and amplifier patched to it play the notes with no adapter in between.

The intended arrangement is one fromMPX among each group of modules that implements an instrument, with a voice cable running to it from wherever the notes come from. Four instruments is four cables, and four toMPX modules to make them — a polyphonic cable in Rack carries one instrument's voices, so a module looking at one has one instrument to hand on. They are adapters, and they leave the patch entirely once a source speaks MPX for itself.

## How the cable works

A voice cable is a real Rack cable — Rack draws it, saves it with the patch and removes it when either module goes — but the notes do not travel along it. Rack cables carry a float per channel per sample, and a note is an event with a name, a start and an end. The events travel through a table inside the plugin, and the cable is what says which source is joined to which destination. Voice cables are drawn violet.

[docs/design.md](docs/design.md) has the whole of it, including why this is not sixteen channels of voltage and not an expander.

## Building from source

Set `RACK_DIR` to your Rack SDK and run `make`. Requires a C++11 compiler.

```
RACK_DIR=/path/to/Rack-SDK make          # build
RACK_DIR=/path/to/Rack-SDK make install  # build and copy into the plugins folder
```

## Licence

GPL-3.0-or-later. No artwork ships with the plugin — every panel is drawn in code.
