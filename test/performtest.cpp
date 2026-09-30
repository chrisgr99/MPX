/** What each articulation does, one at a time.

WHY THERE IS A TEST FOR THIS AND NOT A PATCH. Every number the performer uses is meant to be
tuned by ear, which means the rules file will be edited often — and a rule edited by ear is a rule
nobody checked. So each articulation is played here on its own, with the humanising turned off so
the answer is a number rather than a range, and what is checked is the thing the rule is FOR: a
hammer-on strikes nothing, a palm mute is short and dark, a bend arrives where it was sent, a
brushed chord speaks low to high.

It does not check that any of it sounds good. That is what ears are for; this only says that what
was asked for is what happened.

    make performtest
*/
#include "../src/Perform.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;


static int failures = 0;

static void ok(const char* what, bool passed, const std::string& saw = "") {
	if (!passed)
		failures++;
	std::printf("%-52s %s\n", what, passed ? "yes" : "NO");
	if (!passed && !saw.empty())
		std::printf("%-52s   %s\n", "", saw.c_str());
}

static std::string num(const char* label, double v) {
	char buf[96];
	std::snprintf(buf, sizeof(buf), "%s %.3f", label, v);
	return buf;
}


/** The techniques, by the same numbers the cable uses. */
enum {
	HAMMER_ON = 1u << 0, PULL_OFF = 1u << 1, LEGATO_SLIDE = 1u << 2, SHIFT_SLIDE = 1u << 3,
	SLIDE_IN_BELOW = 1u << 4, SLIDE_IN_ABOVE = 1u << 5,
	SLIDE_OUT_DOWN = 1u << 6, SLIDE_OUT_UP = 1u << 7,
	LET_RING = 1u << 8, PALM_MUTE = 1u << 9, DEAD_NOTE = 1u << 10, GHOST = 1u << 11,
	ACCENT = 1u << 12, HEAVY_ACCENT = 1u << 13, HARMONIC = 1u << 14,
	ARTIFICIAL_HARMONIC = 1u << 15, TAPPED = 1u << 16, STACCATO = 1u << 17, TREMOLO = 1u << 18,
};


/** A performer with the humanising off, so every answer is exact. */
struct Rig {
	Performer p;
	std::vector<PerformMessage> messages;

	Rig() {
		p.humanise(0.f);
		p.strings(6);
		p.silence();
		drain();
	}

	PerformNote plain(float volts, int string, float seconds = 0.5f) {
		PerformNote n;
		static int64_t handle = 1;
		n.handle = handle++;
		n.pitch = volts;
		n.string = string;
		n.seconds = seconds;
		n.level = 0.6f;
		return n;
	}

	void play(const PerformNote& n) { p.note(n); }

	/** Moves time on in small steps, as a host would, keeping every message. */
	void run(float seconds, float step = 1.f / 48000.f * 64.f) {
		for (float t = 0.f; t < seconds; t += step) {
			p.advance(step);
			drain();
		}
	}

	void drain() {
		PerformMessage m;
		while (p.next(m))
			messages.push_back(m);
	}

	int count(uint8_t kind, int voice = -1) const {
		int n = 0;
		for (size_t i = 0; i < messages.size(); i++) {
			if (messages[i].kind == kind && (voice < 0 || messages[i].voice == voice))
				n++;
		}
		return n;
	}

	void forget() { messages.clear(); }
};


