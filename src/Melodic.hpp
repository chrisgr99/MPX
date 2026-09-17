#pragma once
/** Choosing one note, and nothing else.

PURE, AND DELIBERATELY SO. No Rack, no module, no state: everything it needs arrives as
arguments and the answer is a number. That is what makes it testable from a command line over
thousands of chords, which is the only way to find out whether a weighting sounds like music
before there is anything to listen to.

PORTED FROM GXW, whose harmonyMelody.js has been played with for a long time. The numbers are
its numbers and the reasons are its reasons; where a comment here explains WHY a weight is what
it is, that reasoning came across with the code and is worth more than the code.

THE SHAPE OF IT IS THREE STEPS.

  PALETTE — every note of the chosen scale inside the register, within reach of the note before.
  WEIGHT  — each candidate multiplied by how much this line wants it: the interval from the
            last note, whether it is a chord tone, whether the beat is strong, whether it leads
            into the chord that is coming, where it sits in the register, and where the phrase
            is.
  PICK    — the weights become a running total and one number between nought and one chooses.

That last step is why a control voltage can drive a melody at all. The weighting decides what is
likely; the voltage decides which of the likely things actually happens, and the same voltage
against the same weighting always gives the same note.
*/
#include "Chord.hpp"

#include <vector>

namespace px {

/** HOW MUCH A LINE WANTS TO MOVE BY EACH INTERVAL, from a unison to an octave.

A WHOLE TONE IS THE COMMONEST STEP IN EVERY MELODY EVER WRITTEN, so it is the peak and
everything is measured against it. A semitone is nearly as wanted. Thirds are half as likely,
and the drop through the fourth to the tritone is the shape that makes a line singable — a
tritone is not forbidden but it is a twentieth as likely as a step, which is what "avoid it
unless you mean it" looks like as a number.

THE UNISON IS LOW, not nought: a repeated note is a real device and a line made of them is not a
line. The octave is lifted a little above the sevenths around it, because a leap of an octave is
heard as the same note again rather than as a wild jump. */
static const float INTERVAL_WEIGHTS[13] = {
	0.10f,  //  0  unison, a repeated note
	0.90f,  //  1  minor second
	1.00f,  //  2  major second, the commonest step
	0.50f,  //  3  minor third
	0.40f,  //  4  major third
	0.25f,  //  5  fourth
	0.05f,  //  6  tritone
	0.15f,  //  7  fifth
	0.06f,  //  8  minor sixth
	0.05f,  //  9  major sixth
	0.04f,  // 10  minor seventh
	0.03f,  // 11  major seventh
	0.10f,  // 12  octave
};

/** THE WEIGHTS ONE LINE IS DRAWN WITH. Generated from two macro controls rather than set one by
one — see melodyProfile — because the eleven numbers here are not independent of each other and
a panel with eleven knobs on it would be a panel nobody could set. */
struct MelodyProfile {
	/** The register, as MIDI note numbers. */
	int low = 60, high = 84;
	/** How far from the previous note a candidate may be, in semitones. */
	int window = 9;
	/** How hard the interval table is applied. Raising a weight below one to a higher power
	pushes it further down, so a larger number here means leaps are rejected harder. */
	float leapAversion = 1.6f;
	/** How much a chord tone is favoured, and a passing note allowed. */
	float chordPull = 2.7f;
	float passing = 0.6f;
	/** Extra, on a strong beat only. */
	float strongChordPull = 2.1f;
	/** Extra for the chord's root on a strong beat. A bass wants this high. */
	float rootPull = 1.f;
	/** How much a note a step away from a tone of the NEXT chord is favoured once the change is
	close, and how close is close, in beats. */
	float lead = 1.6f;
	float leadWindow = 1.f;
	/** How strongly the line is pulled back toward the middle of its register. */
	float gravity = 0.4f;
	/** Multiplier on candidates below the previous note. Above one, lines tend to fall — which
	is what melodies do, and what makes a rising line sound like an effort. */
	float descendBias = 1.05f;
};

/** THE TWO CONTROLS THE WHOLE PROFILE COMES FROM.

SMOOTHNESS sets the window and the leap aversion together, because they are the same musical
idea measured twice: how far the line may jump, and how much it dislikes jumping.

CHORD LOCK sets the pull toward chord tones against the allowance for everything else. At one
the line lands on the chord and stays there; at nought it floats over it and the harmony is
something the notes happen near.

Everything else keeps its default unless a caller says otherwise, which is what lets a role be a
handful of numbers rather than a whole profile. */
MelodyProfile melodyProfile(float smoothness, float chordLock, int low, int span);

/** The pitch classes a scale offers over the key's tonic. */
enum MelodyScale {
	SCALE_KEY,              /**< The key's own seven notes. */
	SCALE_MAJOR_PENT,
	SCALE_MINOR_PENT,
	SCALE_BLUES,
	SCALE_FROM_CHORD,       /**< Not built; behaves as the key. */
	NUM_MELODY_SCALES,
};

/** Writes the pitch classes of `which`, built on the key's tonic, and returns how many. At most
seven. A scale is not a key: every one of these is rooted on the chart's own tonic, so
transposing the chart moves them all, and what changes is the palette between the chord tones. */
int melodyScalePitchClasses(const Key& key, int which, int* out);

/** THE CANDIDATES A DECISION WAS MADE FROM, for anybody who wants to see why.

Filled only when a caller asks for it, by pointing `MelodyAsk::report` here. It exists because a
knob that seems to do nothing by ear may be doing exactly what it should to the odds, or nothing
at all, and the two can only be told apart by looking at the weights themselves. */
struct MelodyReport {
	static const int MAX = 64;
	int notes[MAX];
	/** Each candidate's share of the whole, nought to one, so they add up to one. */
	float share[MAX];
	int count = 0;
	/** Whether the weighting had nothing to offer and the nearest scale note was taken. */
	bool fellBack = false;
};

/** WHAT THE LINE IS BEING ASKED, all of it, so that the answer depends on nothing else. */
struct MelodyAsk {
	/** The note last played by this line, or -1 to start one. */
	int previous = -1;
	/** The palette, as pitch classes. */
	const int* scale = NULL;
	int scaleCount = 0;
	/** The chord sounding now, and the one coming. */
	const int* chord = NULL;
	int chordCount = 0;
	int rootPc = -1;
	const int* nextChord = NULL;
	int nextChordCount = 0;
	/** Beats until the chord changes, or a large number when nothing is known. */
	float beatsToNext = 999.f;
	/** Whether this note falls on a strong beat. */
	bool strong = false;
	/** The draw: nought to one. */
	float dice = 0.f;
	/** Pitch classes to favour at a phrase boundary, and by how much. One means no anchoring. */
	const int* anchor = NULL;
	int anchorCount = 0;
	float anchorStrength = 1.f;
	/** Pitch classes another voice has just taken, and how hard to avoid them. */
	const int* taken = NULL;
	int takenCount = 0;
	float separation = 0.f;
	/** Where to write the candidates, or NULL not to. */
	MelodyReport* report = NULL;
};

/** THE NOTE, as a MIDI number inside the profile's register. */
int melodicStep(const MelodyAsk& ask, const MelodyProfile& profile);

} // namespace px
