# mpxChart

A chord chart, read and played. It loads an iReal Pro playlist, shows one song as a lead sheet in a window of its own, and publishes the harmony onto an MPX cable and onto four ordinary outputs for a rack that knows nothing of MPX.

## Two views of one chart

A chart is held twice, from one source, and the difference is the whole design.

**As written** — `chartLayout` in `ChartLayout.hpp` — turns the parser's cells into bars carrying their decorations: section letters, repeat marks, first and second endings, measure-repeat marks, meter changes, segno, coda, Fine, D.C. and D.S. jumps, and repeat counts. This is what is drawn. The marks *are* the structure, and a chart with them written out is three times longer and says nothing about the form.

**As played** — `chartPlayback` — walks those same bars into the order they sound: repeats taken the right number of times, the right ending on each pass, and the navigation obeyed. Each entry names the written bar it came from, so a bar played three times appears three times and the cursor can light the right measure on the second pass.

One engine drives both. Two walks would drift apart at the first unusual chart and the cursor would light a bar the ear was not hearing, which is worse than no cursor.

Both were ported from GXW's `harmonyChartLayout.js` and `harmonyNavigation.js`, and both were checked by running GXW's own code and the C++ over the same four playlists and comparing: **2137 charts agree exactly** on bar count, row count, section ranges, and the full played order bar by bar. That comparison found four real bugs — a section marker taken as `*` followed by anything, an unmatched `<` swallowing the rest of a chart, `*624x` read as a repeat count of 624, and "Vamp and solo till cue" read as a D.S. because "and solo" contains a d followed by an s.

`make charttest` runs the census; `ARGS="Some Title"` prints charts as text; `--play`, `--tsv`, `--sections` and `--cells=Title` are the other modes.

## The window

A widget in Rack's scene, not an operating-system window. Modeless: every control on every module keeps working while it is open.

Four bars to a line, always, so the barlines run straight down the page. A section always starts a fresh line. Alternative endings stack and are column-aligned — the second ending is padded to land under the first.

Everything scales from one number: a bar is a quarter of the window's width, and the chord size, the marks, the row height and the margins are fractions of that. So the window is one control — drag it wider and the whole chart grows. It scrolls vertically and the scrollbar appears only when the chart is taller than the window.

The head line does not scroll. It carries the transport and the tune's name, and the music is clipped below it.

Set in Petaluma and PetalumaScript, shipped in `res/` under the Open Font Licence. Letters and numerals come from the text face and the symbols — the major-seventh triangle, the diminished circle, flats and sharps, segno, coda, the measure-repeat marks — from the music face. Music glyphs are set at 0.62 of the text size and dropped by a third of their height, because SMuFL glyphs are cut against a four-space staff rather than a text baseline.

Barlines, repeat dots, ending brackets and the section rule are drawn as geometry, because they must scale with the layout and line up with the grid, which a glyph will not do.

### What can be clicked

**A section letter** chooses that section; clicking it again puts the whole chart back. Only the letter is a hit target.

**Any measure** moves the play head there — to the next occurrence at or after where the music is now, so pointing at a bar ahead of the cursor goes forward to it.

**The transport**, top left, drives the module's own parameters so the two pairs cannot disagree.

Escape closes the window from anywhere. Rack sends a key to the selected widget or to whatever the pointer is over, and neither is this window when somebody glances at the chart and reaches for Escape — so the window asks the keyboard itself once a frame, and stands down while a menu or a text field has it.

### Two marks, two meanings

The measure being played is filled with a translucent block. The chosen section is an orange rule across the top of every bar of every occurrence of that letter, turned down at the true ends of each run. One says what is being played and the other says what will be played, and they are on screen at the same time.

## Sections

A section runs from its letter to the next structural boundary — another letter, or a fresh repeat block opened after this section's own repeat has closed, which is how a tag or a repeat-and-fade coda is written — with trailing blanks trimmed.

Choosing a label takes **every occurrence** of it. An AABA chart with A chosen plays all three A's in the order they come, which is what a musician means by "just the A section".

The form becomes shorter rather than gapped: the kept bars are renumbered so they run without a break, and that is the loop.

Of 6367 sections across the four playlists, 34 are never reached by the walk through the whole form — a coda, a tag, or a section written after a D.C. al Fine. Choosing one of those plays its bars as written, once through. Asking for the coda means play me the coda, not play me nothing.

## The module

Ten HP. The chart is read in the window, so the face carries what you glance at while patching.

A readout: the song title over two lines, the key and meter, which section is playing, the measure and which pass, and the chord sounding now. **The title and the key line are the pickers** — the title opens import, playlists and songs; the key line opens the twelve keys named as a musician names them. Both light faintly under the pointer.

The chord shown is the one the player resolved, not the one written in the bar, so a measure holding a repeat mark still shows what you are hearing.

LETTER and ROMAN choose how chords are written, in the chart and in the readout alike. It is a parameter, so it saves with the patch, appears in Rack's menu and can be mapped.

CHART opens the window, and closes it again. The window is a child of the scene and therefore always over the rack, so bringing an open one to the front achieves nothing you can see — while a button labelled CHART that shuts the chart is what anybody would try. A second chart module asking for the window is the exception: that is not "close it", it is "show me mine instead". TEMPO is the large knob with the tempo in force written above it in green — the clock's measured rate when one is patched, the knob's otherwise, and correct while stopped.

Play and rewind are drawn as a transport: a triangle that becomes two bars while running, and a pair of arrows back. The same pair appears in the window.

### What touches the transport

Rewind stops it. Loading a chart stops it and winds it back. Opening or closing the window does neither — looking at the music while it plays is the commonest reason to open it. The reset **jack** only winds back, since it is a sync signal from the patch and a clock that silenced the music would be useless.

Play is a transport, not a mute: nothing advances while it is off, but the harmony keeps being published, so anything downstream still knows the chord you stopped on.

## Outputs

| Port | Carries |
| --- | --- |
| mpxOut | The harmony block on the MPX cable |
| chord | The chord's tones as polyphonic V/Oct, voiced close from the root |
| root | The root alone |
| PES chord | The chord of the moment, twelve channels, 10 V on its root |
| PES scale | The key, twelve channels, 10 V on the tonic |

The last two are Aria Salvatrice's Poly External Scale format: one channel per semitone from C, 0 V out, 8 V in, 10 V on the tonic. A quantizer reading one follows the changes and reading the other stays in the key.

Rack has no way to carry a chord **as a chord** — nothing transmits "D minor seven, two beats left, the five chord next". That is what the MPX harmony block is, and no prior art for it was found.

All five come from the same resolved chord on the same beat, so they cannot disagree.

## The layout, and displays

The panel is data, in `chartLayout()`. Positions in millimetres; knobs, jacks and lights by their centre, lamp lists and displays by their top-left corner.

A **display** is a widget the module makes itself. The layout owns where it goes and how big it is; the module hands the widget over with `layoutPlaceDisplay`, and from then on the editor can drag it and the saved file remembers it. mpxChart has two: `d.readout` and `d.bpm`.

The other modules place their displays in code and are unaffected. Giving each of them a display item is about four lines apiece and has not been done.

## Still to do

- The panel editor can move a display but not resize one.
- The four ordinary outputs have never been tested against a quantizer.
- MPX has no manual. The clock input's own name carries the one fact that used to clutter the tempo readout.
- Nothing in MPX is pushed to GitHub, and the repository has no CI, so no build is downloadable.
- Unbuilt, and described in [ideas.md](ideas.md): mpxMelody, mpxPhrase, the field module that reads an image, and the three-stage processor order — swing, then ornament, then pitch bend.
