# MPX — design

## What the plugin is for

Playing one note in a modular rack takes several cables. Pitch on one, gate on another, velocity on a third, and anything expressive after that. They have to be kept in step with each other by hand, and a note source cannot start or end a note without all of them arriving together.

This plugin carries one note's worth of everything on one cable, and carries several notes at once on that same cable.

MPX is Modular Polyphonic Expression. The name is taken from MIDI Polyphonic Expression, which gives every sounding note its own pitch bend, pressure and timbre. Nothing here reads or writes MIDI: the name says which idea this is, not which protocol.

## The voice cable

A voice cable is a real Rack cable. Rack creates it, draws it, saves it with the patch, undoes its creation and removes it when either module is deleted.

It does not carry the notes. Rack cables carry one float per channel per sample, and a note is an event with a name, a start and an end.

The events travel through a table inside the plugin. The cable is what says which source is joined to which destination.

Once a frame, on the main thread, fromMPX looks at the cable in its Voice input. If the other end of that cable is a toMPX, the two are linked. Removing the cable unlinks them on the next frame, because the scan is the only thing that establishes the link. Patching happens at human speed, so a scan at frame rate is faster than it needs to be.

### Why not sixteen channels of voltage

Rack's polyphonic cables carry sixteen channels. One note needs nine lanes: gate, pitch, bend as a control signal, bend in volts per octave, level, duration, pan, pressure and timbre. Sixteen channels would therefore carry one note.

The point of the bundle is that one cable is one instrument, and an instrument plays more than one note at a time. Events have no such ceiling.

### Why not an expander

Rack's message passing between modules reaches the module physically beside it and no further. A voice belongs among the modules that implement it, wherever in the rack those are.

### What happens if a voice cable is patched somewhere else

toMPX puts zero volts on the wire, so a module patched to it receives zero volts. fromMPX fed by an ordinary module receives no events, because no link is registered.

### Slots rather than pointers

A voice holding a pointer to its source would hold a dangling one for the frame between the source's deletion and the next scan.

The buses are static, so a voice reads a slot that always exists and finds it unclaimed. A generation number closes the remaining gap: a slot freed and immediately taken by a different module does not silently inherit the old link.

### One producer, several consumers

One toMPX feeds as many fromMPX modules as are patched to it. Each keeps its own cursor into the source's ring.

### Timing

Rack runs every module every sample, so a gate edge is seen on the sample it happens on. No timestamps are needed.

Rack does not order modules by their patching, so an event can be read on the sample after it was written. That is one sample.

## What a note carries

Set at the gate's rising edge and unchanging for the note's life:

- **pitch**, in volts per octave
- **level**, the note's velocity: how hard it was struck
- **duration**, in seconds
- **pan**
- **handle**, which names the note so a later message can reach it

Sent as updates while the note sounds, each naming the note it belongs to:

- **bend**, how far the pitch has moved since the note started
- **pressure**, how hard the note is being played now
- **timbre**, the third per-note dimension: brightness, vowel, position along the string

### Holding is what makes a note a note

A source whose pitch keeps moving after the gate — an unquantised drift, or the next step of a sequence arriving early — must not drag a sounding note around with it. So the values above are read once and held.

### Bend

Bend is a deviation, not an absolute pitch. That is what a wheel, a wind controller and MPE all produce.

It is sent in volts, unscaled. toMPX computes it as the pitch input minus the value held at the gate's edge, so it is zero at every note-on by construction and the source patches one ordinary moving voltage.

**fromMPX's pitch output carries the bend already.** Where the note is now is what an oscillator wants, and a note bent up a semitone after it starts is one signal rather than a note plus a correction somebody has to remember to sum. A guitar line works without a second cable.

It also leaves on its own jack. The **Bend** output runs to five volts at full deflection, scaled by the bend range knob — how many semitones count as full deflection — and clamped there, as a wheel at its stop is. That is for the effects that should respond to a note being bent rather than to the pitch it is now producing.

The two are not two views of one number, which is why both exist. Bend in volts per octave is the truthful quantity — a semitone is a semitone whatever anybody's range knob says — but a semitone is 0.083 volts, which is far too small to modulate anything with. The scaled output is the convenient one and the pitch output is the truthful one, and each is used for the thing it is good at.