int main() {
	// ---- a plain note ------------------------------------------------------------------------
	{
		Rig r;
		r.play(r.plain(0.f, 3, 0.25f));
		r.run(0.1f);
		ok("a note sounds", r.p.voice(2).gate && r.p.voice(2).level > 0.5f);
		ok("on the voice its string names", r.p.voice(2).gate && !r.p.voice(0).gate);
		ok("struck once", r.count(PerformMessage::ATTACK) == 1);
		r.run(0.3f);
		ok("and stops at its own length", !r.p.voice(2).gate
			&& r.count(PerformMessage::RELEASE) == 1);
	}

	// ---- a hammer-on strikes nothing ---------------------------------------------------------
	{
		Rig r;
		r.play(r.plain(0.f, 3, 1.f));
		r.run(0.2f);
		r.forget();
		PerformNote up = r.plain(2.f / 12.f, 3, 1.f);   // Two semitones higher, same string.
		up.technique = HAMMER_ON;
		r.play(up);
		r.run(0.05f);
		ok("a hammer-on strikes nothing", r.count(PerformMessage::ATTACK) == 0);
		ok("and does not stop the string", r.count(PerformMessage::RELEASE) == 0);
		ok("the pitch arrives at the new note",
			std::fabs(r.p.voice(2).pitch - 2.f / 12.f) < 0.001f,
			num("volts", r.p.voice(2).pitch));
		ok("weaker than a picked note", r.p.voice(2).level < 0.6f,
			num("level", r.p.voice(2).level));
	}

	// ---- a hammer-on out of a bent note does not jump ----------------------------------------
	{
		Rig r;
		PerformNote bent = r.plain(0.f, 3, 1.f);
		bent.bendCount = 2;
		bent.bendAt[0] = 0.f;   bent.bendCents[0] = 0.f;
		bent.bendAt[1] = 0.2f;  bent.bendCents[1] = 200.f;
		r.play(bent);
		r.run(0.3f);                       // The bend has arrived: a whole tone up.
		const float before = r.p.voice(2).pitch;
		PerformNote up = r.plain(4.f / 12.f, 3, 1.f);
		up.technique = HAMMER_ON;
		r.play(up);
		r.p.advance(1.f / 750.f);
		ok("a hammer-on starts from where the pitch IS",
			std::fabs(r.p.voice(2).pitch - before) < 0.02f,
			num("moved by volts", std::fabs(r.p.voice(2).pitch - before)));
		r.run(0.05f);
		ok("and still arrives at the new note",
			std::fabs(r.p.voice(2).pitch - 4.f / 12.f) < 0.002f,
			num("volts", r.p.voice(2).pitch));
	}

	// ---- a second note on the same string stops the first ------------------------------------
	{
		Rig r;
		PerformNote ring = r.plain(0.f, 3, 0.5f);
		ring.technique = LET_RING;
		r.play(ring);
		r.run(0.2f);
		ok("let ring is still sounding past its length", r.p.voice(2).gate);
		r.forget();
		r.play(r.plain(5.f / 12.f, 3, 0.5f));
		r.run(0.02f);
		ok("and ends when its own string is struck again",
			r.count(PerformMessage::RELEASE) == 1 && r.count(PerformMessage::ATTACK) == 1);
	}

	// ---- another string is untouched ---------------------------------------------------------
	{
		Rig r;
		PerformNote a = r.plain(0.f, 3, 1.f);
		a.technique = LET_RING;
		PerformNote b = r.plain(7.f / 12.f, 2, 1.f);
		b.bendCount = 2;
		b.bendAt[0] = 0.f;  b.bendCents[0] = 0.f;
		b.bendAt[1] = 0.3f; b.bendCents[1] = 200.f;
		r.play(a);
		r.play(b);
		r.run(0.4f);
		ok("a bend on one string leaves the others alone",
			std::fabs(r.p.voice(2).cents) < 0.5f && r.p.voice(1).cents > 150.f,
			num("the other string moved by cents", r.p.voice(2).cents));
	}

	// ---- a bend arrives where it was sent, and releases ---------------------------------------
	{
		Rig r;
		PerformNote n = r.plain(0.f, 1, 1.f);
		n.bendCount = 3;
		n.bendAt[0] = 0.f;   n.bendCents[0] = 0.f;
		n.bendAt[1] = 0.4f;  n.bendCents[1] = 200.f;
		n.bendAt[2] = 0.8f;  n.bendCents[2] = 0.f;
		r.play(n);
		r.run(0.42f);
		ok("a bend reaches its height", std::fabs(r.p.voice(0).cents - 200.f) < 12.f,
			num("cents", r.p.voice(0).cents));
		r.run(0.4f);
		ok("and comes back down", std::fabs(r.p.voice(0).cents) < 12.f,
			num("cents", r.p.voice(0).cents));
	}

	// ---- palm mute, dead notes, staccato, ghost ----------------------------------------------
	{
		Rig r;
		PerformNote n = r.plain(0.f, 6, 1.f);
		n.technique = PALM_MUTE;
		r.play(n);
		r.run(0.1f);
		ok("a palm mute is dark", r.p.voice(5).timbre < 0.3f, num("timbre", r.p.voice(5).timbre));
		r.run(0.1f);
		ok("and short", !r.p.voice(5).gate);
	}
	{
		Rig r;
		PerformNote n = r.plain(0.f, 6, 1.f);
		n.technique = DEAD_NOTE;
		r.play(n);
		r.run(0.01f);
		ok("a dead note sounds", r.p.voice(5).gate);
		ok("and is dark", r.p.voice(5).timbre < 0.15f, num("timbre", r.p.voice(5).timbre));
		r.run(0.04f);
		ok("and is over in a few hundredths", !r.p.voice(5).gate);
	}
	{
		Rig r;
		PerformNote n = r.plain(0.f, 4, 0.4f);
		n.technique = STACCATO;
		r.play(n);
		r.run(0.21f);
		ok("staccato halves the note", !r.p.voice(3).gate);
	}
	{
		Rig r;
		PerformNote plain = r.plain(0.f, 4, 0.4f);
		r.play(plain);
		r.run(0.05f);
		const float loud = r.p.voice(3).level;
		Rig q;
		PerformNote ghost = q.plain(0.f, 4, 0.4f);
		ghost.technique = GHOST;
		q.play(ghost);
		q.run(0.05f);
		ok("a ghost note is quieter", q.p.voice(3).level < loud * 0.7f,
			num("against", loud));
	}

	// ---- accents ------------------------------------------------------------------------------
	{
		Rig r;
		PerformNote n = r.plain(0.f, 4, 0.4f);
		n.level = 0.5f;
		n.technique = ACCENT;
		r.play(n);
		r.run(0.05f);
		const float accented = r.p.voice(3).level;
		Rig q;
		PerformNote h = q.plain(0.f, 4, 0.4f);
		h.level = 0.5f;
		h.technique = HEAVY_ACCENT;
		q.play(h);
		q.run(0.05f);
		ok("an accent is louder than the note asked for", accented > 0.5f,
			num("level", accented));
		ok("and a heavy accent louder still", q.p.voice(3).level > accented,
			num("level", q.p.voice(3).level));
	}

	// ---- a brushed chord speaks in order ------------------------------------------------------
	{
		Rig r;
		// A downstroke: the lowest string first, which is index three of four in the stroke.
		for (int s = 6; s >= 3; s--) {
			PerformNote n = r.plain((float) (s - 6) / 12.f, s, 1.f);
			n.strum = 1;
			n.strumIndex = (uint8_t) (6 - s);
			n.strumMs = 20;
			r.play(n);
		}
		r.p.advance(0.001f);
		r.drain();
		ok("the first string of a downstroke is on the beat", r.p.voice(5).gate);
		ok("and the last has not spoken yet", !r.p.voice(2).gate);
		r.run(0.08f);
		ok("by the end of the stroke they all have",
			r.p.voice(5).gate && r.p.voice(4).gate && r.p.voice(3).gate && r.p.voice(2).gate);
		ok("and the hand leaned on the first",
			r.p.voice(5).level > r.p.voice(2).level,
			num("first against last", r.p.voice(5).level - r.p.voice(2).level));
	}

	// ---- a slide into a note, and out of one --------------------------------------------------
	{
		Rig r;
		PerformNote n = r.plain(0.f, 3, 1.f);
		n.technique = SLIDE_IN_BELOW;
		r.play(n);
		r.p.advance(0.002f);
		ok("a slide in starts below the note", r.p.voice(2).cents < -100.f,
			num("cents", r.p.voice(2).cents));
		r.run(0.1f);
		ok("and arrives at it", std::fabs(r.p.voice(2).cents) < 5.f,
			num("cents", r.p.voice(2).cents));
	}
	{
		Rig r;
		PerformNote n = r.plain(0.f, 3, 0.6f);
		n.technique = SLIDE_OUT_DOWN;
		r.play(n);
		r.run(0.3f);
		ok("a slide out has not begun in the middle of the note",
			std::fabs(r.p.voice(2).cents) < 5.f, num("cents", r.p.voice(2).cents));
		r.run(0.28f);
		ok("and falls away by the end", r.p.voice(2).cents < -200.f,
			num("cents", r.p.voice(2).cents));
	}

	// ---- a legato slide, which the note before it declares -----------------------------------
	{
		Rig r;
		PerformNote from = r.plain(0.f, 3, 0.4f);
		from.technique = LEGATO_SLIDE;
		r.play(from);
		r.run(0.2f);
		r.forget();
		r.play(r.plain(5.f / 12.f, 3, 0.4f));
		r.run(0.01f);
		ok("a legato slide strikes nothing when it lands",
			r.count(PerformMessage::ATTACK) == 0);
		ok("and is on its way rather than arrived",
			std::fabs(r.p.voice(2).pitch - 5.f / 12.f) > 0.02f,
			num("volts", r.p.voice(2).pitch));
		r.run(0.3f);
		ok("then arrives", std::fabs(r.p.voice(2).pitch - 5.f / 12.f) < 0.002f,
			num("volts", r.p.voice(2).pitch));
	}

	// ---- vibrato --------------------------------------------------------------------------
	{
		Rig r;
		PerformNote n = r.plain(0.f, 1, 2.f);
		n.vibrato = 2;                     // Wide.
		r.play(n);
		r.run(0.05f);
		ok("vibrato waits before it starts", std::fabs(r.p.voice(0).cents) < 1.f,
			num("cents", r.p.voice(0).cents));
		float most = 0.f;
		for (int i = 0; i < 400; i++) {
			r.p.advance(0.002f);
			most = std::fmax(most, std::fabs(r.p.voice(0).cents));
		}
		ok("then moves the pitch about", most > 30.f && most < 70.f, num("cents", most));
	}

	// ---- an instrument with no strings --------------------------------------------------------
	{
		Rig r;
		r.p.strings(0);
		for (int i = 0; i < 4; i++)
			r.play(r.plain((float) i / 12.f, 0, 1.f));
		r.run(0.02f);
		int sounding = 0;
		for (int i = 0; i < r.p.voiceCount(); i++)
			sounding += r.p.voice(i).gate ? 1 : 0;
		ok("four notes with no string get four voices", sounding == 4,
			num("sounding", sounding));
	}

	// ---- the rules file ----------------------------------------------------------------------
	{
		PerformRules rules;
		const std::string written = performRulesWrite(rules);
		PerformRules read;
		std::string complaint;
		const int set = performRulesRead(written, read, &complaint);
		ok("every rule written is read back", set > 30 && complaint.empty(),
			complaint.empty() ? num("rules", set) : complaint);
		ok("and comes back the same",
			read.palmLength == rules.palmLength && read.vibratoHz == rules.vibratoHz);

		PerformRules edited;
		std::string why;
		performRulesRead("# a comment\npalmLength = 0.2   # and a trailing one\n"
			"noSuchRule = 4\n", edited, &why);
		ok("a rule is set from a file", std::fabs(edited.palmLength - 0.2f) < 0.0001f,
			num("palmLength", edited.palmLength));
		ok("and an unknown name is passed over rather than refused",
			why.find("noSuchRule") != std::string::npos, why);
	}

	std::printf("\n%s\n", failures ? "SOMETHING IS WRONG" : "all of them");
	return failures ? 1 : 0;
}
