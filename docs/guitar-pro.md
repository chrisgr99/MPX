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

Over each window — a bar, or the span between changes where the file marks them — the pitches sounding in the accompaniment tracks are collected and weighted by how long they sound and where they fall in the bar. That is matched against chord templates, and the reading that keeps the root moving smoothly is preferred over judging each window on its own. The melody track is left out of the vote, or passing notes drag the answer about. Where the file does carry chord symbols, they are used and nothing is inferred.

What it is for is the modules that vary an imported part: re-voicing a chord, substituting a figure or moving a line all need to know what chord is sounding. It also gives the module a chart to draw, when the notation view is built.

What it is not for is feeding mpxComp, mpxMelody or mpxPhrase. Those were built to invent a part from a chart and a style, and a song gives them far more than they can take — the rhythm somebody played, the voicings, the dynamics, the articulations. Handing them a chord grid throws all of that away, and they would generate a worse part than the one already in the file. If they grow to read the richer material later, that is a change to them.

So the harmony is real and it goes out on the cable, but the song is not a way of driving the existing players.

## What the module does

`mpxGuitarChart` reads a file and plays it.

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

## What it does not read

A file using something the reader does not know is imported as far as it goes, and what was skipped is written to Rack's log. Nothing is said on the panel: a song that plays is useful even where a bend or an unusual tuplet did not survive, and a panel full of warnings about things the listener cannot hear is worth less than the music.

One known gap. A Guitar Pro 6 drum track gives no pitch for its notes — which piece of the kit is struck is a position on the drum staff, read through the instrument's own list of elements, and that list is not read yet, so those notes come out unpitched and counted.

## Seeing the part

A later phase draws one chosen track as written music, as mpxChart draws a chart, so the part can be followed while it plays. The file carries everything that needs — the rhythms, the fret and string of each note, the articulations — so nothing has to be kept for it now.

## What goes out on the cable

The notes, with everything the file said about how they are played: the string and the fret, and flags for hammer-ons and pull-offs, legato and shift slides, slides into and out of a note, let ring, palm mute, dead notes, ghost notes, accents, harmonics, tapping, staccato and tremolo picking; the vibrato; the grace notes; which way a chord was brushed and where in the stroke each note falls; and the bend as up to four points of offset and cents.

The whammy bar and what the right hand did — tapping, slapping, popping — belong to the beat rather than to one note, because the hand does one thing at a time; they are put on each of the beat's notes on the way out, since a note is what travels. A bar movement becomes the bend of any note in the beat that has none of its own.

They are declarations and not renderings. The note says it is hammered on; what that sounds like is decided further down, so two renderers of the same note agree and either can be varied. A hammer-on and a pull-off are the same mark in the file — which one it is depends on whether the hand went up the string or down, so the timeline works it out from the note before it on that string.

The instrument itself travels as state beside the harmony: the program, the tuning, the capo, whether it is a drum kit, and the name to put on a panel. State rather than events, because a module that starts listening between two notes has to know what it is listening to at once.

## Playing it like a guitarist

How an imported part is performed — per-string channels, bend curves, legato, strum spread, palm mute, humanisation, and a generative guitarist steered by style profiles taken from real transcriptions — is specified in [guitar-player-spec.md](guitar-player-spec.md), to be built after this module.

## Variations come afterwards

Varying what was imported — substituting a bar, thinning or thickening a part, moving a figure, keeping the phrasing and changing the notes — is the next phase and wants designing on its own. It is a different problem from generating, and an easier one: the material is already musical, and the question is only how far from it to move.

## What is yours and what is ours

An imported file is a transcription of someone's song. Importing one for your own listening is one thing; shipping any of it inside the plugin is another, and nothing imported is ever included with it.
