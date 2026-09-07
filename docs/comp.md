# mpxComp — chordal accompaniment

Takes a chord and decides where the notes go. A chord arriving as polyphonic volt per octave is a set of notes; it is not yet anything a player would play, and this is the part that makes it one.

Every chord out of mpxChart is voiced upward from its own root, so consecutive chords move in parallel and all the voices leap together — which is why the chart through a polyphonic oscillator sounds like a chord machine rather than like hands. mpxComp is a separate module rather than an option on the chart because voicing is a musical choice and the chart's job is to report the harmony faithfully. Being separate, it also works with anybody's chord source.

## The rule

On a chord change each voice takes the nearest unused tone of the new chord to where that voice already was, instead of the chord being rebuilt from scratch. Common tones do not move at all, the others step. It is the same rule as the chart's bass output, applied to several voices rather than one.

## In and out

| Jack | Carries |
| --- | --- |
| chord in | A chord as polyphonic V/Oct, from mpxChart or anything else |
| mpxIn | An MPX cable: the same harmony, plus the next chord, the beats until it changes, and where the beat sits in the bar |
| clock in | For the rhythm. Not yet built |
| chord out | The voiced chord, polyphonic V/Oct, one channel per voice |
| gates out | One gate per voice, in the same order |
| level out | One level per voice, in the same order |

**The cable wins.** With both inputs patched, the notes come from the polyphonic cable — that is the chord you can see — and the MPX link is what tells the rhythm where the beat is.

**Three outputs in the same voice order** so that an envelope and an amplifier per voice come from one pair of cables. Until the rhythm exists the gates hold while the chord sounds, dipping for a millisecond at each change so an envelope retriggers, and the level is flat at whatever the Level knob says. The jack is there now so a patch built today needs no re-patching when it is not.

## Controls

| Control | What it does |
| --- | --- |
| Voices | How many notes to play, which need not be how many the chord has. Three voices of a seventh chord is a choice a pianist makes constantly |
| Centre | Where the part sits, in semitones from middle C |
| Span | How far it may reach, in octaves |
| Spread | Close, drop two — the second voice from the top dropped an octave, which is the pianist's default — or open |
| Lead | Nought rebuilds every chord from the bottom, as a chord source does. One moves every voice as little as it can. Between them the movement is allowed but pulled back towards the plain voicing |
| Level | The level every voice comes out at, until there are accents |

## Not yet built

The rhythm: broken chords and accompaniment figures, and the accents that make them sound played rather than counted.

| Control | What it will do |
| --- | --- |
| Pattern | Block, broken up, broken down, Alberti, waltz |
| Gate | How long each note is held, as a fraction of its place in the pattern |
| Accent | Where the weight falls: even, metric, downbeat, backbeat, offbeat, or push — the accent landing just before the chord changes rather than on the beat |
| Amount | How much louder an accented note is. The style says where, this says how much |
| Humanise | A small variation in level, and eventually in timing |

These are on the **panel**, greyed and inert, rather than in the right-click menu, because a parameter can be mapped to a controller and automated where a menu item cannot. They are real parameters and can be mapped already. Each comes out from under its veil as its part is written, and the panel does not change shape when it does.

**Metric and push need no clock.** The harmony on an MPX cable carries the beat, the beat within the bar and the bar's length, so both stay correct through a chart that changes metre. Neither is possible from the polyphonic cable alone.

**An MPX output** is planned and deliberately absent. Once the module plays a rhythm it is emitting notes, and a note on an MPX cable carries its level and duration — an accented downbeat is then one event rather than three simultaneous cables a receiving module has to reassemble. But emitting notes makes this a source with a slot to claim, and the voicing was worth getting right first. The panel keeps the space.

## The panel

Fourteen HP. Three columns at 14, 35.5 and 57 mm; control rows at 30, 50, 72 and 92 mm, with labels 8.5 mm under each knob; the rhythm's heading at 62.5; jacks in at 108 and out at 120.

## Still to do

- The rhythm, which is the whole of the section above.
- The MPX output that goes with it.
- Spread is applied after the voice leading rather than being part of it, so changing Spread while a chord sounds moves voices that a player would have left alone.
- Nothing tests it. The obvious first test is the chart's chord output through mpxComp into a polyphonic oscillator, against the same patch without it.
