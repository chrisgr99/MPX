# Showing the score

A Guitar Pro file carries everything a player reads: the notes, the metre, the dynamics and hairpins, the slurs and articulations, the section names, the repeats and the jumps. Playing it without showing it means you can hear where you are and not see it — and seeing it is what lets you play along, and what lets you take an expression off the page and try to make a synthesiser do it.

So: one track at a time, engraved properly, in a window of its own as mpxChart's chart has, with a cursor that follows the playing and a band marking a loop.

## Engraved, not approximated

Two cheaper things were considered and are not enough. A grid of bars with chord symbols — what mpxChart draws — says where you are but nothing about what the music does. A strip of marks standing for notes, pitch as height, is too diffuse to read.

What is wanted is the notation, which means an engraver. Writing one is months of work — beams, accidentals, collisions, slurs, tuplets — so the engraver is [Verovio](https://github.com/rism-digital/verovio), which is C++, LGPL-3, has no interface dependencies, and reads MusicXML.

## The route in

Verovio reads MusicXML and knows nothing of Guitar Pro, so the song model is converted to MusicXML in memory and handed over. One track at a time, because a score of seventeen parts puts three bars on a page.

That conversion is also what a MuseScore import would need later, in reverse — the engraver would then read the file directly. That is a separate job and not this one.

## What was measured

Verovio 6.3.0, built on a Mac with the parsers we do not need turned off — Humdrum, ABC, Plaine and Easie, GABC and DARMS. Editor support cannot be turned off: the build fails without it in this version.

- The static library is 17.2 MB and takes ninety seconds to build. Linked and stripped it adds about 6.3 MB to the plugin.
- A 27-bar guitar part loads and lays out in two hundredths of a second, and a page draws in ten milliseconds.
- Seventeen parts of Layla, 8.5 MB of MusicXML, lay out in 1.3 seconds across 64 pages, each page still drawing in twelve milliseconds.

So the layout belongs on a worker thread and the drawing does not. Pages are cached once drawn; only the cursor and the loop band are redrawn as it plays.

**The resources are 6.3 MB.** Verovio needs its glyph data at run time, and it needs Bravura whichever font is chosen — it measures glyph bounding boxes against it — so the smallest usable set is Bravura, Leipzig and the text metrics, about eighteen hundred small files.

## Drawing it

**Not through its SVG.** A page comes out as glyph outlines in a `defs` block referenced by `use`, with lettering as `text`. Rack's own renderer supports neither, so this route needs a full SVG library and would still land us with a bitmap to scale.

**Through a drawing backend instead.** `DeviceContext` is an abstract base explicitly meant for subclassing — the SVG writer is one such subclass — with forty-seven methods to implement. About twenty do real work: lines, polylines, polygons, rectangles, ellipses, arcs, quadratic and cubic béziers, text, rotated text and music text, and the start and end of each graphic. The rest are settings that can be left empty.

The music glyphs are the part that could have been hard and is not: each is a single SVG path in its own file, so the backend parses a glyph once, caches it as drawing commands, and fills it wherever it is asked for. Lettering is drawn with the host's own text.

**And the graphic markers are what makes it navigable.** Verovio brackets every element it draws with a start and an end carrying that element's identifier, so the backend can record the box each bar occupied as it is drawn. That is what the cursor, clicking a bar, and tinting a loop across several systems all need.

## How large the notation comes out

**A page width does not mean the same thing for every score.** A MusicXML file may carry its own scaling — so many millimetres to so many tenths — and its own page size, and Verovio honours both. So one width holds two bars of a score that declares nothing and eighteen of one that declares a small staff. The Nocturne in E flat came out as two systems of eighteen bars, which on a screen is a scribble.

**So the layout is measured and the width adjusted** until a system holds between two and five bars, which reads well at the sizes this window offers. Verovio is asked for one system a page, so counting pages counts systems. Up to six passes, each a tenth of a second, once, when a score is opened. The width then decides the drawn size: `pageScale` is the pixels asked for divided by that width, so a narrower fitted page draws larger notation.

**Count the bars of the document that was laid out, not of the file.** A score of several parts carries a full set of bar elements per part — the Schumann quartet has 132 bars and 528 bar elements — while what reaches Verovio is one part cut out of it. Counting the file counted every bar once per instrument, so the fit set out to cure a system holding four times too many bars and drove the width down until a system held one. A quartet opened at four times the size it should be, with a single bar overflowing the window.

`Score::load` records the count as it hands the document over, which is the only place that has the document actually being laid out. That also covers a Guitar Pro song written out as MusicXML, where the file on disk is not MusicXML at all.

A single-part score counts the same either way, which is why this went unseen. Two of the piano scores in regular use are engraved as two parts rather than as two staves of one part, so they had been drawing at twice their intended size.

## The window

**It rides with its module.** The window is a child of the scene and so lives in screen coordinates, which meant it stayed put while the rack scrolled out from under it: scroll away from the module and the window was still sitting there, belonging to nothing on screen. What is kept now is its offset from the module rather than its place on the screen, and it is put back beside it every frame. Dragging it, or resizing it by a left or top edge, sets a new offset. The offset is in screen pixels at the current zoom, so zooming moves the rack under a window that stays the size it was.

**The transport is repeated in it**, at the left of the title strip in front of the piece's name, so the line reads as "play this". Reading the score is exactly when you want to start it, stop it and take it back to the top, and the module may be anywhere on the rack — possibly behind the window. The play button shows what pressing it will do: a triangle when stopped, a pair of bars when running.

The window asks and the module's own button moves, through `ScoreFollow::wantTransport`. It is a request and not a state, like the seek beside it: the transport belongs to the module, and two places each holding their own idea of whether the music is running is a pair that can disagree. The chart window does the same thing for the same reason.

The tools on the right are things done to the *view* — its size, its page, which instrument, whether it follows — and they stay there. The transport is about the music, so it sits with it.

## Where the cursor comes from

From our own timeline, not from Verovio's timemap. The timemap exists and gives every event a time in milliseconds — but we already know, exactly, which written bar is playing and which pass through it we are on, and that is what drives the notes. Asking Verovio when things happen would put a second clock beside the one that is already right. It is asked only where things are on the page.

## The toolchain can build it

Asked, rather than assumed. The container that cross-compiles Windows and Linux states no compiler version anywhere in its own sources, so a probe was pushed and run: both cross compilers are **GCC 13.1.0**, built by crosstool-NG — `x86_64-w64-mingw32-g++` for Windows and `x86_64-ubuntu16.04-linux-gnu-g++` for Linux, the latter being the one that targets an old glibc so the plugin runs on old distributions. Both compiled a C++20 probe and three of Verovio's own sources, including its MusicXML importer.

The container also carries Ubuntu's own GCC 11.4, which is NOT what builds Linux plugins. A probe that finds that one first proves nothing, which is how the first attempt at this went.

## The order the work goes in

**The display is built first, and fed properly engraved MusicXML.** Not a MuseScore module — just the window, the backend and the paging, with real scores converted by MuseScore's own command-line converter, which costs nothing and needs no interface. The reason is testing: when a page looks wrong, the input is known good, so the fault is in the renderer. Fed our own converter's output first, every fault would have two possible homes and the time would go on deciding which.

**Then the Guitar Pro converter**, from the song model into MusicXML, onto a display already trusted.

**Then, if it is still wanted, a module that plays a MusicXML score.** By that point it is the smallest of the three: a reader and a panel, with everything else built.

Each step is useful on its own and none is wasted by the next.

## The phases

Each ends with something to look at or hear, and the work stops there to be judged. The days are working days and rough.

**1. The dependencies build themselves.** DONE. `make dep` fetches and builds FluidSynth and Verovio into `dep`, in Rack's own idiom, so a clean checkout works — the library's build machine runs `make cleandep`, `make dep` and `make dist`, and hands us its cross compilers. Two things came out of it: debug information had to be turned off for both, since Verovio's archive was 492 megabytes with it and 17 without; and Verovio reads its glyphs with std::filesystem, which Apple marks unavailable before macOS 10.15, so the Mac floor for the whole plugin rises from 10.9 to 10.15.

**2. A page on the screen.** DONE. Verovio linked in, a NanoVG backend under it, and a window on a real score: dark page, paging, a notation-size control, the wheel running through the whole score and turning the page at each end. The one thing that nearly sank it was a factor of ten — a page is laid out in tenths of a unit, so a glyph is scaled by the point size over the em AND by that tenth, while an advance is not. Without it every glyph is a speck, and a page of specks looks enough like a page of music to be argued about rather than measured.

**3. The backend finished.** DONE, and it turned out to be a measuring job rather than a writing one. Every drawing call Verovio can make is answered except embedded artwork, which no real score asked for. Rotation is the one that mattered: Verovio draws an arpeggio sign and a wavy glissando by turning a horizontal glyph on its side, and in guitar music that is often — six of sixteen scores. Also found by measuring: the glyph files are fragments with no `svg` element, so nanosvg normalises each to its own bounding box unless it is wrapped first; `tuning-glyphnames.json` has to ship or every microtonal accidental is dropped with a complaint; and a bar box is measured in the ADVANCE scale, turned over, and from the glyph's own origin rather than hung off the baseline.

**4. The window as something to use.** Paging, the notation-size control and the bar boxes are all done, so what is left is the part chooser — a score of seventeen staves puts three bars on a page and is unreadable until it is one part at a time. One or two days.

**5. Guitar Pro on the page.** The converter from the song model into MusicXML: durations, tuplets, ties, voices, metre and key changes, repeats, endings, jumps, and the articulations in their notation forms. Four or five days, and the place where things will be missing rather than wrong.

**6. Following and navigating.** The cursor from our own timeline, pages turning with the playing, clicking a bar to move there, dragging a loop, clicking a section to loop it, the tinted band across systems and pages. Three days.

**7. Shipping it.** Windows and Linux, the glyph resources packaged, size checked, help written. Two days.

The likeliest to run over is five: articulations are a long tail.

## How the drawing is checked

The window cannot be looked at from inside the work, and a drawing fault that only shows on the screen is a fault nobody can work on. So `make scorerender` draws a page with the real renderer — real NanoVG, real OpenGL — into a hidden window and writes it out as an image. `ARGS="file.musicxml 0"` draws a single glyph the size of the picture, filled beside outlined, for when a glyph itself is in question.

`make scorerender ARGS="sweep *.musicxml"` is the one that finds things. A glyph the font does not have, a glyph whose file will not parse, and a drawing call that is not written all produce the same thing on a page — NOTHING — so the backend counts what it was asked for and what it could not do, and the sweep draws every page of every score at five notation sizes and totals it. Sixteen songs converted from Guitar Pro currently give 4,729 pages and 700,683 glyphs in under a minute, with nothing undrawn.

It checks the bar boxes in the same pass, which is what matters for phase 6: a box that is empty, inside out, or nowhere near the page is one a cursor would be drawn in the wrong place from, and none of that shows on the page itself. Two things that look like faults and are not, both confirmed against rendered pages rather than assumed. A box reaching left of the page belongs to the first measure of a system, which carries the brace and the clefs in front of it. And a box below the bottom of the page is a score with more parts than that page size can hold — Verovio draws it anyway, so the window scrolls to where the ink stopped rather than to the page's own height, and none of that music is unreachable.

The strongest single check is that the bar count does not move: the same score laid out at five sizes records exactly the same number of boxes, and that number is the measure count of one part.

## A score exported from a performance

Some files write the rubato down. The Nocturne in E flat here carries **512 visible metronome marks across 36 bars** — fourteen to a bar, each with its own beats per minute, because whoever made it recorded a player and wrote every fluctuation into the score as a direction.

Verovio draws all of them, quite correctly, and the page becomes columns of bracketed numbers with the music somewhere underneath. Nothing about the layout is at fault, which is why making the page narrower, taller, or one system at a time changed nothing: the page was full of tempo marks.

So a flood of them is taken out before the score is engraved. An engraver writes a tempo at the top and at each change of pace, which is far fewer than one a bar; more than two a bar is a recording rather than a marking, and then every one goes, since keeping an arbitrary few of a player's wavering says nothing. Of the scores here only that one is affected — the next most marked has a single tempo in 988 bars.

**The music is unaffected.** The tempo it plays at comes from the reader, which takes these marks from the file itself and never from the engraving, so the rubato is still played. It is only not drawn.

## What submitting it would still need

The compiler question was the one that could have ended this. It is not the only one.

The build has to work from a clean checkout with no `dep` folder, because the library builds every plugin from source in that container. Both FluidSynth and Verovio are built here by hand at the moment and are not in the repository, so the Makefile has to fetch and build them itself. That is the real work this leaves behind, and it is the normal arrangement for a library plugin.

The licences have to travel with it: the plugin is GPL-3, Verovio LGPL-3 and FluidSynth LGPL-2.1, and statically linking an LGPL library into a GPL-3 work is permitted, with their texts and source links included.

And the size. About six megabytes of code and six of glyph data, on top of FluidSynth's one. Nothing forbids it, but the SoundFont — thirty-two megabytes — belongs as a download on first use, as mpxPiano's samples already are.
