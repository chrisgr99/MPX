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

## Timing, and what a note does not carry

**Resolved: a note carries no beat of its own.** Its arrival is its time, and where that is comes from the harmony on the cable.

The case that tested it was swing. A processor that delays the off-beats breaks the correspondence between when a note arrives and where it belongs, so something downstream might want to know the beat it was written for.

**The resolution is an ordering rule.** Timing processors go last, after everything that cares about metrical position — chart, rhythm, accent, pitch, then swing, then the instrument. That is the natural order anyway: you decide what a note is and how hard it is played, and then you push it about for feel. Nobody swings first and then decides which chord tone it should have been. With that ordering, nothing downstream of a timing processor needs the original beat, because everything that needed it has already run.

### The rule, as three stages

The flat ordering was refined twice — once by the ornament processor and once by the bend processor — and it is more useful stated as stages, because that tells you where a module nobody has thought of yet belongs.

**Decide** — what notes there are, what pitch they have, how hard they are played. Needs metrical position, so it runs before anything moves a note. Chart, rhythm, accent, melody.

**Place** — swing and groove. Moves notes in time, and in doing so smears the correspondence between arrival and metrical position.

**Decorate** — ornaments first, then bend and the other lane shapers. Needs to see every note, including the ones added a moment earlier.

A processor belongs in the stage matching what it needs. Two rules follow from that and are worth stating on their own: **within Place, movers come before adders**, or an ornament comes adrift from what it ornaments; and **anything that must treat every note runs last**, or the notes an ornament added go untreated.

**A beat field would be worse than nothing.** With a beat on the note and an arrival time, there are two sources of truth. A processor that delays a note either leaves the field alone, making it a lie, or updates it, making it the arrival time written down twice. Redundant or wrong, with no third option.

The caveat, which is honest and small: chain two timing processors and the second reads positions the first has already smeared. Audio effects behave the same way and nobody finds it surprising.

One consequence worth building: a **swing processor should apply its own accent** if it wants one, since it is the last thing that knows which notes it treated as off-beats. A feature on one module rather than a protocol change.

---

## What else a module might want to know, and where it should come from

Three categories, wanting three different answers.

**Derivable — do not carry it.** Whether a note is a chord tone or a passing tone is one set-membership test, since the pitch and the chord are both on the cable. Carrying a flag would store a conclusion beside its own premises, where it can come to disagree with them after a transposer has run. The same goes for beat strength and for whether a note is in the key.

The rule: if it can be recomputed from what is already there, recomputing is safer than trusting.

**A property of time — put it on the harmony.** Cadences and phrases are relationships and moments, true whether or not a note happens there. They are also analysis rather than data: detecting a full cadence means recognising dominant to tonic, a half cadence an arrival on the dominant. Every consumer doing that independently is duplicated work and duplicated disagreement, so the chart module should do it once.

Which sets out what the harmony ought to carry: the current chord, the next, and the one after; beats until the change; the position in the cycle and the cycle's length; the bar, the beat within it, and the time signature; the section and whether this is a boundary; the phrase, and whether this moment begins or ends one; the cadence and its kind.

**Intent — the only candidate for a new lane, and the one to be careful about.** "This note is a resolution", "this is an approach note", "this is an ornament". Not derivable, because they are what the generator meant.

There is real use for them, and this is also the category that rots a protocol. Once one exists the vocabulary grows with no principle to stop it, which is what happened to MIDI's controller space.

**And it can usually be avoided, because intent is best acted on where it is produced.** The melody module already knows a note ends a phrase, so it can lengthen that note itself rather than labelling it and hoping something downstream obliges. Lane processors make that natural, since shaping a note is what they do.

If it ever genuinely cannot be avoided, the honest form is one small tag that a producer sets and a consumer interprets by private agreement, documented as exactly that rather than dressed up as a standard meaning.

---

## An ornament processor, worked through

Taken as a test of the ordering rule, and it refined it.

**Two families.** Rhythmic ornaments — a flam, a drag, a ruff — need no pitch and can run anywhere. Pitched ornaments — grace notes, mordents, turns, trills, slides — need to know what a step above means, so they need the scale and the chord, and they run after the melody module.

