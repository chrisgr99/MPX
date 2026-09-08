# mpxComp — chordal accompaniment

Takes a chord and decides where the notes go. A chord is a set of notes; it is not yet anything a player would play, and this is the part that makes it one.

Every chord out of mpxChart is voiced upward from its own root, so consecutive chords move in parallel and all the voices leap together — which is why the chart through a polyphonic oscillator sounds like a chord machine rather than like hands. mpxComp is a separate module rather than an option on the chart because voicing is a musical choice and the chart's job is to report the harmony faithfully.

## One way in

The chord arrives on an **MPX cable** and there is no other way in. There was a polyphonic input at first and it was removed, because a polyphonic chord is a set of pitch classes and every decision worth making here needs to know which note is which.

Three voices under a thirteenth chord means playing the third, the seventh and the thirteenth and leaving out the root and the fifth. That is not a rule you can apply to twelve anonymous semitones: you would be guessing the root from whichever channel came lowest and guessing the quality from the intervals, and every guess is a place the module plays the wrong three notes with no way for anyone to see why. An MPX cable carries the chord as a degree of the key and a quality, so the tones arrive already knowing what they are.

The cost is that mpxComp only works downstream of something that speaks MPX. That is the right trade: a jack that produces inferior results is worse than no jack, because nobody can tell by looking which of the two they got.

## Choosing the tones

Every quality is written out as a player reads it — extensions included, so a thirteenth chord is six tones and not a dominant seventh — and each tone carries a rank saying how badly it is wanted.

| Rank | What it is |
| --- | --- |
| 0 | The tone that says which quality this is. Usually the third; the fourth of a sus chord; the eleventh of an eleventh chord |
| 1 | The seventh, or what stands in its place — an altered fifth, a sixth, the second tone of a diminished seventh |
| 2 | Colour: the ninths and thirteenths that make a chart sound like the chart |
| 3 | The root, which a bass usually has anyway |
| 4 | The plain perfect fifth, which carries no information and goes first |
| 5 | A tone that is wrong here and is played only if there is nothing else — the third of an eleventh chord, which grinds against the eleventh |

Choosing which tones to play is then taking the lowest ranks and nothing more, with one setting that changes the table: see *Who has the root* below. More voices than tones doubles round the same order, so the sixth voice of a triad doubles the third rather than whatever happened to be first in the list.

## How much of the chord

The **Chord** control says how many of those tones exist at all, which is most of what separates one style of accompaniment from another as far as the notes are concerned. A country band's dominant is four notes and a jazz pianist's is six, and both are playing the chord the chart wrote.

| Setting | What is played |
| --- | --- |
| Triad | Root, third and fifth, and the fourth or second a sus chord puts where its third would be. A major seventh becomes a major chord; a thirteenth becomes a major chord |
| Sevenths | The four-note chord. A thirteenth becomes a dominant seventh; a sixth chord keeps its sixth |
| Extensions | The chord as written, ninths and thirteenths included. The default |

Taking the eleventh away restores the third, which the table had ranked last only because the eleventh was there to grind against it. Otherwise an eleventh chord played as a triad would come out with no third at all.

Two rules are written into the table rather than applied afterwards. An eleventh chord is played without its third, because a perfect eleventh a semitone above the major third is the one interval a comping voicing never contains. An altered dominant has no perfect fifth, because the alterations are what the name means.

## Who has the root

The **Root** control says whether anything else is playing it.

Set to **rootless** — a bass instrument, or the chart's own bass output, has it — the root is ranked fourth as the table has it, so it is among the first tones dropped and a three-voice part comes out rootless. That is correct: the root is already sounding, and a comping part that repeats it wastes a voice on a note nobody needed twice.

Set to **chord has the root**, the root becomes the tone kept first and the first doubled, and the lowest voice is pinned to it. The pin matters as much as the ranking. A voicing with the root somewhere in the middle still sounds rootless, so this is a constraint on the search rather than another cost — a root in the bass is not a slightly better version of a root in the middle, it is a different voicing.

Rootless is the default, because the chart has a bass output and that is the ordinary patch.

Rootless is the term a musician already knows. The other setting is not called rooted, because that would only say the root is present, and a root in the middle of a chord still sounds rootless — the point is that it is underneath.

## Placing the voices

Each voice keeps its pitch from the previous chord, and on a change the voices move as little as they can. Common tones do not move at all; the others step.

**It is a search, not a decision per voice.** Giving each voice in turn its nearest free tone is the obvious method and it is wrong often enough to hear: the first voice takes a tone the third voice needed, and the third then leaps. A chord change happens once a bar and there are at most six voices, so every way of handing the chosen tones to the voices is simply scored and the cheapest one played. A few thousand comparisons at the moment the chord turns over, and nothing at all in between.

