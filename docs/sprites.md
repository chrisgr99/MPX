# mpxSprites — notes from things moving over a picture

## What it is

A picture is loaded and sprites move over it. A colour field derived from the picture pushes them about, and a timer on each sprite reads the pixel underneath it and sends a note. The picture is on the panel and the sprites are seen moving across it, which is most of the point: the motion is watched as well as heard, and a path that looks interesting sounds interesting.

This is the mechanism from GeoSonix, brought over and quantised against the harmony on the cable.

## The picture

Any image is loaded from a file and resampled to 1024 by 1024, whatever it was. The field is then always the same square and nothing downstream depends on the picture's size or shape.

It is drawn on the panel, with the sprites over it.

## The field

Each pixel gives a force as a vector, and there are three ways of reading one. Which is used is a setting, and the picture stays as it is.

**Colour.** Hue is the angle of the force and saturation is its length. The colour of a region therefore names a direction — the force turns smoothly as the hue turns, with no seam where the wheel wraps, because the angle is used as an angle rather than as a number. Grey pushes nothing whatever its hue.

The picture's own average force is taken off every reading. Without that, an image that is mostly blue pushes everything blue-wards for ever, and rotating the whole field only changes which way it is pushed; what is wanted is how each region differs from the picture's own average.

**Slope.** The direction in which the channel changes fastest, taken from neighbouring pixels. A field of slopes has no average push by construction, and it follows what is actually in the picture, since a slope is an edge. The sprite runs across the features.

**Along the slope.** The same vector turned through a right angle, so the sprite travels along an edge rather than across it. It circulates rather than converging: a sprite orbits a bright shape, follows a horizon, runs round the rim of a cloud, and cannot settle in a corner because the field has no sinks in it.

**The blend.** One knob between across and along, since the interesting motion is the spiral between the two. So the chooser on the panel offers colour or slope, and along the slope is the slope with that knob at its full.

**The turn.** A knob that rotates the whole field, so a picture whose colours all push one way can be made to push another.

**The force.** A knob for how hard the picture pushes, from nothing to enough to throw a sprite across the square in a fraction of a second.

**Which channel.** Hue, saturation and value are the defaults — hue steers, saturation pushes, value is read — because they are what the eye reads. Each of the three jobs takes its channel from a setting in the menu, since which channel suits a picture is a thing to be heard rather than argued, and it is tried once for a picture rather than reached for while playing.

## The motion

A sprite has a position, a velocity, and a sign of plus or minus one for each axis.

**The physics runs on its own clock**, fast and steady, and not on the timer that makes notes. The accelerations and the drift have to be smooth however slowly or quickly notes are being sent, and a sprite whose motion was stepped by its own note timer would lurch.

**With no force it travels in a straight line** and bounces off the four walls of the square. A bounce does nothing else: no note, no trigger.

**The force is added each tick.** The speed is limited, both so the motion stays watchable and so a sprite can never cross a wall in one tick.

**The signs are the wall trick.** The force read from the picture is multiplied by the sprite's two signs before it is applied, and a bounce flips the sign for that axis. A region pushing rightwards therefore pushes a sprite away from the right-hand wall it has just left, rather than pinning it there.

**A drift when nothing else is happening.** A small random wander, so a sprite in a black region — no force, and its speed damped — keeps moving instead of sitting on one pixel for ever reading the same value. It is small on purpose: it is there to stop a sprite becoming stuck and for nothing else, and at the knob's full turn it moves the velocity by a few per cent of the speed limit in a second. What makes a sprite move is the picture.

**The transport.** Run holds the sprites still or lets them go. Rewind puts every sprite back where it started, and started again it retraces exactly the path it took before — which means the drift's random stream is part of where a sprite starts from, and is wound back with it. The starting point is taken whenever a sprite is placed or aimed by hand, that being what settling them somewhere means.

## The notes

Each sprite has a timer. When it fires, the pixel under the sprite is read — the value channel by default — and that reading becomes a note.

**The rate is between a floor and a ceiling**, and the reading can drive it: a dark region slows the firing towards the floor and a bright one quickens it towards the ceiling. That is where the sudden changes come from, since the sprite crosses boundaries rather than easing across them. Both limits are set on the panel.

**And a voltage can drive it too.** One polyphonic input, a channel per sprite: channel one is sprite one, channel two sprite two, and a cable with one channel applies to all of them. It sets where between the floor and the ceiling that sprite's timer sits, and the picture's own reading then moves it from there by whatever the depth knob says. So the rate can be played from outside — swept, stepped, clocked from elsewhere — while the picture still has its say, and with the depth at nought the voltage has it alone.

