# Chord inference

Working out the chords of a Guitar Pro song, so that mpxGuitarChart can put them on its MPX cable as mpxChart puts a chart's: the key, the current chord and the next two, each with its bass, and the beats until the change. A module that plays from chords — mpxFingerPicker first — can then play over any song, not only a chart.

Status: designed, not built.

## Two sources

**The chords the file names.** Guitar Pro lets a transcriber write chord names over the music, and the reader already keeps them, each with the bar and the beat it applies from (GpChordMark in GuitarPro.hpp). Where a file has them they are used as they are: parsed into a degree, an accidental, a quality and a bass in the song's key, and nothing is inferred. Of 77 files on hand, 14 have them. More can be downloaded, including more with chord names.

**The chords worked out from the notes**, for every file without names.

## Working them out

1. **The tracks that carry the harmony.** Rhythm guitars, bass and keyboards vote; drum tracks do not; a track that is mostly one note at a time — a melody, a vocal line, a lead guitar — votes with a small weight, since it is full of passing notes. A track's weight comes from what it plays: the share of its time spent sounding two or more notes at once, and its register.
2. **The pitches sounding in each window.** A window is half a bar to start. Each note adds its pitch class with a weight for how long it sounds in the window, more on a strong beat than a weak one. The lowest note sounding on the window's strong beats is kept apart: it is the most likely root, or the bass of a slash chord.
3. **Matching.** The window's weighted pitch classes are compared with every chord the cable can carry — each root, each quality, major, minor, dominant, major and minor seventh, diminished, half-diminished, augmented, suspended, sixth — scored by how much of the window's weight the chord's tones take and how many of the chord's tones are missing. A chord whose root is the window's bass scores higher; a chord whose root is not, but whose tones include the bass, becomes a slash chord when that fits better.
4. **Smoothing over time.** Chords hold for a while and change on bar lines and half bars. The best path through the windows is found as a whole (a Viterbi search), with a cost for every change and a smaller cost for a change on a bar line, so that one passing note cannot make the chord flicker and a change that is really there is not missed.
5. **The key.** From the whole song's pitch classes, weighted the same way, against the major and minor key profiles; the chords are then written as degrees in that key, as mpxChart's are. A song that changes key is a later refinement.

What comes out is the chords at their bars and beats, the same as the chords a file names, and from there one path: the cable's current chord, next two, bass and beats to the change, computed once when the song is loaded and played back with it.

## Testing against the files that name their chords

The files with chord names are the measure. Each is run through the inference with its names hidden, and the result compared window by window with what the transcriber wrote: the share of the song's time where the root agrees, where root and quality agree, and where the bass agrees. A tool prints those figures for every file, and the matching and smoothing are tuned on them. Downloaded files with chord names add to the measure as they arrive; some are kept back and never tuned on, so the final figure is an honest one.

## On the cable

mpxGuitarChart publishes the harmony on every row's cable, as it does now with no chords in it. With this it carries chords: from the file's names where there are any, inferred otherwise. A row carrying a track the inference found to be harmonic and a row carrying the melody get the same harmony, the song's.

## Build phases

1. **The chords the file names**, parsed and put on the cable, with the bass, so mpxFingerPicker can play over the 14 songs that have them.
2. **The inference and its measuring tool**: tracks, windows, matching, smoothing and the key, tuned against the named files.
3. **Fitting it to mpxGuitarChart**: computed at load, shown on the panel in place of the track name where it helps, and on the cable.

## Still to come

- **Key changes** within a song.
- **Windows that follow the music**: a quarter-bar window where the harmony moves quickly, a whole bar where it does not.