The range is in semitones but not whole ones, from a tenth of a semitone to two octaves. A quarter-tone bend, a scale that is not twelve-tone, and a range set by ear are all ordinary things to want.

### Level and pressure are separate

Level is the note's velocity, taken at the gate's edge. Pressure is how the note behaves after that. Neither is derived from the other.

A source that wants a velocity out of a continuous signal does that on its own face, where it is visible, rather than invisibly at the boundary.

### Breath makes the note, and toMPX does not know that yet

On a wind controller there is no key and no strike. Pressure rising through a threshold is the note starting, and pressure falling below a lower one is the note ending — the gap between the two thresholds being what stops a player breathing quietly at the edge from stuttering the note on and off. So on that instrument pressure IS the gate, as well as being the expression the note is played with.

Nothing at the far end changes for this. fromMPX's gate already carries the note's life whatever produced it, and pressure remains its own output because the effects a wind player expects — a filter opening as they push — respond to the pressure itself and not to the fact that a note is sounding.

What is missing is at the near end. toMPX takes a gate on one jack and pressure on another, so a wind controller needs the player to build the threshold and its hysteresis out of a comparator and a latch, which is fiddly to get right and belongs in the module that already knows what a note is.

Three things go together when it is built:

**The two thresholds.** Rising through the upper one starts the note, falling through the lower one ends it. Both adjustable, because the gap differs between instruments and between players.

**Where the velocity comes from.** A breath note has no strike, so the usual answers are the pressure at the moment of crossing, or how fast it rose through the threshold. The second is closer to what a player feels as attack and is what the better wind controllers do. Either way it is one number captured at onset, since level is fixed for the note's life while pressure goes on separately.

**Duration stops being a minimum.** A breath note ends when the breath ends and its length is not known when it starts, so toMPX's "at its duration, whatever the gate does" setting would cut notes off in the middle of a phrase. In breath mode the duration is a maximum or it is ignored.

### What does not ride on the cable

The test is whether the value belongs to a note. Pitch, level, duration, pan, bend, pressure and timbre do: every sounding note has its own.

Tempo, transport, a macro and a pedal belong to the patch instead, and are the same for every voice. They cross as ordinary cables. A voice never needs the tempo to play a note, because duration is in seconds rather than beats.

### An unpatched expression input sends nothing

It does not send zeros. A voice with no breath behind it falls back to its own envelope rather than being held shut by a lane reporting silence.

### Update rate

The continuing values are looked at every sixty-four samples and sent only when they have moved. A control that is not moving sends nothing at all, which is most of them most of the time.

fromMPX ramps to each new value over the same sixty-four samples, so the next one arrives as the ramp completes and the steps between them never reach the output.

A low-pass filter would be the wrong instrument for that. It cannot tell a step it should remove from a transient it should keep, and tonguing a note is a five to ten millisecond dip in pressure that a filter slow enough to smooth the steps is fast enough to blunt.

## Polyphony

A voice cable carries as many sounding notes as the source sends. toMPX takes its channel count from its Gate input, so a monophonic gate makes one note and a sixteen-channel gate makes sixteen, all on the one cable.

A voice in Rack is a group of physical modules, and a module cannot copy the group. So fromMPX allocates the notes among a number of voices and puts its lanes out as ordinary Rack polyphonic cables, one channel per voice.

Everything downstream is then ordinary. A polyphonic oscillator, envelope and amplifier patched to those outputs plays the notes with no adapter in between.

Multitimbral is one voice cable per instrument, each running to the fromMPX of its own cluster.

### Which voice a note takes

A free voice, if there is one. The **No voice free** knob decides what gives when there is not.

- **Take the oldest.** At one voice this is retrigger.
- **Take the quietest**, by the level the note was struck at.
- **Ignore the note.** A drum machine that cannot be interrupted.
- **Glide.** One voice, however high the voice count is set. The gate stays up and the pitch travels to each new note over the glide time.
- **Legato.** Two voices and no more. Notes alternate between the pair, so the note being left releases as the new one strikes. At one voice the notes butt: the old one ends exactly as the new one begins.