**It needs the note's duration**, which decides what ornament is even possible: a trill on a long note is many alternations, and the same instruction on a short note is a mordent. The processor should choose an ornament to fit the time it has rather than being told one and mangling it.

**And it needs to know where it is in the phrase.** Ornaments cluster at cadences and phrase ends in most styles. This is the strongest argument for the chart computing cadences once: an ornament processor with no cadence information decorates uniformly, which sounds mechanical in a way that is hard to name and easy to hear.

### The timing question is a musical question

Most ornaments happen before the beat, and a processor cannot emit a note earlier than it heard about one.

But that is only half the truth musically. Baroque ornaments are played ON the beat — the ornament takes its time from the main note, starting where the main note would have started. Later music and percussion put the grace note before the beat instead.

So the control on the panel is "on the beat or before it", and it maps exactly onto whether the processor needs to run the stream late. The on-beat form needs no lookahead and is historically correct; the before-the-beat form needs the fixed delay recorded above as the escape hatch. The constraint and the style choice are the same control, which is a coincidence worth taking rather than a compromise.

### Why it goes after the timing processors

If ornaments come before swing, the swing processor sees a grace note and its main note as two separate notes twenty milliseconds apart, and can push them to opposite sides of its decision. The ornament comes adrift from what it ornaments.

After means swing decides where the beat lands and the ornament decorates wherever the note ended up, which is what a player does.

The cost is that it reads a metrical position swing has already smeared, and that is tolerable here because what it needs is coarse: whether this is a cadence or a phrase end are bar-scale questions, and thirty milliseconds does not change the answer.

### It is the first module that increases the note count

One note in, several out — a trill on a two-second note could be twenty.

Nothing breaks, since the added notes do not overlap and the unbundler needs no more voices. But the note rate on the cable rises sharply, which is worth remembering when the ring buffer is sized.

It also means an ornament processor, an arpeggiator and a harmoniser are the same shape: one note becoming several, spread in time or stacked in pitch. If one of them works the others are variations on it.

---

## A pitch bend processor, worked through

The first processor that writes a CONTINUING lane rather than shaping a note at its start, which makes it the cheapest kind in the family.

Bend travels as updates tagged with a note's handle, sent while it sounds. So the processor passes the note-on through untouched and emits bend updates for the life of that note. **No delay, no lookahead, nothing held back.** It only has to remember which notes are sounding and when each ends, and the duration on the note tells it that.

### The vocabulary, and which parts are free

**Entry** — a scoop into the note, a slide from below, a pre-bend released down to pitch. Free, because the note is known the moment it arrives.

**Sustain** — vibrato, usually delayed and then widening, which is what makes it sound played rather than switched on. Free.

**Exit** — a fall or a doit. Free if it merely goes somewhere; not free if it targets the next note, which has not arrived.

Same shape as the ornament case, and the same escape hatch: a slide INTO the next note is free if it becomes that note's entry gesture instead of this note's exit. Which is also how a player thinks about it.

### What it needs from the cable

**Duration**, to fit the gesture to the time available. Vibrato beginning after three hundred milliseconds is nonsense on a hundred-millisecond note.

**Duration and phrase position again, to decide which notes are bent at all.** Bends cluster on long notes, at phrase peaks, and on the blue notes of the scale. A processor that bends everything sounds like an effect; one that bends a quarter of the notes sounds like a player.

### The detail that would be easy to get wrong

**It should set the bend range on the notes it treats.** The range is a note-on field saying what full deflection is worth, and it exists so the receiving end can produce a normalised control voltage. A whole-tone bend on a note whose range says twelve semitones barely moves that output.

A processor applying a two-semitone bend should say so on the note. That is within its rights, since the range travels with the note, and it means the instrument responds correctly without being set up by hand.

### Where it goes

In Decorate, after the ornament processor. Running first, the notes an ornament adds would never be bent, and a trill of unbent notes among bent ones is conspicuous.

---

## What already exists, and what Bitwig teaches

Checked rather than remembered, on 2 September 2026.

### The landscape

**Bitwig** is the closest thing built. Every note carries five expression dimensions — velocity, pressure, timbre, gain and per-note pitch bend — and they travel with the note through the whole chain. Its Note FX devices chain and combine, and its Note Grid is a modular environment for generating and processing notes. The architecture is therefore validated; what follows is what to take from it.

