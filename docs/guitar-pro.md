# Importing a song from Guitar Pro

Generating a rhythm and a melody from nothing leaves too much to go wrong: about fifty knobs across three modules, most of which can be set somewhere no music goes, and a result that takes a phrase to arrive so no change can be tied to the knob that caused it.

A Guitar Pro file is a real arrangement — a rhythm somebody played, phrased the way somebody phrased it, with its dynamics and its articulations. Starting from that and varying it is a smaller problem than inventing it, and the starting point is known to be musical.

Nothing in the VCV library reads these files. The nearest are players that read MIDI, and MIDI throws away most of what makes the file worth having.

## The formats, and where each reader comes from

Files downloaded from the internet come in all of them, and the app itself is not in the loop, so all of them have to be read.

**gp3, gp4, gp5** — a sequential binary format, differing in the middle between versions. Ported from PyGuitarPro, which reads exactly these three and is itself a port of alphaTab, which was a port of TuxGuitar. Porting logic that has been exercised on a great many real files is safer than working the format out from a hex dump.

It is sequential, and that is the whole risk in it: a field skipped is not a field missing, it is every count after it read from the wrong place, and the file then says it holds an impossible number of tracks. Version 5.10 writes two things 5.00 does not — a byte saying whether the tempo is printed on the score, and two bytes of drawing flags after each beat's notes — and either one left out puts the rest of the file out by a byte.

**.gp — Guitar Pro 7 and 8** — a zip holding `Content/score.gpif`, which is XML. The zip's central directory names the entry, its DEFLATE stream is inflated here rather than by a library, and the XML is read by a pull parser of the kind IReal.cpp already is. A bare `.gpif` on its own is read by the same parser: that is what some sites hand you.

**.gpx — Guitar Pro 6** — two layers around the same gpif XML. BCFZ is a bit stream of back references and short literal runs, with no entropy coding and no checksum. Inside it is BCFS, a filesystem of 4096 byte sectors: a sector whose first int is 2 names a file and lists the sectors holding it. Ported from alphaTab, which is the only description of either layer.

## Reading a file must not take Rack down

Every count in the file is a number somebody else wrote. Each one is checked against what is left in the file before it is used, every string length is bounded, and a structure that does not make sense ends the import with a message naming the bar it failed in.

A file that cannot be read is a message on the panel. It is never a crash, and never a silent empty song.

## What a song becomes

The file is converted once, on import, into our own structures. From Michelle, which is one of the test files, that is: the title and artist; the tracks with their instrument, tuning, capo and playing style; master bars carrying the key, the time signature and the section letters and names; bars, voices, beats and notes; rhythms as note values with dots and tuplets; each note's pitch, fret and string; ties, slides, palm mute, let ring, accents and brush direction; a dynamic on every beat; the tempo map; and the lyric lines.

Repeats and alternate endings are unfolded into the played order, as mpxChart already does for a chart, so everything downstream sees one timeline.

## The harmony comes from the notes, and is not what the song is for

Many transcriptions carry no chord symbols — Michelle has none, and its chord collection is empty. So the harmony is worked out from what is played.

Over each window — a bar, or the span between changes where the file marks them — the pitches sounding in the accompaniment tracks are collected and weighted by how long they sound and where they fall in the bar. That is matched against chord templates, and the reading that keeps the root moving smoothly is preferred over judging each window on its own. The melody track is left out of the vote, or passing notes drag the answer about. Where the file does carry chord symbols, they are used and nothing is inferred. The full design, and how it is measured against the files that do name their chords, is docs/chord-inference.md.

What it is for is the modules that vary an imported part: re-voicing a chord, substituting a figure or moving a line all need to know what chord is sounding. It also gives the module a chart to draw, when the notation view is built.

What it is not for is feeding mpxComp, mpxMelody or mpxPhrase. Those were built to invent a part from a chart and a style, and a song gives them far more than they can take — the rhythm somebody played, the voicings, the dynamics, the articulations. Handing them a chord grid throws all of that away, and they would generate a worse part than the one already in the file. If they grow to read the richer material later, that is a change to them.

So the harmony is real and it goes out on the cable, but the song is not a way of driving the existing players.

## One module, not three

`mpxScore` reads a file and plays it. It is the only one of these now: `mpxGuitarChart` and `mpxGuitarChart6` are deprecated and hidden from the module browser.

They converged honestly rather than drifted. All three were generated from one header by a handful of defines, and the only differences left were the slug, the panel title and the row count — the score learned nothing the charts lacked, because everything went into the shared header and so into all of them. The score reads Guitar Pro and the charts read MusicXML; the file chooser offers the same nine extensions from either; a track with no tuning turns the guitar's assumptions off by itself.

