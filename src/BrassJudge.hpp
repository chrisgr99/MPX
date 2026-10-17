#pragma once
/** HOW A JOIN BETWEEN TWO NOTES SOUNDS, judged as the trombone's player rehearses it: phase 6 of
docs/brass.md.

From the cup's pressure every 2.5 ms, its pitch over the 10 ms around by de Cheveigné and Kawahara's YIN —
which takes neither an extra crossing for a period, where a transition has strong harmonics, nor half a
period for one, as zero crossings did, and so found squawks that were not there and steered the rehearsal
from natural slurs to gaps — from 60 ms before the join to the end of what was played:

SETTLE, how long after the join the new note first holds within 20 cents of itself for 40 ms, repeating
clearly (YIN's sureness over 0.8);

SMEAR, how long before that the pitch is more than 30 cents from both notes and between them, each period
counted by its loudness against the new note's — a glide the legato tongue keeps 12 dB down counts a quarter —
while sounding within 20 dB of it: the glide a slide makes, or two notes beating as one dies and the other
grows;

SQUAWK, how long the pitch is more than a semitone outside both, while sounding: the lips caught for a moment
on another resonance;

ROUGH, how long before it settles the sound does not repeat (sureness under 0.6), counted by its loudness as
the smear is: two notes at once, or neither;

DIP, the deepest the level falls, in 5 ms windows, under the quieter of the two notes; and GAP, how long it is
more than 20 dB under it.

A rehearsal plays the way with the least score. Without the gap in it, it chose for some joins of the slurred
scale a legato tongue that stopped the lips while the slide moved and left 150 ms of near silence, because
each way that kept them sounding made a glide; a silence between two slurred notes is heard as a gap, and a
glide kept well down by the tongue is hardly heard at all.

Slurs on a real instrument settle in about 80 to 100 ms on the horn (Logie et al. 2009), and a listener hears
a smear or a squawk long before a dip.

NO RACK IN HERE. */
#include <cmath>
#include <cstddef>
#include <vector>

namespace px {


struct BrassJudged {
	double settle = 1e9, smear = 0.0, squawk = 0.0, rough = 0.0, dip = 0.0, gap = 0.0;
	/** A start's front over the note: its loudest 10 ms in its first 80, dB over where it settles, past 2. */
	double over = 0.0;
	/** One number, less better, in milliseconds: the settling, the smear and the roughness, a squawk and a
	gap counting three times, and each decibel a start's front stands over the note ten. */
	double score() const { return settle + smear + rough + 3.0 * squawk + 3.0 * gap + 10.0 * over; }
};

/** THE PITCH OF A STRETCH of `w` samples from `x`, hertz, by YIN, searched from `lowHz` to `highHz`; its
sureness, one where the stretch repeats exactly, into `sure`. `x` must hold rate / lowHz samples past it. */
double brassPitchOf(const double* x, int w, double rate, double lowHz, double highHz, double* sure);

/** HOW A START SOUNDS, a note tongued from silence or after a gap: from the tongue's release at `at`,
every 2.5 ms, the pitch by YIN over 10 ms of each stretch loud enough to hear (within 30 dB of where the
note settles, over its last 50 ms): SETTLE, how long until it holds within 20 cents of `hz`, repeating clearly,
for 40 ms; SMEAR, how long before that it is more than 20 cents off, counted by its loudness; ROUGH, how long
it does not repeat, likewise; SQUAWK, how long it is more than a semitone off; DIP, nought; GAP, how long it
takes to rise from 30 dB under its level to within 3 dB of it, past 60 ms — a start can be too slow as well
as too rough; OVER, how far its front, its loudest 10 ms in its first 80, stands over where it settles, past
2 dB. */
BrassJudged brassJudgeStart(const std::vector<double>& cup, const std::vector<double>& heard, size_t at, double rate, double hz);

/** `cup` and `heard` sampled at `rate` from the same start; the join at sample `at`; `before` and `after`
the two notes' levels as mean squares of `heard`, or nought to measure them from 200 to 100 ms before the join
and over the last 50 ms. */
BrassJudged brassJudgeJoin(const std::vector<double>& cup, const std::vector<double>& heard, size_t at, double rate, double fromHz,
	double toHz);


} // namespace px