**Reason's Players** put note processors in a rack, which is the closest metaphor, on plain MIDI with no note object.

**Ableton and Logic MIDI effects** are the widely known version of the same chain, again on MIDI.

**TidalCycles and Strudel** are the functional equivalent and have by far the richest vocabulary of transformations. Worth mining for WHAT is worth having, even though the mechanism is nothing like a cable.

**Nothing in VCV Rack or Eurorack.** A polyphonic cable carries sixteen channels of voltage and nothing else; the "Polyphonic" tag means only that a module handles sixteen of them. There is no note object anywhere in the ecosystem.

### What is ours

**Harmony travelling with the notes.** Bitwig has no chord awareness in its note chain at all, which is what Scaler and Cthulhu are sold to fix.

**Determinism as the mechanism.** Bitwig's Randomize is random; TidalCycles is deterministic but has no equivalent of a path over a field.

**A modular rack**, where the chain is a cable you can see, split and re-order by dragging.

### Adopt: chance on every processor, not a chance module

Chance is everywhere in their set. Humanize has a chance that an arriving note is passed on at all; Multi-note has one per note unit; Note Repeats has one per repeat; Randomize applies per note across pitch, velocity, timbre, pressure, pan and gain.

Variation is **distributed** rather than centralised in one randomiser, and it costs one knob per processor.

**With one change.** Their chance is random. Ours must be driven by a deterministic draw — the same nought-to-one value the melody module takes — or recurrence is destroyed, and recurrence is what the system is built on. A chance that rolls differently every lap would undo it.

### Adopt: an adder scales, it does not replace

Their Echo scales velocity, pitch and the length of repeated notes. The Arpeggiator outputs notes with scaled velocity and a pitch offset. Multi-note spreads velocity per unit.

Nothing invents values: added notes are derived from the note that spawned them, which is what keeps them sounding related rather than pasted in. The ornament and arpeggiator processors should do the same.

### Adopt: correct or remove, as a choice

Their Key Filter corrects notes that do not match the key, or removes them — both wanted at different times. Any filter here should offer the pair rather than pick one.

Their Quantize has a **forgiveness** parameter: leeway rather than a hard snap. The same instinct, and worth carrying into anything that would otherwise force a value.

### Adopt: physical simulation as an expression generator

Ricochet treats notes as balls bouncing in a room, retriggering on collision, and uses each ball's position to animate panning and timbre. Dribble bounces with damping, losing height each time, and optionally **holds the last note out** — the sort of detail that only comes from using a thing, since the tail of a bouncing sequence wants to stop somewhere deliberate.

A simulation writing to the expression lanes is a shape MPX supports directly, and a different way to generate correlated values from the field idea — probably a more musical one for gestures.

### Worth considering: harmony by listening

Their Harmonize conforms notes to the active notes of **a different track**. Chord awareness with no chart at all: the harmony is whatever something else is playing.

For MPX that would be a second cable input, whose sounding notes define the chord. It cannot say what is coming next or where the cadence is, so it does not replace the chart — but it is a much cheaper path to a working harmoniser, it works with a keyboard being played live, and it is a good first module while the chart does not exist.

### The question their Note FX Layer raises

They run note processors in **parallel** as well as in series.

MPX assumes one cable into one input. If an input **merged** several cables — interleaving their events rather than choosing one — parallel chains come free, and so does combining several sources into one stream.

Rack's own convention points the same way: several cables into one input sum their voltages, and interleaving events is the note-domain equivalent. It is a small change to the reader.

Worth deciding deliberately, because it is far easier to allow now than to add once modules assume a single upstream.

---

## How the cable is carried, and what a reharmoniser shows about it

### What exists

The cable carries nothing. It is a real Rack cable so that Rack owns it — draws it, saves it, undoes it, removes it when a module goes — and it puts zero volts on the wire. Its only job is to say which source is joined to which destination.

The data lives in a table inside the plugin: sixty-four buses, each a ring of 256 events with a claimed flag, a generation number and a write index. An event is a fixed-size record — a kind, a lane, a handle, and then pitch, level, duration, pan and bend range for a note-on, or one value for an update.