Rows are six or twelve, chosen in the right-click menu, twelve putting a second column beside the first. That is the last thing the twelve-row chart had that the score did not.

**A slug is permanent**, so the two charts are still registered and a patch saved with one still opens. They are hidden, not removed.

**A pickup is a bar of its own length, numbered nought.** MusicXML marks one `implicit`, and it holds less music than the metre says — the E minor Prelude opens with one quarter in a bar of four, Autumn Leaves with three. Counted as a full bar, the difference was inserted as silence, so the music sat three quarters late from its first note and the first whole bar's notes arrived after the cursor had passed them.

A bar may also hold MORE than the metre says. A cadenza is written as a run over a held chord, and where the writer gave it no backup to return the cursor with, the run simply follows the chord: bar 32 of the Nocturne in E flat is two dotted halves — a full twelve-eight bar — and then thirty-two sixteenths, fourteen quarters of music in a bar of six. Counted as six, the run ran past the end of the bar into the ones after it, so a passage disappeared and the bars after it played two things at once. A bar is as long as what it holds, which for a cadenza is also what a player does with it. The Ballade has three such bars and the Nocturne two.

And a bar's number is not its position. A pickup is numbered nought and the first whole bar is one, so the two differ by one for the rest of the piece. Anything that shows or asks for a bar — a cursor on an engraving, a loop over a passage — uses the number the reader sees on the page, and the module translates. Of six scores here, three have pickups.

**The per-row audio outputs are gone.** A row is played through its MPX cable, and the band is heard through the stereo mix. The ports are laid out for twelve rows whatever the panel shows, which puts the mix on outputs twelve and thirteen — where it was before, when six rows each had an audio jack — so a patch wired to the mix stays wired to it.

One MPX cable is one instrument, always. That is what every module already downstream of a cable assumes — mpxVoicing decides where the voices sit, mpxArp arpeggiates the notes, mpxOut turns the cable into control voltage, and none of them asks which part a note belongs to. A cable carrying a whole band would have all of them playing every track at once, and making each one part-aware is a change in every module and a rule that some would honour and some would not.

So each track goes out on its own jack. A row is a mute, a jack, and the name of the track the jack is carrying — Jazz Guitar, Bass, Drums — which is the jack's caption. The names are part of the panel rather than of the layout, since they change with the song.

Mute rather than unpatching, so a part can be dropped and brought back without losing the cable or whatever was set downstream of it.

Six rows are shown to begin with, and a band opens on the right for six more, as mpxLine's voices do. Rack fixes a module's ports when it is created, so all of them exist from the start and the count decides how many are drawn; the width changes with it and Rack is asked to place the module again, which pushes whatever is to the right along.

A song with more tracks than there are rows is the normal case rather than the exception — nineteen tracks is an ordinary transcription, and what is wanted out of it is four or five: drums, bass, lead guitar, rhythm guitar. So a row chooses its track rather than taking it by file order.

The choosing is a button beside the jack. Clicking it opens a list of the song's tracks by name, and what is chosen becomes the jack's caption. The list is the one mpxChart's styles use: sized to itself inside a menu overlay, so it covers only as much of the screen as it needs.

Nothing stops two rows carrying the same track, and that is worth having: one of them varied and the other not, to hear the difference.

On loading a song the rows are filled with the tracks carrying the most notes, which is a reasonable first guess and is then changed by hand. Loading a second song keeps each row on the track of the same name where there is one, since a row already patched into something downstream should stay pointing at the same instrument; where there is not, the row falls back to the same guess. A row can also be set to nothing, which leaves its jack silent.

A jack patched into mpxPiano plays that track, which is how a song is auditioned before anything else is built. Nothing is needed for it: mpxPiano reads the note bus like every other consumer, and a song is another producer on it. Bends and vibrato arrive once there is something rendering them, since mpxPiano already reads the bend and timbre lanes. A percussion track is the exception — its note numbers are kit pieces rather than pitches, so it wants a drum module and not a piano.

Reaching CV is mpxOut on whichever tracks are wanted, which is how MPX reaches CV already. A polyphonic pitch and gate for one chosen track is easy to add later if that proves a nuisance for somebody who only wants to hear a file.

It stands beside mpxChart rather than replacing it. A chart is where a part is invented from nothing; a song is where a part already exists and is played, and then varied.

## Swing, which the score does not say

A knob on mpxScore, two-sided: clockwise swings late, anticlockwise early, the middle is even. Nought by default, so nothing already patched starts swinging.

**The amount and the ratio are different things.** The knob is an amount; what it means at the tempo being played comes from `Swing.hpp`, measured over 456 jazz solos and 22,574 eighth pairs, and it varies with tempo in both directions. The module publishes the amount and both ratios on the cable as the chart does, so anything playing alongside divides the beat the same way.

