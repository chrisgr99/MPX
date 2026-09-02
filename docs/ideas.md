# Ideas, not decisions

A record of a design conversation held on 1 and 2 September 2026, about harmony, melody and the sources that drive them. Nothing here is settled and nothing here is built. It is written down so that it is not lost.

Read it as a set of positions arrived at by argument, with the reasoning attached, since the reasoning is the part worth keeping.

---

## Where it started

GXW generates melodies by moving objects along paths over images. Some attribute of the image at the object's position — a colour channel, or a formula over the firing context — becomes a number between zero and one. That number selects a note from the tones the current chord and key make available. Several objects move at once, so several notes sound at once.

The rhythm comes from a separate editor: a pattern of crosses, dots and digits laid against the same measures the chord chart uses. A cross is a note, a dot is a rest, and a digit is a ratchet of that many evenly spaced hits inside the slot.

The question is how to bring that into VCV Rack as several modules rather than one large one.

---

## What GXW actually does, from its source

**Chords are stored as Roman degrees**, not letters — a degree, an accidental and a quality, relative to the song key. A chart is therefore key-independent and transposition is a change of one number.

**Repeats are preserved, not flattened.** The progression is an ordered list of typed cells including bars, section openings, repeat marks, endings, time-signature changes and similes. `expandProgression` unrolls them into timed spans on demand.

**The whole harmony-to-melody interface is one call.** `harmonyAt(expanded, beat)` returns the current chord, the next chord, the one after that, and the number of beats until the change. Nothing else passes between the two halves.

**Phrases are derived from the changes alone.** Sections are hard boundaries; four- and eight-bar phrases are preferred within them; authentic cadences are detected; the spans are contiguous.

**The melodic step is one pure function**, about 350 lines with no dependencies, called once per note:

- **Palette** — every scale note in the register within a window of the previous note.
- **Weight** — an interval table peaking at a whole step and falling away through thirds to a nearly forbidden tritone; a chord-tone boost, larger on strong beats; a root boost for bass lines; a voice-leading boost for notes a step from a *next-chord* tone once the change is close; a register gravity; a descending bias; a phrase-boundary anchor toward tonic and dominant.
- **Pick** — the weights become a cumulative distribution, sampled by one number between nought and one.

Two macro controls generate the whole profile: **smoothness** sets the window and the leap aversion, **chordLock** sets chord pull against passing-tone allowance. The built-in styles are points in that space.

**There are three draws, not one.** Pitch, velocity and duration each resolve their own driver. They are correlated because they read the same moving point, not because they share a value.

**The melodic memory is one previous note, kept per object** and cleared on rewind.

---

## The system is deterministic

This is the property everything else depends on, and it was the subject of a correction during the conversation.

The field is addressed by position, not generated as a stream. The same image, the same path and the same starting beat produce the same notes every time.

The path is closed and its period is set to a whole number of harmony cycles. Each cycle within one lap reads a different stretch of path, so each is a different melody over the same changes; at the end of the lap the whole thing returns.

**From the second lap onward it is exactly periodic, first note included.** The thing immediately preceding the start of the path is always the end of the path, so every lap has the same predecessor. Only the first lap differs, because its opening note is preceded by the initial state, and even that settles within one note.

Three ways to work follow from this. Leave everything alone and the passage recurs. Move the path over the same field and you get a different line from the same material, which is why several paths over one image sound like they belong together. Change the field and you have changed the piece, and you can get it back exactly.

Recurrence is what makes it sound composed rather than endless. It is not a refinement; it is the mechanism.

---

## The proposed factoring

**In VCV the driver is a cable.** Whatever produces a control voltage can drive the pitch, velocity or duration draw. That makes images, noise, oscillators and sequencers interchangeable without anything being designed for it.

But **a driver for this system needs two things an ordinary module has not got**: a period expressed in harmony cycles, and a reset shared with the harmony. Without those there is no recurrence, which is the point. So driver modules are worth building rather than assumed.

