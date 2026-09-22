#pragma once
/** ONE VOICE'S CHOICE OF NOTE, from the harmony on the cable — everything mpxVoice decides, with
no Rack module in it.

WHY IT IS SEPARATE. The melodic step in Melodic.hpp is pure, but what the voice hands it was
worked out inside the module: which scale, which chord, whether a beat is strong, and — since the
phrase anchor — where the phrase is going. A simulation that wanted to know what the melody would
play had to copy all of that, and a copy is a second opinion that drifts from the first. So the
gathering lives here, the module calls it, and so does anything that wants to predict the module.
*/
#include "NoteBus.hpp"
#include "Melodic.hpp"

namespace px {

/** The knobs of one voice, as numbers. */
struct VoiceSettings {
	float smooth = 0.75f;
	float lock = 0.55f;
	/** The note the line is centred on, as a MIDI number, and how wide it may range. */
	int centre = 72;
	int span = 24;
	float leading = 1.f;
	int scale = 0;
	/** REPEATED NOTES, nought to one: how often the line stays on the note it is on. Below
	nought, the interval table's own figure, which is what the voice did before it had the knob. */
	float repeats = -1.f;
	/** MOTIF, nought to one: how surely a note restating an earlier note's rhythm takes its pitch
	too, so the figure comes back whole. See Event::echo. */
	float motif = 0.f;
	/** CONTOUR, nought to one: how hard each breath group is pulled into the shape of a sung
	line — up to its high point a quarter of the way through, then down to end a little below
	where it began. */
	float contour = 0.f;
};

/** WHAT THE LINE KNOWS ABOUT ITS OWN PAST, for one note: the pitch of the note it restates and
the same step moved to here (MOTIF), and how far through its group it is and where the group
began (CONTOUR). -1 throughout for nothing known. */
struct VoiceLine {
	int echoPitch = -1;
	int echoStep = -1;
	/** The move the restated note made from the note before it, when both are known. */
	bool hasEchoMove = false;
	int echoMove = 0;
	float along = -1.f;
	int lineStart = -1;
};

/** THE NOTES A VOICE HAS PLAYED, as many as a motif can reach back for: every note that arrives,
including one later dropped into a breath, since the rhythm counts back through all of them. The
module and the simulation both keep one, so the two find the same notes. */
struct VoiceMemory {
	static const int SIZE = 512;
	int notes[SIZE];
	long count = 0;
	int lineStart = -1;

	/** The note played `k` notes ago, one being the last; -1 if there is none. */
	int back(int k) const {
		if (k < 1 || k > count || k > SIZE)
			return -1;
		return notes[(count - k) % SIZE];
	}
	/** What the next note, restating the note `echo` back and falling `along` its group, knows. */
	VoiceLine lineFor(int echo, float along) const {
		VoiceLine l;
		l.along = along;
		l.lineStart = along > 0.001f ? lineStart : -1;
		if (echo > 0) {
			l.echoPitch = back(echo);
			const int srcBefore = back(echo + 1), last = back(1);
			if (l.echoPitch >= 0 && srcBefore >= 0) {
				l.hasEchoMove = true;
				l.echoMove = l.echoPitch - srcBefore;
				if (last >= 0)
					l.echoStep = last + l.echoMove;
			}
		}
		return l;
	}
	void remember(int note, float along) {
		notes[count % SIZE] = note;
		count++;
		if (along >= 0.f && along <= 0.001f)
			lineStart = note;
	}
	void clear() {
		count = 0;
		lineStart = -1;
	}
};

/** THE WEIGHT A REPEATED NOTE IS GIVEN at a setting of the REPEATED NOTES knob, against a step's
one. On a square, so the knob's travel is spread evenly over the share of repeated notes a
listener hears — a few per cent at nought, about a sixth at the middle, more than half at full —
rather than bunched into its last quarter, which is where an exponential put it. */
float voiceRepeatWeight(float repeats);

/** THE NOTE, as a MIDI number. `previous` is the line's last note or -1 to start one; `dice` is
the draw, nought to one; `taken` and `separation` are what the voices drawn before this one have
just played and how hard to avoid them. `report`, if given, receives the candidates. */
int voiceNoteFor(const Harmony& h, const VoiceSettings& s, int previous, float dice,
	const int* taken = NULL, int takenCount = 0, float separation = 0.f,
	MelodyReport* report = NULL, uint8_t flags = 0, int beforePrevious = -1,
	float heldBeats = 0.f, const VoiceLine* line = NULL);

/** WHAT THE VOICE DOES TO A NOTE ONCE IT HAS CHOSEN THE PITCH: how long it lasts, how hard it
is played, and whether it is played at all. The four knobs below the note choice on the panel. */
struct VoiceShapeSettings {
	/** Minus one short, nought as the rhythm sent it, one held and slurred into the next. */
	float articulation = 0.f;
	/** Minus one evened out, nought as sent, one leaning on strong beats and chord tones. */
	float accent = 0.f;
	/** Beats of rest before each phrase end — for a rhythm that does not phrase. */
	float breath = 0.f;
	/** How often a note sounding across a chord change is ended at it, nought to one. A line's
	held ending is exempt, and a note clashing with the new chord by a semitone always ends. */
	float endAtChanges = 0.f;
};

struct VoiceShape {
	/** Not played: the note falls in the breath at a phrase's end. */
	bool drop = false;
	/** How long it lasts, in beats, and how hard, nought to one. */
	float beats = 0.f;
	float level = 0.f;
	/** Held until the next note arrives, and handed over to it rather than ended before it —
	which is what slurring is. */
	bool slur = false;
};

/** THE SHAPE OF ONE NOTE. `beats` and `level` are what the rhythm sent; `strong` is whether it
falls on a strong beat; `flags` are the rhythm's (see Event::flags); `draw` is nought to one and
decides, where the knob asks for a likelihood rather than a certainty. */
VoiceShape voiceShape(const Harmony& h, const VoiceShapeSettings& s, int note, float beats,
	float level, bool strong, uint8_t flags, float draw);

/** THE VOICE THAT GOES WITH EACH PHRASE STYLE (see PhraseStyle), as mpxVoice's parameters by
number: smoothness, chord lock, register (MIDI), span, articulation, accent, voice leading, scale,
breath, end notes at changes, role, repeated notes, motif, contour. Returns false for a style with
no voice of its own — SONG, the defaults. Shared by the presets and by the chart's STYLE, so the
two agree. */
static const int VOICE_STYLE_PARAMS = 14;
bool voiceStyle(int style, float* out);

/** Whether a note falls on a strong beat, by the same rule the melodic step uses. */
bool voiceStrongBeat(const Harmony& h);

} // namespace px