**A score has to move its own notes.** mpxChart publishes the swing and the modules downstream place their notes where it says — but those modules are inventing rhythm, and this one is playing rhythm that is already written. Nothing downstream will move a written note, so `swung()` moves it here.

**Only the off-half and the off-quarter of a beat are touched**, and that one rule does the work of three. A triplet sits on a third of a beat and is left alone, which is right — a triplet is already a three-way division, and swinging it would be swinging a swing. An ornament's small notes have their own fast timing and are not on the grid either. And anything written off the grid stays where it was put.

It also picks the level without being told: a note on the half-beat takes the eighth ratio and one on the quarter-beat the sixteenth, so a piece that mixes the two needs no mode set.

**A swung note keeps its written end**, so the first half of a divided beat grows and the second shrinks, which is the whole shape of it.

Simple metres only. A ratio divides a beat in two and a compound metre has already divided its beat in three, which is the thing swing imitates. The bar lines and the beats themselves never move.

**It is gentler at slow tempos, and that is the corpus and not a fault.** Players swing their eighths hardest around 140 to 180 beats a minute and much less at a ballad tempo, where the dividing happens at the sixteenth level instead. At the knob's full travel:

| | 60 | 80 | 120 | 160 | 200 | 260 bpm |
|---|---|---|---|---|---|---|
| off-beat eighth lands at | 52.8% | 54.2% | 57.3% | 58.8% | 58.2% | 55.9% |

## What it does not read

A file using something the reader does not know is imported as far as it goes, and what was skipped is written to Rack's log. Nothing is said on the panel: a song that plays is useful even where a bend or an unusual tuplet did not survive, and a panel full of warnings about things the listener cannot hear is worth less than the music.

One known gap. A Guitar Pro 6 drum track gives no pitch for its notes — which piece of the kit is struck is a position on the drum staff, read through the instrument's own list of elements, and that list is not read yet, so those notes come out unpitched and counted.

## Seeing the part

A later phase draws one chosen track as written music, as mpxChart draws a chart, so the part can be followed while it plays. The file carries everything that needs — the rhythms, the fret and string of each note, the articulations — so nothing has to be kept for it now.

## What goes out on the cable

The notes, with everything the file said about how they are played: the string and the fret, and flags for hammer-ons and pull-offs, legato and shift slides, slides into and out of a note, let ring, palm mute, dead notes, ghost notes, accents, harmonics, tapping, staccato and tremolo picking; the vibrato; the grace notes; which way a chord was brushed and where in the stroke each note falls; and the bend as up to four points of offset and cents.

**Ornaments are written out as the notes they stand for.** A trill is the written note and the one a step above it in the key, alternating for the note's whole length, starting and ending on the written note; a mordent is main, below, main; an inverted mordent — which is what a short trill is written as — is main, above, main; a turn is above, main, below, main. The short ones are a flick at the start and then the note itself, never taking more than half of it.

A step in the key, not a tone and not a semitone: the Nocturne's trill is on F in E flat major, where the step above is G; in another key the same F trills to F sharp. The key signature names seven notes and the neighbour is the next of them either way. The speed is a time and not a fraction of the note, about a dozen a second, because an ornament is fingers rather than tempo.

A wavy line is a trill too, where it STARTS one. The line is drawn as a pair, a start on the note the trill is on and a stop where it ends, and taking either for a trill put one on the note that merely ended it — bar 7 of the Nocturne carries a stop whose start is nowhere in the file, and a trill appeared there that nobody had written.

**`make marksused` counts every mark in a pile of scores and says whether we play it.** Ornaments were being found one at a time by ear — a trill missing in one bar, a short one in another — and a mark that is read and ignored sounds exactly like a mark that was never there. Across the scores here it found 12 inverted mordents, 25 wavy lines, 3 mordents and 2 turns going unplayed, none of which anybody had noticed. It now reports only bowing marks, fingerings and tuplet brackets as unplayed — the first two meaning nothing to a piano and the third being how a rhythm is drawn rather than what it is.

**A fermata is a pause, and everything waits for it.** It holds one beat longer than written and then the music goes on at the tempo it was at, so it is a change of clock rather than of notes — a rest under a fermata is held too, and stretching only the notes would leave the silence unstretched. How long is written nowhere: players hold about half as long again to twice, and this takes the middle of that. Written in every part at the same moment, so the first found at a position is the pause and the rest are the same pause written again. The Ballade has two, the Nocturne four, Autumn Leaves two.

**A mark applies to the note it is written on.** The loudness line holds a step as two points at the same moment — the level up to there, then the level from there — and reading the first of them gave a note sitting exactly on a mark the dynamic BEFORE it. Marks are written on beats and notes are played on beats, so that was most of the notes carrying a mark at all: a note on an ff played at the p before it. In the Ballade 74 notes were wrong, the worst by two thirds of the whole range.