### A family of rhythm modules

Two ways of making rhythm, deliberately two modules, with room for more later.

**mpxEuclid** — built. Four voices, each with steps, pulses, offset and a clock divider. GXW's own Euclidean generator would be subsumed into it; the thing worth taking from it is that its output is a full-length slot string, so a generated pattern can be displayed like a typed one and edited by hand afterwards.

**A pattern module** — the crosses, dots and digits, laid against the chart's measures. Slot count, an active-beats string that loops, and a parallel strength string for accent.

Both emit MPX notes carrying velocity and duration and **no pitch**.

**Ratchets are notes.** A ratchet is several closely spaced notes rather than one note with a property, which needs no protocol change and steps the melody generator once per hit. Because those notes are short, the duration rule below frees them to be passing tones — which is what makes a ratchet sound like a flourish rather than three chord tones hammered.

### The chart, and where harmony travels

**mpxChart** — holds a chart, imports it, displays it, takes the clock and reset, and owns the beat.

**Harmony rides the MPX cable itself**, rather than on a connection of its own. This replaces an earlier position in this conversation, which was that harmony should have a second bus of its own kind. Three things settled it.

**A driver needs the beat and has no notes.** A field module must know the cycle length and the position within it to set its lap period, but it produces control voltages and consumes nothing. With a separate chart connection it needed a link to something it otherwise had nothing to do with. With harmony on the MPX cable it takes the cable, ignores the notes and reads the beat.

**It saves no connection to separate them.** In a two-instrument patch the chart has to reach both rhythm modules either way. The fan-out is the same; what changes is that there is one cable type instead of two.

**And chaining becomes the shape of the system**: chart into rhythm into melody into instrument, one cable throughout.

So a cable carries a harmony and any number of notes played against it. "One cable is one instrument" stops being exactly true and becomes one **stream**. Two charts feeding one chain is a conflict, which is a user error and a detectable one.

#### The rule this revises

The design document states the test as: does the value belong to a note? Anything belonging to the performance instead — tempo, a macro, a pedal — stays off the cable, because it is the same for every voice and would make the cable mean two things.

Harmony fails that test as written, and the test is drawn in the wrong place. The better line is **whether it belongs to the note domain**. Harmony does: it is discrete, structural, and every note-domain module needs it. Tempo does not: it is continuous, and Rack already has cables that carry it properly.

So: notes and the harmony they are played against travel together; transport travels as ordinary cables.

### The melody module

An MPX cable in, a chart connection in, the draw voltages in, an MPX cable out with the pitch filled in. A processor rather than a source or a sink — a kind MPX does not have yet, and one the transport already allows, since a module can hold a reader and claim a slot at the same time.

Two of them on one chart gives a line and a bass that agree.

**Clipping a note's duration at the chord change** is nearly free and worth doing: a note ringing through a change is the commonest way generated lines sound wrong.

The previous-note memory must be per voice. Two lines sharing one would interfere in a way that would look like a bug and sound like a mess.

### Lane processors

The strongest structural idea in the conversation, and it is what turns MPX from a transport into a domain with a chain in it.

**A processor changes one lane and passes everything else along.** A pan processor that modulates pan and forwards the rest. A duration processor watching the harmony. A swing processor. Something that turns a plain flow of notes into a groove by adjusting timing and velocity together.

MIDI never really got this, because a MIDI effect has no note object to work on — it is a stream of bytes. MPX has a note with named lanes, so a module that touches one lane is the natural unit, and each one stays small. A pan processor is about a hundred lines. That is a far better way to grow the family than a few large modules with menus.

**What is easy** is anything that changes a value on a note as it passes: pan, velocity, duration, and the continuing lanes. Read it, change one field, emit. A tremolo written onto the pressure lane while a note sounds is the same shape.

Clipping a note at a chord change belongs here rather than inside the melody module.

