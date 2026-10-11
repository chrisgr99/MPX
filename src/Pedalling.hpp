#pragma once
/** PEDALLING FOR THE PASSAGES A SCORE DOES NOT MARK, written into the played timeline the way the
ornaments are, so that every instrument on the cable is pedalled the same way.

Most scores are under-marked. The Chopin Ballade marks 189 of its 262 bars, and this arrangement of
Finlandia marks bars 1 and 3 and then nothing until bar 43. Played literally the rest is dry: every
note is stopped a fraction of a second after its written end, which is not how anybody plays a
piano.

WHERE A LIFT CAN GO COMES FROM THE METRE, AND WHETHER ONE IS NEEDED FROM THE HARMONY. Neither does
the job alone. Measured against real pedal marks, lifting wherever the bass note changed caught
nearly every real lift and lifted seven times too often, because a left hand arpeggiating one chord
changes its bass note all the time; changing on every bar line did better than that, and holds
through every chord that changes inside a bar. So the candidates are the beats, and at each one the
question is whether what is about to be struck clashes with what is still ringing only because the
pedal is down. An arpeggio of the chord already held adds nothing new and the pedal stays down; a
new chord brings pitches a step away from those ringing and the pedal changes. See
test/pedaltest.cpp for how often that agrees with real marks. */
#include "GpTimeline.hpp"
#include "GuitarPro.hpp"

#include <vector>

namespace px {


/** THE NUMBERS THE RULE IS MADE OF, together so that test/pedaltest.cpp can try others. Read from
how often a beat carries a real lift in ten pedalled scores — by where it falls in the bar, how much
it clashes, how long the pedal has been down and whether it strikes a bass of its own — and checked
on twelve more that were not looked at while choosing them.

WHERE THE BEAT FALLS MATTERS MOST. Three first beats of a bar in four carry a real lift, half of
the half bars, and three other beats in a hundred.

TWO MEASURES OF CLASH, both the roughness between what is ringing and what is struck, against the
louder of the two, squared. The NEW one counts only pitches not already ringing, so an arpeggio of
the chord held scores nothing. EVERY PAIR counts them all, because a pitch struck again still sounds
against the passing notes left ringing from the bar before — which is why a pianist changes on the
first beat even when the chord does not.

A BASS OF ITS OWN: the beat's lowest note at or below anything left ringing with its key up. Lifting
then clears the old sound and loses nothing underneath; lifting without one takes away the bass
the harmony stands on, and a pianist holds the pedal for it. */
struct PedalRule {
	/** ON THE FIRST BEAT OF A BAR: a change for any new pitch that clashes at all, for every pair
	clashing this much, or, with nothing clashing, when the bar strikes a bass of its own. */
	double firstNew = 0.002;
	double firstPairs = 0.03;
	bool firstOwnBass = true;
	/** HALF WAY THROUGH A BAR: a change when the new pitches clash this much, after the pedal has been
	down this many seconds, and, if asked, only with a bass of its own. */
	double halfNew = 0.02;
	double halfHeld = 0.0;
	bool halfOwnBass = false;
	/** ON ANY OTHER BEAT: only a clear clash, after the pedal has been down this long, over a bass of
	its own. */
	double otherNew = 0.08;
	double otherHeld = 0.6;
	bool otherOwnBass = true;
	/** THE LONGEST THE PEDAL IS HELD WITHOUT A CHANGE, in seconds. Past this, the next first beat
	changes it whatever the harmony is doing. */
	double longest = 8.0;
	/** A PASSAGE NEEDS THIS MANY BARS WITH NO MARK to be pedalled here. A shorter gap between two
	marks is the writer lifting on purpose. */
	double unmarkedBars = 2.0;
};


/** WHETHER A TRACK IS ONE A SUSTAIN PEDAL BELONGS TO: a piano of some kind, acoustic or electric.
A harpsichord and a clavinet have none, and nor does anything else a score can hold. */
bool gpPedalledTrack(const GpTrack& track);

/** THE PEDAL MOVES A PIANIST WOULD MAKE between two places in the played timeline, in quarters,
worked out from the notes of the pedalled tracks. The pedal is up at `from`; it goes down with the
first note, changes where the harmony asks, comes up for rests and staccato, and comes up at `to`
if it is down then. Every move is marked as generated. */
std::vector<GpPlayedPedal> gpPedalPassage(const GpSong& song, const GpTimeline& line, double from,
                                          double to, const PedalRule& rule = PedalRule());

/** THE MARKS WITH THE GAPS FILLED: `line.pedals`, and pedalling of its own in every stretch of
`rule.unmarkedBars` bars or more that they leave the pedal up, into `line.pedalsFilled`. Nothing
when the song has no pedalled track. */
void gpFillPedalling(const GpSong& song, GpTimeline& line, const PedalRule& rule = PedalRule());


} // namespace px