**Voices do not cross,** and this is what makes them voices. The search fills them from the bottom upward and each must be above the last, so voice two is the same line before and after the change — which is what lets a voice that has not moved simply carry on sounding.

What is counted:

- How far each voice moves, with the top voice weighted heaviest. It is heard as the melody whether anybody wrote one or not, so the search spends its movement elsewhere first.
- The spacing the Spread setting asks for.
- A semitone between neighbouring voices, anywhere in the register. A thirteenth chord holds a third and a thirteenth a semitone apart and a player puts an octave between them.
- A tight interval low down, which is the one spacing fault that sounds like a mistake rather than a choice.

Movement counts at one per semitone and the rest below it, so a voicing that keeps its lines and spaces itself a little oddly wins over the reverse.

**Every setting takes effect at once.** A chord already sounding is voiced again the moment any of the controls moves, rather than waiting for the bar to turn over, because a knob that does nothing when it is turned reads as broken. Nothing is re-struck for that with the rhythm off — only a real chord change moves a note — or turning a knob would retrigger every envelope in the patch as it went round.

**The plain stack** — the chord built from the bottom of the range upward — is what Lead at nought asks for, and also the reference for a voice that did not exist before this chord. It is built in the shape the Spread asks for, or the first chord of a take would come out close however the panel was set.

## Out

| Jack | Carries |
| --- | --- |
| mpxIn | The harmony to play: the current chord as a degree of the key and a quality, the next chord and the one after, the beat, the beat within the bar, the time signature, and the beats until the chord turns over |
| mpxOut | The notes this module plays, and everything that arrived on the way in, forwarded |

**Two jacks, and both of them MPX.** A note on an MPX cable carries its pitch, its level and its duration as one event, so the three parallel cables this had at first — pitch, gate and level — were saying in triplicate what one event says once. `fromMPX` is the one place where any MPX cable becomes ordinary Rack signals, and putting the breakout there rather than on every source means it is the same breakout every time.

**And that is what makes a tie possible,** which is the point rather than a side effect. A voice that does not move is a note that is simply never sent again: one event carrying on, the way a player's finger stays down. Three cables could not say that — a gate cable has no way to mark one channel as carrying on — so every voice was struck afresh at every chord change, which is exactly the fault the voicing exists to avoid. What is now sent at a chord change is a note-off and a note-on for the voices that moved, and nothing at all for the ones that did not.

**A note's duration is a safety net, not what ends it.** The module sends its own note-off when a voice moves; the duration is what lets the far end finish a note whose off was lost. It is the rest of the chord with a fifth over, worked out from the beats remaining and the tempo — which nothing on the cable states, but the beat advances in real time, so watching how far it gets in a known number of samples is the tempo.

**There is no clock input.** The module is dead without an MPX cable and an MPX cable already carries the beat, the bar and the time signature. A clock jack could only ever have been a second timebase fighting the first.

**The trap to know about.** Nothing tells the far end how many voices are coming, so a four-voice comp into a `fromMPX` set to two voices loses notes silently, by stealing. Set the receiving module's voice count to at least this module's Voices. Something should say so on the panel; nothing does yet.

## Controls

| Control | What it does |
| --- | --- |
| Voices | How many notes to play, one to six, which need not be how many the chord has |
| Root | Rootless, or chord has the root — whether a separate instrument has the root, or this part plays its own and puts it underneath. See above |
| Register | Where the part sits, in semitones from middle C |
| Chord | Triad, sevenths or extensions — how much of the chord exists to be voiced. See above |
| Span | How far it may reach: one, two or three octaves |
| Spread | Close, drop two — the lowest voice an octave under the rest — or open, every other voice an octave up so the gaps are fifths and sixths |
| Voice leading | Nought rebuilds every chord from the bottom, as a chord source does. One moves every voice as little as it can. Between them the movement is allowed but pulled back towards the plain voicing |

## About styles

There is no control that says jazz, or country, or gospel. Most of what separates those is the rhythm, the instrument and the articulation rather than the voicing, and a genre control on a module with no rhythm would promise what it cannot deliver.

What differs in the voicing is narrower, and the panel already has it in parts a player can hear and set: rootless against root in the bass, close against drop two, three voices against five, and now four notes against six. A jazz voicing is rootless, four voices, drop two, extensions. A country voicing is rooted, three voices, close, triad.

Named styles are still worth offering once the rhythm exists, but as menu items that set the panel rather than as a parameter of their own. A preset is an action; the controls it moves stay visible and automatable, which is the rule the rest of the panel follows.

## The rhythm

**Off by default, and off is not an absence.** With the rhythm off the chord is held and tied — a voice that does not move is never re-struck — which is the module's other half and what anyone feeding a pad wants. Turning it on trades the tie for a figure, and that is a musical choice rather than a completeness. Switching either way clears up after itself: turning it on would otherwise leave the voices a figure never touches sounding for ever, and turning it off would leave the part silent until the next chord change.