**The one real constraint is that timing can only be delayed, never advanced.** A processor sees a note at the moment it happens; it cannot have known about it sooner.

That is less limiting than it sounds. Swing *is* delaying the off-beats. A laid-back feel is delaying everything slightly. Flams, lateness and dragging are all delays. What it rules out is pushing a note earlier, and quantising something that arrived late.

The escape hatch is standard: a processor that runs the whole stream a fixed amount late — a sixteenth, say — can move notes both ways within that window. It costs latency, which matters when playing and does not when sequencing, so it wants to be a switch on the processors that need it rather than a property of the domain.

Two bookkeeping details come with delaying. A delayed note-on must delay its note-off by the same amount or the note comes out short, so the processor holds a small per-note table. And a processor should keep the incoming handle when it is passing the same note along, minting only for notes it adds.

**Once the shape exists, a lot follows nearly free**: humanise, accent by bar position, legato and staccato, a filter that drops hits by probability, a register folder, an arpeggiator, a harmoniser that turns one note into three.

Each of those is a module somebody could write without understanding the rest of the system, which is the real test of whether the architecture is any good.

---

## Pass-through has to be automatic

The same requirement arrives from two directions, which is a good sign that it is the right shape.

Harmony riding the cable means a rhythm module — which cares nothing for harmony — must forward it, or the melody module downstream never sees it. Lane processors mean a pan processor must forward every lane it does not touch.

**Including lanes that do not exist yet.** A timbre lane added next year must not be dropped by a pan processor written this year.

So forwarding belongs in the shared transport code, and a module declares only what it changes. Left to each module to remember, one of them eventually will not, and the failure is silent: a lane quietly missing three modules downstream, with nothing to see.

---

## What the split makes possible that GXW cannot easily do

The note arrives with its duration already decided by the rhythm module, so the melody generator knows **before choosing a pitch** whether it is writing a held note or a passing one. In GXW the pitch and the duration are drawn at the same moment from the same context.

So three weighting terms become available, all of them multipliers on the existing score, needing no new machinery:

**Duration.** A long note is very likely a chord tone; a short one is free. This does most of the work of putting the important notes of the harmony in the places that matter. It wants to be a knob rather than a rule — pushed too far, every long note becomes a root and the line stops moving.

**Metrical position, properly.** GXW's strong-beat test is simply an even beat index, which treats beat three like beat one in four-four. The chart connection knows the bar and the time signature, so the downbeat can be strongest, then the half bar, then the other beats, then anything off the beat.

**Which chord tone.** GXW's pull is flat across all of them with a root bonus on strong beats. Root and third are what identify a chord; the fifth is nearly interchangeable; the seventh is colour that wants resolving. Ranking them would make a held note land on something that says which chord it is.

---

## The chart viewer

The drawing is not the hard part — a chart is boxes, lines and text, and the window itself is a solved problem in this plugin. The layout is the work: deciding what belongs in which bar is where repeat marks, section letters and endings attach.

Simpler forms share the same data model, so nothing is wasted by starting small: a text line per four bars is genuinely readable and fits a panel; a grid of bars without decorations shows the harmony; the full chart is where to end up.

**Selection should be by phrase, not by bar.** A passage that runs through a cadence and returns home sounds intentional; an arbitrary bar range does not. The phrases are already computed from the changes. Manual bar selection is the override, not the primary way.

---

## The field module

A separate module that knows nothing about MPX. It reads a path over a two-dimensional field and puts out control voltages, which makes it useful for anything.

### The one engineering decision that makes the rest easy

**Rasterise every field to a bitmap once, then sample the bitmap.**

A generator can then be as expensive as it likes, because it runs when a knob changes rather than per note. A loaded photograph and a generated field become the same thing with no separate code path. Processing applies uniformly to both. The display is the bitmap. Sampling is a lookup.

### What a photograph has that plain noise has not

**Several scales at once.** A single octave of noise has one scale and sounds like it. Four or five octaves, each at double the frequency and half the amplitude, give the long climbs with detail riding on them.