Glide uses one voice because that is the whole of it. At two voices every other note would find the second one free, start fresh with no glide, and half the line would slide while half jumped.

Legato uses two because only one note is ever giving way to one other, so a third voice has nothing to do. It is what a wind instrument does: changing the length of a vibrating column does not move the pitch — one resonance dies while the next establishes, which is why a slurred saxophone line sounds nothing like a portamento.

### Stealing

A voice taken while it is still sounding has its gate held low for one millisecond first. A gate that never falls is not an edge, so without that the note is replaced under a gate that stays up, nothing downstream strikes again, and what is heard is the first note decaying while its successors pass silently through.

The amplitude is not faded here. In this rack the envelope and the amplifier are separate modules, and fading the level output would fight the envelope rather than help it.

### A voice is not free until its sound has finished

A voice whose note has ended is not silent. Its gate has fallen and whatever envelope the patch put after it is in its release, so handing that voice to a new note cuts the release off and moves it to a new pitch part way through. No envelope can repair that, however note-aware it is: the pitch has already moved, and the fact that would have prevented it — how long this release lasts — is in a different module from the allocator.

**fromMPX takes the envelope back on a jack.** Patch the envelope that the gate drove into it, and a voice counts as busy while its own channel is above a floor of 0.05 volts, whether or not its note has ended. The channels line up by construction, since the gate that drove that envelope came from here. It works with any envelope — exponential, multi-stage, a low pass gate, a ten-second tail — because it watches the signal rather than modelling it.

With nothing patched the module falls back to the best guess available: among free voices it takes the one that has been free longest, since that is the one whose release is furthest along whatever its length. That is a heuristic and is meant to be; the jack is how it becomes exact.

Where every voice is either playing or releasing, a releasing one is taken before a playing one — and the quietest of them, read straight off the envelope. A tail at a twentieth of its level is nearly gone whether it started a moment ago or long since, so how far a release has got is the measure, not how long ago it began. With nothing patched, age is the stand-in.

That is not the ROLLOVER setting, which decides what gives when every voice is still playing. Its "quietest" means the quietest note — the level it was struck at — which is the right measure there and the wrong one here, where the notes have already ended and the only question is which sound is furthest gone.

### After a note ends

The gate falls. Pitch, level and pan are left where they are, so a voice in its release still reads the note it is releasing.

### Duration is a maximum

The duration is in the note-on as well as being what ends the note at the source, so a note-off that never arrives cannot leave a voice sounding.

## Voltages

| Value | On the cable | At fromMPX |
| --- | --- | --- |
| Pitch | volts per octave | volts per octave |
| Level | 0 to 1 | 0 to 10 V |
| Duration | seconds | 1 V per second |
| Pan | -1 to 1 | -5 to 5 V |
| Bend | volts | -5 to 5 V at the range, and volts per octave unscaled |
| Pressure | 0 to 1 | 0 to 10 V |
| Timbre | 0 to 1 | 0 to 10 V |

At toMPX the same scales apply to the inputs: level and pressure and timbre are 0 to 10 V, pan is -5 to 5 V, duration is one volt per second.

## The modules

**toMPX** and **fromMPX** are adapters: they bring ordinary control voltages into the domain and take them back out. A patch needs them only where a source or an instrument does not speak MPX itself.

**mpxEuclid** is a source. It has no adapter in front of it, which is what the transport was designed to allow and what this document claimed before anything but toMPX could do it. [euclid.md](euclid.md) describes it.

**mpxChart** is a source and the plugin's only reader of written music: it loads iReal Pro charts, shows one as a lead sheet in a window, and publishes its harmony. [chart.md](chart.md) describes it.

**mpxComp** is a processor of harmony rather than of notes: a chord in, the same chord voiced and led in, out as polyphonic pitch, gates and level. [comp.md](comp.md) describes it, including the rhythm that is planned and the controls that are on its panel waiting for it.

**mpxMonitor** lists what is passing on a cable, and **mpxProgression** is a chooser of short progressions.

## Limits

Sixteen toMPX modules can exist at once, each holding four voice cables. Each holds two hundred and fifty-six events, which is more than a sample's worth by a wide margin.

Sixteen voices per fromMPX, which is Rack's polyphonic channel limit.