**And the chart can place them on a grid.** Each sprite has a note grid, a plate at the head of the timing row: Free, or a note value — whole, dotted half, half, half-note triplet, dotted quarter, quarter, quarter-note triplet, dotted eighth, eighth, eighth-note triplet, dotted sixteenth or sixteenth — shown as its symbol in Petaluma. At Free the timer alone says when a note begins, or the TRIG input's ticks do. At a note value, with a chart on the cable, a note may begin only on that grid: counted from the start of every bar of the chart, so a dotted quarter in four falls on one, two and a half, and four, and starts again at the next bar line, and timed by the chart's tempo, so everything downstream that reads the tempo off the cable is in step with it. The sprite's timer still says when a note is wanted, at the rate the picture drives; Sync then decides which grid point it goes to, as it does for a TRIG tick: the next one, the nearest, or every so many. The first grid point after the chart starts, or after a rewind, plays. Straight eighths and sixteenths in a bar of quarters lean where the chart's swing puts them; dotted and triplet grids are played as written. A note's length is its Duration share of the time since the sprite's last note. A sprite on a grid ignores the TRIG input, and with no chart it plays as Free.

**The reading is a contour rather than a pitch.** With a chart on the cable the harmony decides the note, as it does for every other module here: nought to one maps across the line's range and the chord says what that means. The same sprite over a different chart plays the same shape on different harmony.

**Or a sprite is a voice of the chord.** With a voicing on the cable there is a second way to read it, chosen by a setting: sprite one plays the lowest voice, sprite two the next, and so on. The picture then decides only when each voice sounds and how hard, and the chord decides what. Four sprites over a four-note voicing is a chord played by four things wandering about a picture, which is a different instrument from four lines against one harmony, and both are worth having. A sprite with no voice to take — four sprites over a three-note voicing — wraps round to the lowest.

A version that sends control voltages rather than notes is worth having later, for the modular side of the rack.

## The sprites

One to four, chosen by a switch. Each is placed by dragging it on the picture, which is where it starts from.

**A sprite is a disc and nothing else.** It wears the colour of the picture underneath it, so what is steering it and how it is moving are seen together, and it is ringed in a tone set a fixed distance from the brightness just outside it — light on a dark region, dark on a pale one, sliding between the two over about a fifth of a second rather than switching. Nothing is drawn but the discs. A line showing each sprite's heading and speed was tried and taken out again: four of them whipping about over a photograph drew the eye to the arithmetic rather than to the motion, and the motion is the point.

**One set of controls, for the sprite selected.** Clicking a sprite selects it, and the per-sprite controls on the panel — its timer's floor and ceiling, how much the reading drives the rate, its force gain — then belong to that one. Four sets of controls at once is a worse panel than one that follows the selection, and the settings are per sprite underneath whatever is shown.

**Several selected at once.** The tabs above the controls choose which sprite they show. A click on a tab selects that sprite alone. Shift and a click adds a tab to the selection or takes it out, and ALL, a square tab at the left of the strip, selects all four. A knob turned while several are selected sets that one setting on every selected sprite and leaves their other settings as they are, so sprites that differ elsewhere still differ; the knobs show the tab first clicked, outlined in white when others are selected with it. Every selected sprite wears its tab's coloured ring in the picture. Touching a sprite in the picture selects it alone, unless it is already one of several, when it becomes the one the knobs show. Command and a click on a tab puts that sprite's settings back to where a fresh module starts them — every knob at its default, two octaves of range, its own starting turn — or every selected sprite's, when the tab is one of them. Whether a sprite is playing, and where it is, are left alone.

Everything else is per sprite and kept in an array from the start, so the second and the fourth are settings rather than a rewrite: position, velocity, the two signs, the timer's phase and rate, and its own reading.

One sprite is the default; four is the arrangement worth reaching for, particularly with each playing a voice of the chord.

## What is saved

The picture itself, in the patch, rather than a path to a file: a patch sent to somebody else then sounds and looks as it did. The sprites' starting positions and velocities are saved with it.

## Not yet

A trail behind each sprite would make the motion easier to read and it is cheap, but it is left out of the first version: the sprites moving are legible enough to judge whether the field is doing anything worth hearing, and a trail is a thing to add once the rest is right.