**A global arc.** Fractal noise is statistically the same everywhere; a sunset is not. An explicit gradient layer with a direction and a curve is the horizon, and it is what makes a path across the field rise or fall coherently rather than wander.

**Texture that varies by region.** Modulating the amplitude of the fine octaves with another slow field makes some areas busy and others smooth — the cloud against the clear air — so not every part of the field affects the pitch equally.

**Edges.** Cellular noise partitions the plane into regions with borders; posterising the lowest octave into a few levels does something similar. Either mixed in gives the abrupt changes.

**Domain warping.** Distorting the sampling coordinates with a slow field turns flat bands into cloud shapes rather than stripes. One extra lookup.

**Three channels that agree about the big shapes and differ in the detail.** The three draws must be correlated but distinct, which is what the colour channels of a photograph are. Three fields sharing their low octaves and differing in their high ones. Not three independent fields, which would decorrelate the draws, and not one field used three times, which would lock them together.

### The path

**Seed** — which field. This is the image, and it is one number, so a patch carries its material rather than pointing at a file.

**Position** — where the path sits over the field. Several outputs at several positions give voices that belong together.

**Size** — how much of the field the path crosses, which sets how fast the values change.

**Shape** — a circle; a Lissajous figure, which gives an enormous variety of closed loops from two numbers; or a circle wobbled by a low octave of the field itself.

**Laps** — how many harmony cycles one traversal takes.

### Other generators, and what each is musically

**Interference** — a few radial sine sources summed. Periodic in some directions, beating in others. The most sequencer-like field.

**Mandelbrot** — vast smooth regions and infinitely intricate boundaries, so a path gives long calm stretches punctuated by dense activity. Zoom depth yields new material from the same formula.

**Cellular** — distinct regions with hard borders. Abrupt changes rather than drift.

**Reaction-diffusion** — spots and stripes with organic boundaries. Needs simulating, which the bitmap decision affords.

**L-systems** — these produce line drawings, not fields. Rasterised, a path gives silence punctuated by events; using distance-to-nearest-branch as the value gives a smoother version. A different character rather than a better one.

### Processing is a musical control

**Posterise** turns continuous drift into discrete steps, which is the difference between a line that slides and a line that moves in intervals. A fundamental change to what the melody generator receives, from one knob.

**Blur** sets how fast values change under the cursor. **Contrast and curve** decide how much of the register gets used.

### On seeing it

The mechanism does not need to be visible to work, and can be hidden.

But what makes GXW composable rather than merely generative is that the material is **visible** — the cursor moves over the picture, so the relationship between what is seen and what is heard is legible, and a passage can be anticipated. That argues for the display being available even when it is not the point.

---

## Open questions

Whether a note still needs to carry **the beat it happened on**. Harmony riding the cable brings the beat with it, so a module reading the cable already knows where it is — which was the whole reason for wanting a timestamp. Probably answered, and worth checking rather than assuming.

Whether the field module belongs in this plugin at all, given that it knows nothing about MPX.

Whether loading a real photograph is worth the cost it brings: a patch then points at a file on the disc, so it stops being portable unless the picture is stored inside it.

---

## What the two decisions above have in common

Harmony on the cable and lane processors arrived separately and want the same thing: a module forwards what it does not consume, automatically, including what it has never heard of.

That one requirement is the load-bearing part of both. If it is built into the transport, both ideas are cheap. If it is left to each module, both ideas are a slow accumulation of silent bugs.

---

## Order of work, if it is taken up

The melody generator is the smallest piece and where the musical quality lives. It ports almost mechanically.

So: the melody engine against a simple progression set on a panel, before any import and any viewer — enough to hear whether the line sounds like music and to tune smoothness and chord-lock by ear. Then the chart model and import. Then the viewer and phrase selection.

The field module is independent of all of it and can be built at any point, since a control voltage is a control voltage.