**A step is the unit.** Rate says how often one falls — one, two, three or four to the beat — and the pattern says who plays on it. Steps are counted from the start of the cycle rather than from when the module was switched on, so two of these on the same chart agree without being told.

| Control | What it does |
| --- | --- |
| Rhythm | Off for a held chord, on for a figure |
| Rate | Steps a beat: one, two, three or four |
| Pattern | Block, broken up, broken down, Alberti, waltz |
| Gate | How long a note is held, as a fraction of its step |
| Accent | Where the weight falls |
| Amount | How much louder an accented note is. The pattern says where, this says how much |
| Humanise | A small variation in level |
| Strum | How far apart the voices of one step are struck, up to 90 ms |
| Balance | How much the top voice is favoured over the ones underneath |

Alberti is low, high, middle, high — the figure under half of Mozart's left hands — and the waltz is the bass on the first of three with the rest of the chord on the other two. Both are named after what they do rather than invented here.

**Accents come from the metre, not from a choice.** The harmony carries the time signature, so strong-weak-medium-weak is a lookup and stays right through a chart that changes metre. Four four is strong, weak, medium, weak; three four is strong, weak, weak; six eight is two groups of three. Downbeat, backbeat and offbeat are the obvious ones. Push is the interesting one: it leans on the last step before the chord turns over, which only the harmony knows is coming — it is the clearest thing on the panel that could not be done from a clock cable.

**Balance is the only control that varies level across the notes of one chord.** A pianist plays the top voice louder so the melody sits above its own harmony. An accent cannot do that, because in a block chord every voice is accented alike.

**A chord change sends nothing while a rhythm is running.** The next step plays the new voicing when it falls. Striking the chord at the change as well would put an extra note in front of the beat every time the harmony turned over.

**Still to come:** humanising the timing as well as the level, and a strum that can run downward as well as up.

## Where the code is

The search is in `Voicing.cpp` and knows nothing of Rack: given where the voices are, which tones they are to play and how much room they have, it says where they go. That is what makes it testable, which the module is not. `mpxComp.cpp` is what the panel means, handed to it. The chord written out with its ranks is `chordVoicingTones` in `Chord.cpp`, beside `chordPitchClasses`, which stays as it is — a quantizer wants three or four equal tones and a player wants all of them ranked, and both are right for what asks.

## The panel

Twenty HP. A choice between named things is not a knob: Root, Span, Spread, Chord, Pattern and Accent are lamp columns, one lamp per choice with its name to the right and the group named above. Voices is a knob with six detents, because it is a count and a count reads round a dial. The rest — Register, Voice leading, and the inert Gate, Amount and Humanise — are knobs because they are continuous.

Voices, Register and the Root column run down the left; Span and Spread stack in the next column; Voice leading and the Chord column take the right. The rhythm's heading is at 76.3 with its two lamp columns below it and its three knobs across at 60, 78 and 94. One row of jacks at 114, two in and four out.

**Every knob wears its marks.** Two ticks at the ends of the sweep for a continuous one, three for Register so that middle C is findable, and one per detent for Voices with the number beside it, so the count can be set by looking rather than by watching a tooltip. The sweep is 0.83 pi either way, which is 299 degrees and not the 270 a knob looks like it turns — the number comes from `componentlibrary.hpp`, because marks that disagree with the pointer are worse than no marks.

A scale is not an item in the layout. It is derived from its knob's own position and size every time the panel is redrawn, and moving a control in the editor redraws it, so the marks stay round their knob and there is nothing that can be left behind.

**Names sit two millimetres from the visible edge of what they name** — under a knob, over a lamp column, under a jack. The edge, not the centre: a RoundBlackKnob is 9.6 mm across and a PJ301M jack is 8.03, both read from the component SVGs, so the offsets differ. Setting one offset for both is what made the knob names look pressed against their knobs while the jack names floated away from their jacks.

A numbered knob is bigger than its knob, and its name clears the numbers rather than the metal: the sweep ends pointing down and to either side, so the numbers for one and six reach lower than the ticks do.

**It is twenty HP because of that rule.** Two-millimetre gaps and a numbered ring need the height, and four narrower columns hold what three wider ones could not. The jacks are one row rather than two for the same reason — a second row costs twelve millimetres that the controls above wanted more. Pattern reads UP and DOWN rather than BROKEN UP and BROKEN DOWN, since the group is called Pattern and the entry above them is Block; the longer wording made that column reach the one beside it.

Every position was checked rather than eyed: each control's visible extent worked out from the component sizes, and no two of them left closer than a millimetre.

## Still to do

- Nothing tests it inside Rack. The search itself has been exercised on its own; the obvious first test in a patch is the chart into mpxComp into a polyphonic oscillator, against the same patch without it.