**The handle is what makes it work.** Unique for the session, minted from a counter. A note-on names a note and every later message about it carries the same handle, so an off or a bend update reaches the right one with sixteen sounding.

Reading is by cursor: each consumer holds its own read index, so one source feeds several consumers, each draining at its own pace, and a reader attaching starts at the present rather than replaying everything ever sent.

### Harmony should be STATE, not events

A stream of chord-change events would leave a module that starts listening between changes knowing nothing until the next one. Rack's own model is the guide: a cable carries a value readable at any sample, not a stream that must not be missed.

So a bus becomes two things — a ring of note events, and a **block of current harmony** any reader can read at any moment. The chart writes it; everything else reads it whenever it likes.

Forwarding then costs almost nothing: a processor copies the upstream block into its own each sample. No stream to relay, nothing to miss.

### The rule that lets it survive its own future

**A processor mutates a COPY of an event. It never builds a new one field by field.**

Read the event, change the one field you care about, push it on. A lane added next year travels through every processor written this year, because they copied a record they did not fully understand rather than reconstructing one they did.

That is a discipline rather than a mechanism, so the shared code should make it the easy path: a processor declares what it changes and the framework does the copying.

### A reharmoniser, and the asymmetry it reveals

It reads the harmony block, transforms it, and writes a new one. Notes pass through untouched, and it belongs first in the chain — straight after the chart, before anything that reads harmony.

**Notes arrive as they happen; harmony describes what is coming.** The block carries the chord now, the next, the one after, and beats until the change — so a reharmoniser inserting an approach chord before a dominant already knows the dominant is coming and how much room there is, and can place the approach going forward in time.

Note processors have no lookahead and must work around it. **Harmony processors get lookahead for free.** That asymmetry is a dividend from carrying harmony as state rather than as a stream, and it was not obvious when that choice was made.

### What a reharmoniser needs, and why Roman storage earns its place

Reharmonisation rules are about FUNCTION, not about chords: before a dominant insert its predominant; replace a dominant with the one a tritone away; substitute the relative minor for a tonic.

Chords stored as Roman degrees relative to the key give function directly. Stored as letters it would have to be worked out against the key every time, and got wrong at every modulation. That is the strongest practical argument for the storage model GXW uses.

### Two operations, deserving separate controls

**Colour** — keep the root and the function, change the quality. Triad to seventh, seventh to ninth or thirteenth. This is most of what "make it jazzy" means and it is nearly risk-free, because the harmony still does the same job.

**Substitute** — replace a chord, or insert new ones. Tritone substitution, an approach before a target, a passing diminished, borrowing from the parallel minor. Interesting and much riskier, because it changes where the music is going.

Two knobs. Colour can be turned up freely; substitution wants using sparingly and turning down when a passage stops making sense.

### A style is a weighted rule set

Each rule a pattern to match — this function, in this position, with this coming next — and a substitution to apply. Pop is mostly colour with some suspension; jazz adds sevenths, approaches and substituted dominants; bossa, gospel and blues have their own small vocabularies. A style is data rather than code, so adding one is cheap.

With the chance driven by a **deterministic draw**, per the lesson from Bitwig. A reharmonisation rolling differently every lap would destroy recurrence, and the point is that the fourth chorus differs from the first AND comes back.

### Two consequences worth having

Everything downstream follows automatically: the melody and the bass both read the cable, so they get the new chords without being told, and they agree with each other because they read the same block.

And a chart viewer patched after it shows the REHARMONISED chart — which suggests the viewer should read harmony from a cable rather than only from the module holding the chart, so one can be put anywhere in the chain to see what the harmony is at that point. A better module than one welded to the chart, and it costs nothing extra.

---

## Open questions

Whether the field module belongs in this plugin at all, given that it knows nothing about MPX.

Whether an MPX input should MERGE several cables, interleaving their events, which would give parallel chains for free. Far easier to allow before modules assume a single upstream than after.

Whether 256 events a bus is still enough once a processor can turn one note into twenty. Enormous headroom while a note is a note; worth revisiting before ornaments and arpeggiators exist rather than after.

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
