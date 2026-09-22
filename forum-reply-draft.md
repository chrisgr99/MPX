Thanks, both. There are two separate questions in this, so I will take them one at a time.

**Where MPX came from.** I did not set out to design a protocol. Fifteen years ago I wrote a generative polyphonic sequencer called GeoSonix — C++ on Qt — which produces notes in several independent voices and played them out over MIDI. That version is no longer maintained, but about a year ago I decided to take the idea further and reimplemented it in Web Audio, and alongside it built a Web Audio modular synthesiser, DreamRack, to play it, with voices as a first-class thing: each voice is a tab holding that voice's own group of modules. It runs in a browser at https://chrisgr99.github.io/DreamRack, and here it is playing polyphonic voices, using Strudel as the note source to test the idea: https://youtu.be/-zu7oc7BWHI. What I needed between sequencer and synth was an interface that was polyphonic in the way the sequencer already was, and that is where MPX came from. A good deal of it works in Web Audio now, and I am porting it to VCV because I want access to a far wider ecosystem of modules than I can write myself.

That origin is why the design looks the way it does. A generative sequencer does not emit gates and pitches; it emits notes, in a key, against a chord, with a duration known at note-on, and it goes on making decisions about those notes while they sound. Getting that into VCV meant either throwing most of it away or inventing somewhere to put it.

**One part of this is coordination.** Seven signals per voice, kept in step, arriving in one place. That part is ergonomics: for a four-module chain it is 21 cables against 3.

**The other part is information that is not a voltage.** Some of what a note carries has no sensible voltage representation at all:

- The chord currently in force, and the key — not as a CV, but as a set of pitch classes a downstream module can quantise or voice against.
- The identity of a note that is already sounding. Every note carries a handle, so the source can come back to it after note-on: end it early, or change it, because of something that happened at the source since it started. In a generative sequencer that is the ordinary case, not an edge case — the decision to cut a note or move it is made while the note is playing. With gates and pitches you can only address a voice slot, and once two notes overlap on the same pitch nothing downstream can work out which note-off belongs to which note-on.

Neither of those can be sent down a wire that carries a number.

**On the context menu and a special mode: there is no mode.** MPX has converters at both ends, and they are ordinary modules with ordinary jacks:

- **toMPX** takes seven conventional inputs — Gate, 1V/oct, Level, Duration, Pan, Pressure, Timbre — and bundles them into one MPX signal.
- **fromMPX** takes an MPX signal and breaks it back out to seven conventional outputs — Gate, 1V/oct with bend applied, Level, Bend, Pressure, Timbre, Pan.

So the boundary is a module, not a mode. Anything in the rack can generate an MPX stream, and anything in the rack can consume one. Processors in the middle forward what they do not consume — the monitor module exists partly to prove that, since it changes nothing and passes the stream on — so you can tap a chain anywhere with a fromMPX and take conventional signals off it without breaking the chain.

**Expanders could carry this.** A left/right expander chain is a sanctioned non-voltage channel in Rack already, and MPX could have been built on it. The cost is adjacency: an expander chain forces every module in a voice to sit physically next to every other one, in order. A cable lets me build the voice's identity in one place and run it to a cluster of modules somewhere else in the rack — several clusters, if the same note stream drives more than one.

**What I am asking here.** I am going to write a number of MPX modules either way, since I need them for my own sequencer. The question is whether anyone else has a use for the mechanism itself. If there is interest, I would package it as a library, so that anyone wanting to send or receive notes this way could do it without reimplementing the bus.
