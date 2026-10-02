# Guitar model

A module that plays one MPX part, usually a Guitar Chart row, on a physically modelled guitar. Nothing is patched between it and the mixer: the strings, the playing and the body are all inside it.

Name: mpxGuitar.

Status: built. Help in help/mpxGuitar.json, from the //? lines beside each control; a demonstration patch, mpxGuitar demo.vcv, plays Stairway to Heaven with the fingerpicked part on a nylon mpxGuitar, the electric part on an electric one, and the rest of the band through mpxGuitarChart6.

## Purpose

mpxGuitarVoice plays a part through an oscillator and a filter, which can be any sound but is never a string. This module models the strings themselves, so the things a guitarist does — picking at a point on the string, bending it, hammering on, muting it with the palm, touching a harmonic — act on the sound the way they do on an instrument, rather than being imitated by envelopes and a filter.

## The strings

Each string is a digital waveguide, the extended form of the Karplus-Strong algorithm: a delay line one period of the note long, closed in a loop through a filter that takes away a little energy on every pass, more of it at high frequencies. That is how a string loses its brightness as it rings.

- **Pitch** is the loop's length. The delay line is read between samples by four-point Lagrange interpolation, so every note is in tune and the length moves as smoothly as the pitch: bends, slides and vibrato are a moving loop length with no clicks. A first-order allpass was used first; it is exact for a held note but has a memory, and each time a slide carried the delay across a whole sample its coefficient jumped and the string clicked. The read is solved for the period by measuring every filter's delay at the fundamental, the interpolator's included.
- **Stiffness** is four first-order allpasses whose delay falls with frequency, so the upper partials come round the loop sooner and sit sharp, as on steel strings. A fixed filter sharpened a high string's partials hundreds of cents for every few on a low one, so the coefficient is worked out for each note: at full stiffness the eighth partial is six cents sharp on any string. The strings use 0.7 of that.
- **Decay** is set as the time the fundamental takes to fall by 60 dB, and the loop's gain is worked out from it for each note. How long that is depends on the note, as on a guitar, where the bass strings ring for many seconds and the top string at the twelfth fret for two or three, and the open strings ring clearer and longer than the stopped ones, a fingertip stopping a string more softly than a fret or the nut. The Sustain knob sets the open low E's time. The Taper knob shortens each octave above it, to 0.44 of the octave below at its top; the Fret damping knob shortens a fretted note, to half the open string's time at its top, and damps its upper partials as if damping were 0.3 higher. A note whose fret is not known is taken as open; a harmonic rings as the open string does. A free string, ringing in sympathy, rings as its open note.
- **The loss filter** is a one-pole lowpass whose pole sets how much faster the upper partials die than the fundamental. The plain Karplus-Strong two-point average was tried first and barely touched the partials below a few kilohertz.
- **A pluck** is the string's own shape when it is let go, added into the loop over one period: a triangle with its apex where the string was pulled aside. Its partials fall away as the square of their number, so most of its energy is in the low ones, and it has no partials with a node under the pick, which is the difference between picking at the bridge and over the neck. A burst of filtered noise was used first; it put about as much into the upper partials as the lower, and was most of why the guitar sounded tinny. The pick's hardness rounds the apex with a one-pole low-pass run round the period, from about 200 Hz for a fingertip to nearly open for a plectrum, and a hard pick adds a little noise, its click. The level is the note's dynamic. The pick begins where its triangle crosses nought, rising: begun at its corner, the triangle with its mean taken away started at about half its height, and every note began with a step from silence, a broad, clicky thump with every frequency in it and the body's middle resonances ringing with it. From the crossing the largest jump at the start is a fingertip's own slope, under a tenth of its height where it was over two thirds. A string struck while it rings is caught first, as a pick or fingertip catches it: for the one period the new pluck takes to go in, the loop's old vibration is cut to a quarter, so the new note sounds at its own level. Added to the old vibration as it was, the new pluck met it at whatever point of its cycle it happened to be and could land against it: a C restruck a quarter of a second after it was first picked came out up to 5 dB weaker in its first 30 ms and 7 dB weaker over the next 100, which sounded like the note cut short with nothing after it. Caught, a restrike is within a decibel of a fresh pluck whenever it comes. A hammer-on's knock does not catch the string, which rings on.
- **A DC blocker** on the output removes any offset the noise leaves in the loop.

## The hands

GuitarModel.hpp turns each thing the performer decides into what a hand does to a string.