**And ornaments before one note follow each other.** They are all written at the position of the note they lean on, so taking that literally sounded three of them at one instant — a faint blur where a run of three quick notes belongs.

A piece written for the piano is marked up differently from a transcription for the guitar, and what a MusicXML file carries is read as well: the dynamic marks and the hairpins, worked out into a level for every note from the whole part at once, since a hairpin's destination is written nowhere but at the next mark; a crescendo or a diminuendo written as a word rather than a hairpin — cresc., calando, smorzando — which has no bracket to close it and so aims at the next dynamic mark, however far off, with several in a row making one journey to it rather than a step each; accents and strong accents, staccato and staccatissimo, tenuto; the slurs, as a flag on every note inside one saying it runs into the note after it; rolled chords, which are a brush and travel as one; and the sustain pedal.

A hairpin leading to a dynamic puts its stop and that dynamic at the same moment, which is the commonest marking in music. That took three goes to read properly, and all three faults sounded like the same complaint — the music going somewhere nobody wrote. A look-ahead that skipped everything "not later than here" skipped the very mark the hairpin led to, so most hairpins fell back to a blind step; reading the mark before the stop meant arriving at pp and then taking another step down below it; and taking the next mark from anywhere ahead meant five bars of alternating hairpins in the Ballade all aiming at one p four bars later, so they came out flat and the crescendo among them came out backwards, taking the music down from f to p. A stop is now settled before a mark at the same moment, since it is the mark it was leading to; a mark further off than a bar belongs to something else and the hairpin travels a step of its own; and a hairpin means what it says, so one that would arrive no louder is a crescendo still.

A word says which way the loudness is going and only a mark says how far, which is why they are read together. The Ballade reaches bar 60 at p and is marked calando, then smorz., then più dimin., and says pp at bar 68: between them those are one long fade a fifth quieter, not three instructions to drop a dynamic each. Read as a step apiece they take the music to a seventh of where it belongs, and bars 62 to 67 came out at the floor — the composer asked for quiet and got all but silence. Where the next mark contradicts the word, the word wins and the ramp travels a step, since a crescendo arriving no louder is not one, and no ramp whose destination nobody wrote may fade below ppp. A step, where one is taken, is one dynamic off the ladder the marks make — about an eighth, not the quarter it used to be, which was two dynamics at a stroke and put an eighth of the Ballade at full.

The pedal is the one that is not a property of a note. It travels as state, like the harmony and the instrument, because a module that starts listening halfway through a pedalled bar has to know the dampers are up now rather than when the foot next moves. MusicXML writes it as a bracket — a start and a stop — and the model keeps the moves of the foot that the bracket stands for, because what a player has is a foot and not a list of brackets.

Where two brackets touch, which is how a pedalled piece is usually written, the stop and the start are at the same instant and read literally they are a foot that never leaves the floor: the whole piece would ring into one chord. That join is the moment the pianist clears the sound, so the two marks are made into a single change — lifted and taken again — which is both what is meant and the most audible thing a pedal mark carries. The Chopin Ballade is 250 brackets and 204 of those joins.

The middle pedal is read and carried and not sent. Sostenuto holds the keys that are already down and nothing struck after them, which nothing downstream can do, and sending it as the sustain it is not would hold the whole keyboard.

The whammy bar and what the right hand did — tapping, slapping, popping — belong to the beat rather than to one note, because the hand does one thing at a time; they are put on each of the beat's notes on the way out, since a note is what travels. A bar movement becomes the bend of any note in the beat that has none of its own.

They are declarations and not renderings. The note says it is hammered on; what that sounds like is decided further down, so two renderers of the same note agree and either can be varied. A hammer-on and a pull-off are the same mark in the file — which one it is depends on whether the hand went up the string or down, so the timeline works it out from the note before it on that string.

The instrument itself travels as state beside the harmony: the program, the tuning, the capo, whether it is a drum kit, and the name to put on a panel. State rather than events, because a module that starts listening between two notes has to know what it is listening to at once.

## Playing it like a guitarist

How an imported part is performed — per-string channels, bend curves, legato, strum spread, palm mute, humanisation, and a generative guitarist steered by style profiles taken from real transcriptions — is specified in [guitar-player.md](guitar-player.md), to be built after this module.

## Variations come afterwards

Varying what was imported — substituting a bar, thinning or thickening a part, moving a figure, keeping the phrasing and changing the notes — is the next phase and wants designing on its own. It is a different problem from generating, and an easier one: the material is already musical, and the question is only how far from it to move.

## What is yours and what is ours

An imported file is a transcription of someone's song. Importing one for your own listening is one thing; shipping any of it inside the plugin is another, and nothing imported is ever included with it.
