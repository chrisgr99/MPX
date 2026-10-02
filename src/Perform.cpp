/** See Perform.hpp. */
#include "Perform.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace px {

namespace {


/** The bits the cable uses, repeated here rather than included: this file knows nothing of the
bus, so that it builds into a command-line program. They are the same numbers. */
enum {
	T_HAMMER_ON = 1u << 0,
	T_PULL_OFF = 1u << 1,
	T_LEGATO_SLIDE = 1u << 2,
	T_SHIFT_SLIDE = 1u << 3,
	T_SLIDE_IN_BELOW = 1u << 4,
	T_SLIDE_IN_ABOVE = 1u << 5,
	T_SLIDE_OUT_DOWN = 1u << 6,
	T_SLIDE_OUT_UP = 1u << 7,
	T_LET_RING = 1u << 8,
	T_PALM_MUTE = 1u << 9,
	T_DEAD_NOTE = 1u << 10,
	T_GHOST = 1u << 11,
	T_ACCENT = 1u << 12,
	T_HEAVY_ACCENT = 1u << 13,
	T_HARMONIC = 1u << 14,
	T_ARTIFICIAL_HARMONIC = 1u << 15,
	T_TAPPED = 1u << 16,
	T_STACCATO = 1u << 17,
	T_TREMOLO = 1u << 18,
};

enum { V_NONE = 0, V_SLIGHT = 1, V_WIDE = 2 };
enum { G_NONE = 0, G_BEFORE = 1, G_ON = 2 };


float clamp(float v, float lo, float hi) {
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/** Cents as a fraction of a volt: twelve hundred cents to the octave. */
float centsToVolts(float cents) {
	return cents / 1200.f;
}

/** Where a bend has got to, part way through a note. The points are what the source meant; the
line between them is the performer's own, and a cosine is what a finger does — it leaves slowly,
crosses quickly and arrives slowly. */
float bendAt(const float* at, const float* cents, int count, float through) {
	if (count <= 0)
		return 0.f;
	if (through <= at[0])
		return cents[0];
	for (int i = 1; i < count; i++) {
		if (through > at[i])
			continue;
		const float span = at[i] - at[i - 1];
		const float part = (span > 0.f) ? (through - at[i - 1]) / span : 1.f;
		const float eased = 0.5f - 0.5f * std::cos(part * 3.14159265f);
		return cents[i - 1] + (cents[i] - cents[i - 1]) * eased;
	}
	return cents[count - 1];
}


} // namespace


// ---- the rules file ---------------------------------------------------------------------------

namespace {

/** Every rule, by the name it has in the file. One list, used for both reading and writing, so
the two can never disagree about what a rule is called. */
struct RuleName {
	const char* name;
	size_t offset;
};

#define RULE(field) {#field, offsetof(PerformRules, field)}

const RuleName RULES[] = {
	RULE(hammerGlideMs), RULE(hammerLevel),
	RULE(slideFraction), RULE(slideMaxMs), RULE(shiftSlideLevel),
	RULE(slideInSemitones), RULE(slideInMs),
	RULE(slideOutDownSemitones), RULE(slideOutUpSemitones), RULE(slideOutMs), RULE(slideOutFade),
	RULE(palmLength), RULE(palmMaxMs), RULE(palmLevel), RULE(palmTimbre),
	RULE(deadMs), RULE(deadLevel), RULE(deadTimbre),
	RULE(ghostLevel), RULE(ghostLength), RULE(staccatoLength),
	RULE(accentLevel), RULE(heavyAccentLevel),
	RULE(harmonicLevel), RULE(harmonicTimbre), RULE(openTimbre),
	RULE(letRingMaxSeconds),
	RULE(vibratoSlightCents), RULE(vibratoWideCents), RULE(vibratoHz),
	RULE(vibratoDelayMs), RULE(vibratoFadeMs),
	RULE(strumDownMs), RULE(strumUpMs), RULE(strumLevelFall), RULE(strumUpExtraFall),
	RULE(tremoloHz), RULE(tremoloLevelVary),
	RULE(timingMs), RULE(levelVary), RULE(lengthVary),
	RULE(bendRangeSemitones),
};

#undef RULE

const int RULE_COUNT = (int) (sizeof(RULES) / sizeof(RULES[0]));

float* fieldOf(PerformRules& rules, size_t offset) {
	return (float*) ((char*) &rules + offset);
}

std::string trimmed(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos)
		return "";
	const size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

} // namespace


int performRulesRead(const std::string& text, PerformRules& rules, std::string* complaint) {
	int set = 0;
	size_t at = 0;
	int line = 0;
	while (at <= text.size()) {
		const size_t end = text.find('\n', at);
		const std::string raw = text.substr(at, (end == std::string::npos) ? end : end - at);
		at = (end == std::string::npos) ? text.size() + 1 : end + 1;
		line++;

		// A comment or a blank line. The hash is what every configuration file uses.
		std::string one = trimmed(raw);
		const size_t hash = one.find('#');
		if (hash != std::string::npos)
			one = trimmed(one.substr(0, hash));
		if (one.empty())
			continue;

		const size_t equals = one.find('=');
		if (equals == std::string::npos) {
			if (complaint && complaint->empty())
				*complaint = "line " + std::to_string(line) + ": no equals sign";
			continue;
		}
		const std::string name = trimmed(one.substr(0, equals));
		const std::string value = trimmed(one.substr(equals + 1));
		bool known = false;
		for (int i = 0; i < RULE_COUNT; i++) {
			if (name != RULES[i].name)
				continue;
			*fieldOf(rules, RULES[i].offset) = (float) std::strtod(value.c_str(), NULL);
			known = true;
			set++;
			break;
		}
		// A NAME THIS VERSION HAS NEVER HEARD OF IS NOT AN ERROR. A file written for a later one
		// is read as far as it goes, which is better than refusing the lot.
		if (!known && complaint && complaint->empty())
			*complaint = "line " + std::to_string(line) + ": no rule called " + name;
	}
	return set;
}


std::string performRulesWrite(const PerformRules& rules) {
	PerformRules copy = rules;
	std::string out =
		"# How a note marked hammer-on, palm muted or bent actually sounds.\n"
		"#\n"
		"# Times are in milliseconds, levels and brightnesses are fractions, pitches are cents\n"
		"# or semitones as the name says. Anything after a hash is ignored, and a name this\n"
		"# version does not know is passed over rather than refused.\n\n";
	char buf[128];
	for (int i = 0; i < RULE_COUNT; i++) {
		std::snprintf(buf, sizeof(buf), "%-24s = %g\n", RULES[i].name,
			(double) *fieldOf(copy, RULES[i].offset));
		out += buf;
	}
	return out;
}


// ---- the performer ----------------------------------------------------------------------------

void Performer::strings(int count) {
	stringCount = (count < 0) ? 0 : ((count > PERFORM_VOICES) ? PERFORM_VOICES : count);
}


void Performer::silence() {
	for (int i = 0; i < PERFORM_VOICES; i++) {
		if (plans[i].sounding && plans[i].struck)
			push(PerformMessage::RELEASE, i, plans[i].key, 0.f);
		plans[i] = Plan();
		voices[i] = PerformVoice();
		voices[i].string = i + 1;
		voices[i].timbre = rules.openTimbre;
	}
}


void Performer::silenceVoice(int i) {
	if (i < 0 || i >= PERFORM_VOICES)
		return;
	if (plans[i].sounding && plans[i].struck)
		push(PerformMessage::RELEASE, i, plans[i].key, 0.f);
	plans[i].sounding = false;
	voices[i].gate = false;
	voices[i].level = 0.f;
}


void Performer::set(int i, bool timbre, float value) {
	if (i < 0 || i >= PERFORM_VOICES)
		return;
	if (timbre) {
		// THE SOURCE OVERRIDES THE RULE. A note that says how bright it is while it sounds knows
		// better than a table of what a palm mute usually does.
		plans[i].timbre = clamp(value, 0.f, 1.f);
		voices[i].timbre = plans[i].timbre;
	}
	else
		voices[i].pressure = clamp(value, 0.f, 1.f);
}


float Performer::noise() {
	// A small integer generator, because the same patch must play the same way twice and
	// because nothing here needs a better one.
	random = random * 1664525u + 1013904223u;
	return ((float) ((random >> 8) & 0xFFFF) / 32768.f) - 1.f;
}


void Performer::push(uint8_t kind, int voice, int key, float value, uint32_t technique,
		int fret) {
	if (queued >= (int) (sizeof(queue) / sizeof(queue[0]))) {
		// A block that produced more messages than this is a patch doing something no player
		// could; the oldest are the ones already acted on, so the newest are kept.
		taken = 0;
		queued = 0;
	}
	PerformMessage& m = queue[queued++];
	m.kind = kind;
	m.voice = voice;
	m.key = key;
	m.value = value;
	m.technique = technique;
	m.fret = fret;
}


bool Performer::next(PerformMessage& out) {
	if (taken >= queued) {
		taken = 0;
		queued = 0;
		return false;
	}
	out = queue[taken++];
	return true;
}


int Performer::voiceFor(const PerformNote& n) {
	// A STRING IS A VOICE. One is the highest string, and the voices are in that order, so the
	// arithmetic is the whole of the allocation.
	if (n.string > 0 && n.string <= PERFORM_VOICES)
		return n.string - 1;
	// An instrument with no strings: the voices past the ones the strings use, taken in turn,
	// oldest first — which for anything unfretted is what a note-per-voice allocator does.
	const int from = (stringCount > 0) ? stringCount : 0;
	int best = from;
	float oldest = -1.f;
	for (int i = from; i < PERFORM_VOICES; i++) {
		if (!plans[i].sounding)
			return i;
		const float left = plans[i].length - plans[i].age;
		if (oldest < 0.f || left < oldest) {
			oldest = left;
			best = i;
		}
	}
	return best;
}


void Performer::note(const PerformNote& n) {
	const int i = voiceFor(n);
	Plan& p = plans[i];

	// ---- what the note before it on this string asked for ----------------------------------
	//
	// A HAMMER-ON STRIKES NOTHING. The left hand sounds the note from the one already ringing,
	// so the string keeps going and its pitch moves; the same is true of a legato slide, which
	// the note before it declared. A shift slide DOES strike, a little softer.
	const bool ringing = p.sounding && p.struck;
	const bool hammered = (n.technique & (T_HAMMER_ON | T_PULL_OFF)) != 0;
	const bool slidInto = ringing && (p.technique & T_LEGATO_SLIDE) != 0;
	const bool noAttack = ringing && (hammered || slidInto);

	const float wasCents = ringing ? (voices[i].cents) : 0.f;
	const float wasVolts = ringing ? p.base : n.pitch;

	// ---- the level -------------------------------------------------------------------------
	float level = n.level;
	if (n.technique & T_HEAVY_ACCENT)
		level *= rules.heavyAccentLevel;
	else if (n.technique & T_ACCENT)
		level *= rules.accentLevel;
	if (n.technique & T_GHOST)
		level *= rules.ghostLevel;
	if (n.technique & T_PALM_MUTE)
		level *= rules.palmLevel;
	if (n.technique & T_DEAD_NOTE)
		level *= rules.deadLevel;
	if (n.technique & T_HARMONIC)
		level *= rules.harmonicLevel;
	if (hammered)
		level *= rules.hammerLevel;
	else if (n.technique & T_SHIFT_SLIDE)
		level *= rules.shiftSlideLevel;
	// THE HAND LEANS ON THE FIRST STRING IT REACHES, and less on each one after it; an upstroke
	// is lighter than a downstroke throughout.
	if (n.strum != 0) {
		level *= 1.f - rules.strumLevelFall * (float) n.strumIndex;
		if (n.strum < 0)
			level *= 1.f - rules.strumUpExtraFall;
	}
	if (n.grace != G_NONE)
		level *= rules.ghostLevel;
	level *= 1.f + rules.levelVary * human * noise();
	level = clamp(level, 0.f, 1.f);

	// ---- how long it sounds ------------------------------------------------------------------
	float length = n.seconds;
	if (n.technique & T_DEAD_NOTE)
		length = rules.deadMs / 1000.f;
	else if (n.technique & T_PALM_MUTE) {
		length *= rules.palmLength;
		const float cap = rules.palmMaxMs / 1000.f;
		if (length > cap)
			length = cap;
	}
	if (n.technique & T_STACCATO)
		length *= rules.staccatoLength;
	if (n.technique & T_GHOST)
		length *= rules.ghostLength;
	// LET RING IS NOT A LENGTH. It sounds until its own string is struck again, which is what
	// the next note on this voice does; the cap is only so that nothing rings for ever.
	if (n.technique & T_LET_RING)
		length = rules.letRingMaxSeconds;
	length *= 1.f + rules.lengthVary * human * noise();
	if (length < 0.002f)
		length = 0.002f;

	// ---- brightness --------------------------------------------------------------------------
	float timbre = rules.openTimbre;
	if (n.technique & T_HARMONIC)
		timbre = rules.harmonicTimbre;
	if (n.technique & T_PALM_MUTE)
		timbre = rules.palmTimbre;
	if (n.technique & T_DEAD_NOTE)
		timbre = rules.deadTimbre;

	// ---- when it speaks ----------------------------------------------------------------------
	//
	// A BRUSHED CHORD IS ONE MOVEMENT OF THE HAND, so each string speaks a little after the one
	// before it. The first is on the beat, which is where the chord is felt to be.
	float delay = 0.f;
	if (n.strum != 0) {
		float stroke = (n.strumMs > 0) ? (float) n.strumMs
			: ((n.strum > 0) ? rules.strumDownMs : rules.strumUpMs);
		delay = stroke / 1000.f * (float) n.strumIndex;
	}
	delay += rules.timingMs / 1000.f * human * noise();
	if (delay < 0.f)
		delay = 0.f;

	// ---- the plan ----------------------------------------------------------------------------
	const bool wasLetRing = ringing && (p.technique & T_LET_RING) != 0;
	if (ringing && !noAttack) {
		// STRING MONOPHONY: the note that was sounding stops, whatever it asked for. A real
		// string cannot sound two notes, and let ring means until this moment and no longer.
		push(PerformMessage::RELEASE, i, p.key, 0.f);
	}
	(void) wasLetRing;

	Plan next;
	next.sounding = true;
	next.age = 0.f;
	next.length = length;
	next.base = n.pitch;
	next.level = level;
	next.timbre = timbre;
	next.startDelay = delay;
	next.struck = false;
	next.key = (int) std::lround(60.f + 12.f * n.pitch);
	next.handle = n.handle;
	next.pan = n.pan;
	next.technique = n.technique;
	next.fret = n.fret;
	next.vibrato = n.vibrato;
	next.bendCount = (n.bendCount > 4) ? 4 : n.bendCount;
	for (int k = 0; k < next.bendCount; k++) {
		next.bendAt[k] = n.bendAt[k];
		next.bendCents[k] = n.bendCents[k];
	}

	// A slide INTO the note: it starts away from the pitch and arrives at it. Without knowing
	// the future it cannot begin before the note does, so it arrives a little after instead.
	if (n.technique & T_SLIDE_IN_BELOW)
		next.slideInCents = -rules.slideInSemitones * 100.f;
	else if (n.technique & T_SLIDE_IN_ABOVE)
		next.slideInCents = rules.slideInSemitones * 100.f;

	if (noAttack) {
		// The string is already sounding, so what changes is where its pitch is going. The
		// distance is measured from where the pitch actually IS, bend and all, so a hammer-on
		// out of a bent note does not jump.
		next.struck = true;
		next.startDelay = 0.f;
		const float from = (wasVolts - n.pitch) * 1200.f + wasCents;
		next.glideFrom = from;
		next.glideSpan = (slidInto && !hammered)
			? std::fmin(p.length * rules.slideFraction, rules.slideMaxMs / 1000.f)
			: rules.hammerGlideMs / 1000.f;
		if (next.glideSpan < 0.001f)
			next.glideSpan = 0.001f;
		next.glideLeft = next.glideSpan;
		// The note is not struck again, so its loudness is what the string still has, taken
		// down a little: a hammered note is weaker than a picked one.
		next.level = clamp(voices[i].level * rules.hammerLevel, 0.f, 1.f);
		push(PerformMessage::LEVEL, i, next.key, next.level, n.technique, n.fret);
	}

	p = next;
	voices[i].string = i + 1;
	voices[i].handle = n.handle;
}


/** Where one voice's pitch, level and brightness have got to. */
void Performer::shape(Plan& p, float dt, int index) {
	if (!p.sounding)
		return;

	// Waiting to be struck: a string of a brushed chord, or the timing moved a little late.
	if (!p.struck) {
		p.startDelay -= dt;
		if (p.startDelay > 0.f)
			return;
		p.struck = true;
		push(PerformMessage::ATTACK, index, p.key, p.level, p.technique, p.fret);
		push(PerformMessage::TIMBRE, index, p.key, p.timbre);
	}

	p.age += dt;

	float cents = 0.f;
	const float through = (p.length > 0.f) ? clamp(p.age / p.length, 0.f, 1.f) : 1.f;

	// The bend the source asked for, as a curve through its points.
	if (p.bendCount > 0)
		cents += bendAt(p.bendAt, p.bendCents, p.bendCount, through);

	// A slide into the note, decaying away over its own time.
	if (p.slideInCents != 0.f) {
		const float span = rules.slideInMs / 1000.f;
		const float left = (span > 0.f) ? clamp(1.f - p.age / span, 0.f, 1.f) : 0.f;
		const float eased = 0.5f - 0.5f * std::cos(left * 3.14159265f);
		cents += p.slideInCents * eased;
	}

	// A glide the note before it began: a hammer-on's eight milliseconds, or a legato slide's
	// longer movement.
	if (p.glideLeft > 0.f) {
		p.glideLeft -= dt;
		const float left = clamp(p.glideLeft / p.glideSpan, 0.f, 1.f);
		const float eased = 0.5f - 0.5f * std::cos(left * 3.14159265f);
		cents += p.glideFrom * eased;
	}

	// Vibrato, which comes in rather than being there from the first moment. The performer makes
	// it: a source saying "wide vibrato" has said everything it can usefully say.
	if (p.vibrato != V_NONE) {
		const float delay = rules.vibratoDelayMs / 1000.f;
		const float fade = rules.vibratoFadeMs / 1000.f;
		if (p.age > delay) {
			const float in = (fade > 0.f) ? clamp((p.age - delay) / fade, 0.f, 1.f) : 1.f;
			const float depth = (p.vibrato == V_WIDE)
				? rules.vibratoWideCents : rules.vibratoSlightCents;
			cents += depth * in
				* std::sin(6.2831853f * rules.vibratoHz * (p.age - delay));
		}
	}

	float level = p.level;

	// A slide out of the note, over its last stretch, fading as it goes.
	const float outSpan = rules.slideOutMs / 1000.f;
	if ((p.technique & (T_SLIDE_OUT_DOWN | T_SLIDE_OUT_UP)) && p.length > outSpan) {
		const float from = p.length - outSpan;
		if (p.age > from) {
			const float part = clamp((p.age - from) / outSpan, 0.f, 1.f);
			const float semis = (p.technique & T_SLIDE_OUT_DOWN)
				? -rules.slideOutDownSemitones : rules.slideOutUpSemitones;
			cents += semis * 100.f * part;
			level *= 1.f - (1.f - rules.slideOutFade) * part;
		}
	}

	// Tremolo picking: the level pulses at the picking rate rather than the note being struck
	// again and again, which would be a different note on the cable every time.
	if (p.technique & T_TREMOLO) {
		const float phase = std::sin(6.2831853f * rules.tremoloHz * p.age);
		level *= 1.f - rules.tremoloLevelVary + rules.tremoloLevelVary * phase;
	}

	PerformVoice& v = voices[index];
	v.gate = true;
	v.handle = p.handle;
	v.cents = cents;
	v.pitch = p.base + centsToVolts(cents);
	v.level = level;
	v.timbre = p.timbre;
	v.pan = p.pan;

	// A MESSAGE ONLY WHEN SOMETHING MOVED. A renderer that is told the same number sixty
	// thousand times a second is being told nothing.
	if (!p.everSent || std::fabs(cents - p.sentCents) > 0.5f) {
		push(PerformMessage::BEND, index, p.key, cents);
		p.sentCents = cents;
	}
	if (!p.everSent || std::fabs(level - p.sentLevel) > 0.004f) {
		push(PerformMessage::LEVEL, index, p.key, level);
		p.sentLevel = level;
	}
	if (!p.everSent || std::fabs(p.timbre - p.sentTimbre) > 0.004f) {
		push(PerformMessage::TIMBRE, index, p.key, p.timbre);
		p.sentTimbre = p.timbre;
	}
	p.everSent = true;

	if (p.age >= p.length) {
		push(PerformMessage::RELEASE, index, p.key, 0.f);
		p.sounding = false;
		v.gate = false;
		v.level = 0.f;
	}
}


void Performer::advance(float dt) {
	for (int i = 0; i < PERFORM_VOICES; i++)
		shape(plans[i], dt, i);
}


} // namespace px