- **A pick** is a pluck, at the pick position and hardness the panel sets.
- **A hammer-on, a pull-off or a tap** is a change of the loop's length with only a soft knock put in, so the string rings on at its new pitch.
- **A palm mute** is the side of the hand on the strings, the light mute of folk and Travis picking: a sustain of about 0.7 s, the upper partials going first, and a slightly softer pick, the attack within a decibel of a plain note's. The performer lets a palm-muted note last up to 600 ms and strikes it at 0.95 of the level. At a third of a second's sustain, a performer cutting it off at 150 ms and 0.85 of the level, a palm-muted note under a ringing one of the same pitch came out 6 dB down at once and 20 dB down a tenth of a second later: a string killed, not muted.
- **A dead note** is a string held against the frets: a sustain of a few hundredths of a second, leaving little but the pick.
- **A harmonic** sounds the partial with a node under the finger, above the open string rather than the fret: two at the twelfth fret, three at the seventh and the nineteenth, four at the fifth. The string's open pitch comes from the instrument's tuning and capo on the cable. An artificial harmonic sounds an octave above the stopped note.
- **A note ending** is the hand coming down: the string falls 60 dB in a tenth of a second, its top first. Let-ring notes end later because the performer has already moved their end.
- **A level change while a note sounds**, a tremolo's pulse or a slide's fade, scales the string's output relative to how hard it was struck.

The performer's messages carry each note's technique bits and fret with its strike, and with the level change that marks a note the left hand sounds without one.

## Sympathy and the finger

- **Sympathetic strings.** A string is free once its note has ended for half a second, or if it has not been played: tuned to its open pitch, undamped, and driven by a little of what the strings being played are doing at the bridge. Only free strings hear the bridge, so no two strings feed each other round a loop that could grow. An A played on the low E's fifth fret sets the open A string ringing about 24 dB below it.
- **Finger noise.** A band of noise near 2.8 kHz rides on a wound string — the lower half of the instrument — while its pitch moves under a sliding finger: during a note that slides in or out, and on the way to a note reached by a legato or shift slide. Its level follows the finger's speed. A bend moves the pitch without it.

Each has a small knob on the main panel, since how much a guitar's strings ring in sympathy and how much a player's fingers squeak both vary from one guitar and one player to another.

## The body

The strings alone are thin. Three guitars are chosen between on the panel: the steel-string acoustic, the electric, and the nylon-strung classical. The choice is the player's; the instrument a part names does not set it.

The nylon's strings are a sixth as stiff as steel, lose their upper partials much sooner (0.45 more damping), and are played with a softer touch (0.6 of the hardness), and its body radiates less of the top.

For the two acoustics, a set of resonant filters tuned to a guitar body's main resonances shapes the strings, and a radiation low-pass after them rolls away the highs a body hardly radiates: from 4.5 kHz for the steel-string, 2.8 kHz for the nylon. Without it three quarters of the full body was the strings passing straight through, so turning the body up warmed the sound without making it less bright.

For the electric, each string is heard by a magnetic pickup at a point along it: the string less itself a fraction of a period earlier, a comb whose notches fall on the partials with a node under the pickup, then the coil's resonance near 3.2 kHz with a Q of 2.5, the glassy edge of a single-coil pickup. The body knob sets the pickup's place, from 6% of the string from the bridge to 25%, the neck; the bridge pickup measures about 9 dB thinner and brighter, its third to twelfth partials against its first two. The pickups' signal is taken down to 0.18 of the strings' level before the tone knob and the amplifier: at the strings' own level a note reached the amplifier near 0.9 and a chord near 2.5, and the electric was driven hard at every setting of the drive knob, the amplifier putting back what the tone knob took away. Then the guitar's tone knob, and an amplifier: tanh saturation, its drive divided back by the drive's square root, so a light touch comes through clean while a hard one or a high drive breaks up; then the tone stack, with its middle scooped under its bass and treble; then a speaker's roll-off below 70 Hz and above 5 kHz. The whole is trimmed to the acoustic's level, so changing guitar changes the sound rather than the volume.

The acoustic body is ten band-pass resonances fed by the strings at once: the air in the body near 102 Hz, the top plate's first mode near 195 Hz and the back's near 230 Hz, and seven more up to 2.4 kHz, each quieter and narrower than the one below. The body knob blends from the bare strings to the full body, which is part direct sound and part resonance, balanced so that a strummed chord is within a decibel of the bare strings: turning the body up changes the tone, not the volume. Each side of the stereo field has its own body, so a chord keeps its spread.

The strings may also be coupled loosely at the bridge, so that unplayed strings ring in sympathy.

## The performer

The performer library, the same one mpxGuitarist, mpxGuitarVoice and the built-in band use, decides each note's string, start, length, level, bends and slides. A physical model needs some things the performer now only implies — that a note is a palm mute, rather than only that it is darker — so its messages gain those facts in phase 2.

## Controls

### The main panel

Ten HP: the guitar, every control of it doing something in all three guitars.

THE CHARACTER KNOBS EXAGGERATE AT THE TOP. Brightness, Body on the acoustics, Sympathy, Finger noise, Stiffness, Taper and Fret damping reach half again beyond the most a real guitar does, so that turning one up makes plain what it does, and it can then be brought back to taste. Each starts at its realistic value, two thirds as far round the dial as that value's top. Pick, Tone, Drive, Bass, Middle and Treble have wider ranges for the same reason. Sustain is left as it is.

On large knobs:

