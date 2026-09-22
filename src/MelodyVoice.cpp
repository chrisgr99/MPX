#include "MelodyVoice.hpp"
#include "ChartLayout.hpp"

#include <algorithm>
#include <cmath>

namespace px {

/** THE ENDS OF THE REPEATED NOTES KNOB, as weights against a step's one. Calibrated by
`make melodytest`, which prints the share of repeated notes the knob gives across its range. */
static const float REPEAT_LOW = 0.03f;
static const float REPEAT_HIGH = 1.5f;

bool voiceStyle(int style, float* out) {
	// A melody line in each, drawn from the scale bent to each chord. Pop repeats a note on 29 per
	// cent of intervals and jazz on 5, which is what REPEATED NOTES is set for; the ballads hold
	// and slur more, the up-tempo styles leap and accent more.
	static const float VOICE[6][VOICE_STYLE_PARAMS] = {
		//  smooth lock  reg   span  artic accent lead  scale breath end  role repeat motif contour
		{   0.85f, 0.6f, 67.f, 14.f, 0.4f, 0.2f, 1.5f, 4.f, 0.f, 0.f,  1.f, 0.55f, 0.6f, 0.6f },  // Pop 1
		{   0.8f,  0.6f, 67.f, 14.f, 0.2f, 0.3f, 1.5f, 4.f, 0.f, 0.f,  1.f, 0.6f,  0.6f, 0.6f },  // Pop 2
		{   0.75f, 0.55f,69.f, 16.f, 0.f,  0.5f, 1.2f, 4.f, 0.f, 0.f,  1.f, 0.65f, 0.6f, 0.5f },  // Pop 3
		{   0.75f, 0.55f,67.f, 17.f, 0.2f, 0.2f, 1.8f, 4.f, 0.f, 0.2f, 1.f, 0.3f,  0.4f, 0.4f },  // Jazz 1
		{   0.65f, 0.5f, 67.f, 19.f, 0.f,  0.3f, 1.8f, 4.f, 0.f, 0.3f, 1.f, 0.25f, 0.3f, 0.3f },  // Jazz 2
		{   0.55f, 0.45f,69.f, 22.f,-0.2f, 0.4f, 2.f,  4.f, 0.f, 0.4f, 1.f, 0.2f,  0.3f, 0.2f },  // Jazz 3
	};
	if (style < 1 || style > 6)
		return false;
	for (int i = 0; i < VOICE_STYLE_PARAMS; i++)
		out[i] = VOICE[style - 1][i];
	return true;
}

bool voiceStrongBeat(const Harmony& h) {
	const float inBar = h.beatInBar;
	int whole = (int) std::lround(inBar);
	const float off = std::fabs(inBar - (float) whole);
	if (h.barBeats > 0 && whole >= h.barBeats)
		whole -= h.barBeats;
	return off < 0.25f && (whole % 2) == 0;
}

float voiceRepeatWeight(float repeats) {
	const float k = std::max(0.f, std::min(1.f, repeats));
	return REPEAT_LOW + (REPEAT_HIGH - REPEAT_LOW) * k * k;
}

int voiceNoteFor(const Harmony& sounding, const VoiceSettings& s, int previous, float dice,
		const int* taken, int takenCount, float separation, MelodyReport* report,
		uint8_t flags, int beforePrevious, float heldBeats, const VoiceLine* line) {
	const Harmony& h = sounding;
	// THE PROFILE, GENERATED FROM TWO KNOBS. The register is given as a low note and a width, so
	// the knob that names the centre is turned into the bottom of the window.
	MelodyProfile profile = melodyProfile(s.smooth, s.lock, s.centre - s.span / 2, s.span);
	profile.lead = s.leading;
	if (s.repeats >= 0.f)
		profile.unison = voiceRepeatWeight(s.repeats);

	int chord[MAX_CHORD_TONES], next[MAX_CHORD_TONES];
	int chordCount = chordPitchClasses(h.current, h.key, chord);
	// OVER A DIMINISHED CHORD THE LINE MOVES THROUGH ITS TONES AND HOLDS ONLY THOSE IN THE KEY.
	// In Michelle the melody over D diminished in F climbs F, A flat, B natural and steps down to
	// G on the C that follows: every tone of the chord, moving. What sounds wrong is holding A flat
	// or B, the tones outside the key — so a long note over a diminished chord takes its tones in
	// the key, D and F (see the held-note anchors below), and the others are passed through. Every
	// other note of the key lies a semitone from one of its tones, so the palette is the chord.
	int full[MAX_CHORD_TONES];
	const int fullCount = chordCount;
	for (int i = 0; i < chordCount; i++)
		full[i] = ((chord[i] % 12) + 12) % 12;
	const bool diminished = h.current.quality == Q_DIM || h.current.quality == Q_DIM7;
	int inKeyTones[MAX_CHORD_TONES];
	int inKeyCount = 0;
	{
		int keyPcs[7];
		scalePitchClasses(h.key, keyPcs);
		for (int i = 0; i < chordCount; i++) {
			bool inKey = false;
			for (int k = 0; k < 7; k++)
				inKey = inKey || ((keyPcs[k] % 12) + 12) % 12 == full[i];
			if (inKey)
				inKeyTones[inKeyCount++] = full[i];
		}
	}
	const int nextCount = h.next.valid ? chordPitchClasses(h.next, h.key, next) : 0;

	int scale[12];
	int scaleCount = melodyScalePitchClasses(h.key, s.scale, scale);
	// FROM THE CHORD: the chord's own tones, and of the key's other notes only those that sit
	// comfortably against it — none a semitone from a chord tone, none a tritone from its root.
	// Over F that is F major pentatonic; over B flat 7 in F, B flat, C, D, F, G and A flat; over
	// E flat 6, E flat major pentatonic; over a diminished chord, the chord. So nothing
	// the line holds can rub against the harmony. Keeping every key note but the ones next to an
	// added chord tone left E over B flat 7 and A over E flat 6, and the line sounded out of tune.
	if (s.scale == SCALE_FROM_CHORD && chordCount > 0) {
		const int rootPc = ((chordRootPitchClass(h.current, h.key) % 12) + 12) % 12;
		int keyPcs[7];
		scalePitchClasses(h.key, keyPcs);
		scaleCount = 0;
		for (int i = 0; i < chordCount && scaleCount < 12; i++)
			scale[scaleCount++] = ((chord[i] % 12) + 12) % 12;
		for (int k = 0; k < 7 && scaleCount < 12; k++) {
			const int pc = ((keyPcs[k] % 12) + 12) % 12;
			bool keep = ((pc - rootPc + 12) % 12) != 6;
			for (int i = 0; i < fullCount && keep; i++) {
				const int d = ((((full[i] - pc) % 12) + 12) % 12);
				if (d == 0 || d == 1 || d == 11)
					keep = false;
			}
			if (keep)
				scale[scaleCount++] = pc;
		}
	}

	// WHICH KEY NOTE EACH OUTSIDE NOTE ALTERS. A note borrowed from the parallel key — A flat, E
	// flat or D flat in F major — lowers the key note above it; any other outside note, B natural in
	// F, raises the one below. In a minor key the reverse: the parallel major's notes are raised.
	int naturalOf[12];
	{
		int keyPcs[7];
		scalePitchClasses(h.key, keyPcs);
		bool inKey[12] = {};
		for (int k = 0; k < 7; k++)
			inKey[((keyPcs[k] % 12) + 12) % 12] = true;
		static const int MINOR[7] = {0, 2, 3, 5, 7, 8, 10};
		static const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
		bool inParallel[12] = {};
		const int tonic = ((int) h.key.tonic % 12 + 12) % 12;
		for (int k = 0; k < 7; k++)
			inParallel[(tonic + (h.key.minor ? MAJOR[k] : MINOR[k])) % 12] = true;
		for (int pc = 0; pc < 12; pc++) {
			naturalOf[pc] = -1;
			if (inKey[pc])
				continue;
			const bool lowered = h.key.minor ? !inParallel[pc] : inParallel[pc];
			const int nat = (pc + (lowered ? 1 : 11)) % 12;
			if (inKey[nat])
				naturalOf[pc] = nat;
		}
	}

	MelodyAsk ask;
	ask.naturalOf = naturalOf;
	ask.previous = previous;
	ask.beforePrevious = beforePrevious;
	ask.scale = scale;
	ask.scaleCount = scaleCount;
	ask.chord = chord;
	ask.chordCount = chordCount;
	ask.rootPc = chordRootPitchClass(h.current, h.key);
	ask.nextChord = next;
	ask.nextChordCount = nextCount;
	ask.beatsToNext = h.beatsToNext;

	// A STRONG BEAT IS A WHOLE BEAT AT THE FRONT OF THE BAR OR HALFWAY THROUGH IT. Asked of the
	// bar rather than of a count of notes, because what makes a beat strong is where it falls in
	// the music and not how many notes have gone by.
	//
	// TO THE NEAREST BEAT, NOT THE ONE BEFORE. A rhythm source on its own clock arrives a hair
	// early or late, and rounding down turned a note two hundredths of a beat before beat three
	// into the tail of beat two. The first recording showed nought strong beats in 351 notes,
	// which meant the strong-beat chord pull and the root pull had never applied to anything.
	ask.strong = voiceStrongBeat(h);

	// WHERE A PHRASE IS GOING. The chart says how a phrase ends and how far off that end is;
	// these are the pitches that make an ending sound like one.
	//
	// A CLOSING CADENCE ARRIVES ON THE TONIC: the ear is waiting for it, and a line that lands
	// anywhere else at a full close sounds as though it stopped rather than arrived. A HALF
	// CADENCE LEAVES THE QUESTION OPEN: the dominant's root, the fifth of the key, sits over it
	// without resolving, so a phrase can end unfinished on purpose, and the next one is an answer.
	//
	// WHICH NOTE: the one the rhythm marks as the phrase's last, when it marks one — and otherwise
	// the notes within two beats of the phrase's end, with a pull growing from nothing to strong,
	// which is the best a melody can do alone. The window alone misses the note that matters
	// whenever the phrase breathes at its end: the last note then falls two or three beats before
	// the end, and the arrival was never pulled at all. There is no pull anywhere else in a
	// phrase: a tonic pull applied throughout would make every note the tonic.
	// A PICKUP BELONGS TO THE PHRASE IT LEADS INTO. It sounds inside the ending phrase's last two
	// beats, which is exactly where the window pulls toward the ending phrase's arrival note — so
	// without this, every pickup would have been drawn toward the note the line had just left.
	const bool pickup = (flags & Event::PICKUP) != 0;
	const bool marked = (flags & Event::ARRIVAL) != 0 && !pickup;
	const bool nearEnd = h.phraseBeats > 0.f && h.beatsToPhraseEnd <= 2.f && !pickup;
	int anchors[MAX_CHORD_TONES > 3 ? MAX_CHORD_TONES : 3];
	int anchorCount = 0;
	if (marked || nearEnd) {
		const int tonic = ((int) h.key.tonic % 12 + 12) % 12;
		switch (h.phraseCadence) {
			case CADENCE_AUTHENTIC:
			case CADENCE_PLAGAL:
			case CADENCE_BACKDOOR:
			case CADENCE_TRITONE:
				anchors[anchorCount++] = tonic;
				break;
			case CADENCE_HALF:
				// THE DOMINANT'S ROOT, the fifth of the key: the one note that pauses over a dominant
				// rather than hanging. Its third is the key's leading tone, its seventh must fall, and
				// its fifth is the key's second degree — each held at a phrase end in Michelle left the
				// line suspended rather than paused. From the chord rather than the key, since a minor
				// key's dominant is its own chord.
				anchors[anchorCount++] = ((chord[0] % 12) + 12) % 12;
				break;
			default:
				break;
		}
	}
	// A BREATH TAKEN ON A CONSONANCE. The last note before a pause inside a phrase lands on a
	// tone of the chord sounding — more gently than an arrival, since the phrase is not over.
	// Without it a group could stop on any passing note, which is the other half of a line that
	// never seems to settle anywhere.
	//
	// ONLY THE TRIAD: root, third and fifth. A breath note is usually the held one, and a seventh or
	// an added sixth held through a breath sounds unfinished — the seventh wants to fall. In
	// Michelle a line breathed on A flat held over B flat 7, the chord's seventh.
	const bool groupEnd = (flags & Event::GROUP_END) != 0 && !marked;
	bool heldNote = false;
	if (groupEnd && anchorCount == 0) {
		anchorCount = std::min(chordCount, 3);
		for (int i = 0; i < anchorCount; i++)
			anchors[i] = ((chord[i] % 12) + 12) % 12;
	}
	// A LONG NOTE IS A CHORD TONE, wherever it falls in a line. A note held three quarters of a
	// beat or more is heard against the harmony rather than passing through it, and on anything
	// but the chord it sounds unstable — the root, third or fifth, or any tone of a diminished
	// seventh, whose tones are all alike. Endings have their own anchors already.
	if (anchorCount == 0 && heldBeats >= 0.75f && chordCount > 0) {
		if (diminished && inKeyCount > 0) {
			anchorCount = inKeyCount;
			for (int i = 0; i < inKeyCount; i++)
				anchors[i] = inKeyTones[i];
		}
		else {
			anchorCount = std::min(chordCount, 3);
			for (int i = 0; i < anchorCount; i++)
				anchors[i] = ((chord[i] % 12) + 12) % 12;
		}
		heldNote = true;
	}

	// AN ENDING HELD INTO THE NEXT CHORD belongs to both. Chosen for the chord it starts over, F
	// ended a line over B flat 7 in Michelle and then sounded on over E flat 6, where it is not a
	// chord tone; B flat is in both. So where the ending will still be sounding when the chord
	// changes, the anchors are narrowed to the tones of the next chord's triad — when any of them
	// is left.
	if ((marked || groupEnd || heldNote) && anchorCount > 0 && nextCount > 0 && h.beatsToNext > 0.f
			&& heldBeats > h.beatsToNext + 0.1f) {
		int both[MAX_CHORD_TONES > 3 ? MAX_CHORD_TONES : 3];
		int bothCount = 0;
		for (int i = 0; i < anchorCount; i++) {
			for (int j = 0; j < std::min(nextCount, 3); j++) {
				if (anchors[i] == ((next[j] % 12) + 12) % 12) {
					both[bothCount++] = anchors[i];
					break;
				}
			}
		}
		// NOT IF IT MEANS REPEATING THE NOTE BEFORE: B flat struck again after a B flat is one note
		// played twice, and an ending should move. The current chord's triad stands instead.
		const int prevPc = previous >= 0 ? ((previous % 12) + 12) % 12 : -1;
		if (bothCount == 1 && both[0] == prevPc)
			bothCount = 0;
		if (bothCount > 0) {
			anchorCount = bothCount;
			for (int i = 0; i < bothCount; i++)
				anchors[i] = both[i];
		}
	}
	// THE APPROACH: the note before a full close steps from the second or the seventh, the two
	// notes either side of the tonic, so the ending is arrived at rather than leapt to. Only
	// before a close — the question at a half cadence is left to find its own way.
	const bool approach = (flags & Event::APPROACH) != 0 && anchorCount == 0 && !marked;
	const bool closing = h.phraseCadence == CADENCE_AUTHENTIC || h.phraseCadence == CADENCE_PLAGAL
		|| h.phraseCadence == CADENCE_BACKDOOR || h.phraseCadence == CADENCE_TRITONE;
	if (approach && closing) {
		int degrees[7];
		scalePitchClasses(h.key, degrees);
		anchors[anchorCount++] = degrees[1];
		anchors[anchorCount++] = degrees[6];
	}

	// THE STEP INTO AN ENDING avoids the note the ending will land on; the ending itself moves off
	// the note before it.
	int endingPcs[2];
	int endingCount = 0;
	if (approach) {
		if (closing)
			endingPcs[endingCount++] = ((int) h.key.tonic % 12 + 12) % 12;
		else if (h.phraseCadence == CADENCE_HALF && chordCount > 0)
			endingPcs[endingCount++] = ((chord[0] % 12) + 12) % 12;
	}
	ask.arriveByStep = (flags & Event::ON_CHANGE) != 0;
	ask.avoid = endingPcs;
	ask.avoidCount = endingCount;
	ask.moveOn = marked || groupEnd;

	if (anchorCount > 0) {
		ask.anchor = anchors;
		ask.anchorCount = anchorCount;
		// DECISIVE ON THE ARRIVAL, because at a full close the tonic is not a preference but what
		// the style is. A pull of four lost to smoothness — a step down from G to F outweighed it,
		// and phrases ended on the fourth, over a C chord, which is not even a chord tone. At
		// forty it still lost whenever every target note was a leap away, so the arrival is now
		// chosen AMONG the target notes, with the rest of the weighting picking the nearest. A
		// half cadence is as decisive, with the dominant's three or four tones to choose among.
		if (marked) {
			ask.anchorStrength = 40.f;
			ask.anchorOnly = true;
		}
		else if (approach && closing) {
			ask.anchorStrength = 5.f;
		}
		else if (heldNote) {
			ask.anchorStrength = 4.f;
			ask.anchorOnly = true;
		}
		else if (groupEnd && !nearEnd) {
			// ONLY THE TRIAD, not merely toward it: at a pull of four a low CHORD LOCK let a breath
			// land on a passing note, and a held passing note is the least stable thing a line can
			// stop on.
			ask.anchorStrength = 4.f;
			ask.anchorOnly = true;
		}
		else {
			const float near = 1.f - std::min(1.f, std::max(0.f, h.beatsToPhraseEnd / 2.f));
			ask.anchorStrength = 1.f + 3.f * near * near;
		}
	}

	// MOTIF. In thirty pop songs, when a line restates an earlier line's rhythm it takes the same
	// notes a third of the time and new ones half the time; moving the same steps elsewhere is
	// rare. So the pitch itself is what is pulled toward, hard, and the moved step only a little,
	// for when the pitch is out of reach or out of the chord.
	if (line && s.motif > 0.f && line->echoPitch >= 0) {
		const float m = std::min(1.f, s.motif);
		ask.echo = line->echoPitch;
		ask.echoWeight = 1.f + 60.f * m * m;
		ask.echoStep = line->echoStep;
		ask.echoStepWeight = 1.f + 12.f * m * m;
		// AND WHERE NEITHER FITS THE CHORD NOW SOUNDING, THE SHAPE: the same way by about as far,
		// so a figure over new harmony is still heard as the same figure.
		ask.hasEchoMove = line->hasEchoMove;
		ask.echoMove = line->echoMove;
		ask.echoShapeWeight = 1.f + 4.f * m * m;
	}
	// CONTOUR. In the same songs a line's high point comes early — a quarter of the way through,
	// at the median, and at its very first note in a quarter of them — and the line falls about
	// four semitones from there to its last note, which ends about where it began. So the aim
	// rises four semitones over the first quarter and falls five over the rest.
	if (line && s.contour > 0.f && line->along > 0.001f && line->lineStart >= 0) {
		const float x = std::min(1.f, line->along);
		const float f = x < 0.25f ? x / 0.25f : 1.f - 1.25f * (x - 0.25f) / 0.75f;
		ask.aim = (float) line->lineStart + 4.f * f;
		ask.aimStrength = std::min(1.f, s.contour);
	}

	ask.dice = dice;
	ask.taken = taken;
	ask.takenCount = takenCount;
	ask.separation = separation;
	ask.report = report;
	return melodicStep(ask, profile);
}

VoiceShape voiceShape(const Harmony& h, const VoiceShapeSettings& s, int note, float beats,
		float level, bool strong, uint8_t flags, float draw) {
	VoiceShape out;
	out.beats = std::max(0.01f, beats);
	out.level = level;
	const int pc = ((note % 12) + 12) % 12;

	// BREATH: A REST BEFORE EACH PHRASE END, for a rhythm that does not phrase. A note that
	// would start inside the last BREATH beats of a phrase is not played, and one that would run
	// into them is cut short, so the line stops and the phrase is heard to end. A rhythm that
	// marks its notes — mpxPhrase — has already placed its breaths, and adding another on top
	// made every phrase end twice, so BREATH leaves such a rhythm alone.
	if (s.breath > 0.f && flags == 0 && h.phraseBeats > 0.f) {
		if (h.beatsToPhraseEnd < s.breath - 0.01f) {
			out.drop = true;
			return out;
		}
		out.beats = std::max(0.05f, std::min(out.beats, h.beatsToPhraseEnd - s.breath));
	}

	// ARTICULATION: the rhythm's lengths, shortened or held. Held past its own length a note is
	// also slurred — handed to the next note when that arrives rather than ended before it, which
	// is what makes held notes a legato line rather than overlapping ones.
	const float a = std::max(-1.f, std::min(1.f, s.articulation));
	if (a < 0.f)
		out.beats *= 1.f + 0.7f * a;
	else if (a > 0.f) {
		out.beats *= 1.f + a;
		out.slur = true;
	}

	// END NOTES AT CHANGES: a note sounding across a chord change, ended at it — always, when it
	// would clash with the new chord by a semitone, since holding that is the one thing that
	// never sounds intended; otherwise as often as the knob says.
	if (h.next.valid && h.beatsToNext > 0.f && h.beatsToNext < out.beats - 0.01f) {
		int next[MAX_CHORD_TONES];
		const int n = chordPitchClasses(h.next, h.key, next);
		bool in = false, clash = false;
		for (int i = 0; i < n; i++) {
			const int t = ((next[i] % 12) + 12) % 12;
			in = in || t == pc;
			const int d = std::abs(t - pc);
			clash = clash || d == 1 || d == 11;
		}
		// A LINE'S HELD ENDING IS HELD THROUGH THE CHANGE, as a singer holds the last note of a
		// line while the band moves on — unless it clashes. Cutting it three times in five, at an
		// END NOTES AT CHANGES of 0.6, undid HOLD: the rhythm held the ending for three beats and
		// the voice ended it at the next bar line.
		const bool ending = (flags & (Event::ARRIVAL | Event::GROUP_END)) != 0;
		if ((clash && !in) || (!ending && draw < s.endAtChanges)) {
			out.beats = h.beatsToNext;
			out.slur = false;
		}
	}

	// ACCENT: levels evened toward a middle value, or leaned — louder on strong beats and chord
	// tones, softer off them.
	const float c = std::max(-1.f, std::min(1.f, s.accent));
	if (c < 0.f) {
		out.level += (0.75f - out.level) * -c;
	}
	else if (c > 0.f) {
		int chord[MAX_CHORD_TONES];
		const int n = chordPitchClasses(h.current, h.key, chord);
		bool chordTone = false;
		for (int i = 0; i < n; i++)
			chordTone = chordTone || ((chord[i] % 12) + 12) % 12 == pc;
		out.level *= 1.f + c * ((strong ? 0.3f : -0.15f) + (chordTone ? 0.1f : -0.1f));
	}
	out.level = std::max(0.05f, std::min(1.f, out.level));
	return out;
}

} // namespace px
