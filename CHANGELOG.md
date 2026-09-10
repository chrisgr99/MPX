# Changelog

Newest first.

## 2.0.0 — 10 September 2026

First release, as a pre-release. The modules work; the protocol is still a proposal, and the part
that would let other plugins speak it has not been written yet.

### Added
- **mpxChart** — reads iReal Pro playlists and plays their charts onto an MPX cable. A library of chord progressions is included.
- **mpxArp** — an arpeggiator on the chord from the cable, with a length that can be pushed into overlap.
- **mpxRand** — varies one attribute of the notes on a cable, by white noise, a random walk or Perlin noise.
- **mpxComp** — chordal accompaniment: voices a chord and leads the voices between changes.
- **mpxEuclid** — four Euclidean voices on one cable, each with its own steps, pulses and offset.
- **mpxProgression** — a dozen chord progressions everybody knows.
- **mpxMonitor** — shows what is on a cable and passes it on unchanged.
- **toMPX** — gathers ordinary control voltages into a cable.
- **fromMPX** — takes a cable apart into ordinary polyphonic control voltages.
- **polyToStereo** — a channel strip per voice: two level stages multiplied, a pan, and a stereo pair. Not an MPX module; any polyphonic patch can use it.
- A master random seed and a pass counter on the cable, so everything downstream that makes a random choice starts from the same number and a take can be reproduced.