- **Pick**: where the strings are picked, from 2% of their length from the bridge, glassy and thin, to 50%, the middle of the string, hollow and soft. Starts at 15%.
- **Brightness**: the pick's hardness, from a fingertip to a hard plectrum at two thirds of the way; above that the click as the pick leaves the string grows, to three times a plectrum's. Starts at 40%.
- **Sustain**: how long the open low E rings, from 0.5 s to 20 s to fall 60 dB; the other notes by Taper and Fret damping. Shorter is also duller, as old strings are: the loss filter's damping falls as the sustain rises.
- **Body**: on the two acoustics, how much of the body is heard, the full body at two thirds of the way and the resonances pushed past a real body's above it; on the electric, where the pickup is, from the bridge to the neck. Its name on the panel and in its tooltip reads PICKUP while the electric is chosen.

On small knobs, set once for a guitar:

- **Sympathy**: how much the free strings ring with the played ones. The bottom is none; a third of the way is an ordinary acoustic; the top three times that.
- **Finger noise**: how loud a finger sliding along a wound string is. The bottom is silent; the top half again as loud as at two thirds.
- **Stiffness**: how sharp the strings' upper partials sit, from none to the eighth partial nine cents sharp, past any real string; a steel string's six cents at two thirds. Starts at 47%.

In a second row, how the notes ring against one another:

- **Taper**: how much shorter high notes ring than low ones. At the bottom every note rings as long as Sustain says; at a third of the way each octave above the open low E rings 0.66 as long as the octave below, where it starts; at the top 0.29.
- **Fret damping**: how much a fretting finger shortens and dulls a note against the same string open. At the bottom alike; at a third of the way, where it starts, three quarters as long; at the top a quarter as long and much duller.

At the top, the MPX input on the left and on the right the three guitars, **Nylon**, **Steel** and **Electric**: one lamp group reading down, the panel library's radio control, each named on its left, the one playing lit. The setting's values follow that order, nylon nought, steel one, electric two; a patch saved under the earlier numbering is renumbered as it opens. Choosing the electric opens its fold-out; choosing either of the others closes it. The name of the instrument on the cable is below them. To come: an amplified option for the nylon and the steel-string, opening the same fold-out with its tone controls, as an acoustic-electric played through an amplifier is.

### The electric's fold-out

Four HP, one column, shown only while the electric is chosen: choosing it widens the module to the right, pushing the modules beside it along as Rack does when a module is dropped among others, and choosing either acoustic narrows it again, leaving them where they are. So no control on show is inert in the guitar that is playing.

- **Tone**: the guitar's tone knob, two one-pole low-passes before the amplifier, from open down to 300 Hz.
- **Drive**: how hard the strings push the amplifier, a drive of 1 to 18. Starts at 30%, a drive of about 2.4.
- **Bass**, **Middle** and **Treble**: the amplifier's tone stack, each from -18 dB to +18: a shelf below about 100 Hz, a band around 500 Hz, and a shelf above about 3 kHz. They start at +3, -6 and +4, the scooped middle and lifted ends of a clean amplifier. The three add together, so all three at +18 dB is about twenty-two times as loud as all three at nought.

## Inputs and outputs

- **MPX in**: one part.
- **Left and right out**: the guitar.
- **Level lamp**, between the two outputs: a green flash as each note is struck; orange when either output passes 7.07 V, 3 dB under clipping; red when it reaches 10 V, where Rack's audio interface, taking 10 V as full scale, clips. Red is held a second and orange 0.3 s, so a single peak is seen. Three colours from the panel library's level lamp, light.level, since green and red lit together make yellow rather than orange. Cable voltages are not clipped in Rack; the lamp says when the guitar's output would clip at an audio interface taking it as it is.

## Engine

The strings and the body are a library with no Rack in it, built into a command-line test as well as the plugin. The test measures what can be measured — each string's tuning, its decay time, where the pick puts the nodes, how the pick's hardness moves energy into the upper partials, that a bend lands in tune and nothing runs away — and writes audio files of the same notes to listen to.

## Tests

`make stringtest` measures one string: tuning, decay, damping, pick position, hardness, the start of a note, level, bends, slides, stiffness, restriking and stability, and writes audio files to build/. `make guitartest` measures the hands and the instrument: palm mutes, dead notes, harmonics, hammer-ons, note ends, sympathy, finger noise, nylon against steel, the body, the pickups, the amplifier, the tone knob, the taper — the open low E unchanged by it, the open high E falling much further in a second — and fret damping — D fretted on the A string falling further than the open D, and the two alike at nought — and writes the same phrase on each guitar to build/. `make leveltest` holds a note from mpxGuitar and the same note from mpxGuitarChart6's built-in band within a few decibels.

## Still to come

- **Amplified acoustics**: an option for the nylon and the steel-string that opens the electric's fold-out with its tone controls, as an acoustic-electric played through an amplifier is, without the electric pickup's coil.
- **Two planes of vibration**: each string vibrating in two directions that die away at slightly different rates and pitches, for the slowly changing ring of a real string.
