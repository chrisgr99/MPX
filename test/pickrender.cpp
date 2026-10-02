/** The finger picker through the performer and the modelled guitar, written to a file to be
listened to: C, A minor, F and G, a bar each, twice, at 96 beats a minute, once in each pattern,
each to its own file.

	make pickrender
*/
#include "../src/FingerPicking.hpp"
#include "../src/Perform.hpp"
#include "../src/GuitarModel.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;

static const float RATE = 48000.f;

static ChordNotes triad(int root, bool minor) {
	ChordNotes c;
	const int iv[3] = {0, minor ? 3 : 4, 7};
	for (int i = 0; i < 3; i++) {
		c.pc[i] = (root + iv[i]) % 12;
		c.essential[i] = i < 2;
	}
	c.count = 3;
	c.bass = root;
	return c;
}

int main() {
	const ChordNotes bars[4] = {triad(0, false), triad(9, true), triad(5, false), triad(7, false)};
	const float bpm = 96.f;
	const float stepSeconds = 30.f / bpm;

	for (int pattern = 0; pattern < NUM_PICK_PATTERNS; pattern++) {
	FingerPicker picker;
	picker.pattern = pattern;
	Performer performer;
	performer.strings(6);
	performer.humanise(1.f);
	GuitarModel g;
	g.setSampleRate(RATE);
	for (int s = 0; s < 6; s++) {
		g.open[s] = (picker.tuning.open[s] - 60) / 12.f;
		g.openKnown[s] = true;
		g.setPitch(s, g.open[s]);
	}
	g.settings.body = 0.7f;
	g.settings.sympathy = 0.5f;
	g.settings.fingerNoise = 0.3f;
	g.settings.taper = 0.5f;
	g.settings.fretDamping = 0.5f;

	std::vector<float> l, r;
	int64_t handle = 1;
	int64_t ringing[6] = {};
	int ringingFret[6] = {};
	auto run = [&](float seconds) {
		for (int i = 0; i < (int) (seconds * RATE); i++) {
			performer.advance(1.f / RATE);
			for (int s = 0; s < 6; s++)
				g.setPitch(s, performer.voice(s).pitch);
			PerformMessage m;
			while (performer.next(m)) {
				if (m.kind == PerformMessage::ATTACK)
					g.strike(m.voice, m.value, m.technique, m.fret);
				else if (m.kind == PerformMessage::LEVEL)
					m.technique ? g.legato(m.voice, m.value, m.technique) : g.setLevel(m.voice, m.value);
				else if (m.kind == PerformMessage::RELEASE)
					g.end(m.voice);
			}
			float a = 0.f, b = 0.f;
			g.process(&a, &b);
			l.push_back(a * 0.6f);
			r.push_back(b * 0.6f);
		}
	};
	auto stop = [&](int s) {
		if (!ringing[s])
			return;
		for (int i = 0; i < performer.voiceCount(); i++)
			if (performer.voice(i).gate && performer.voice(i).handle == ringing[s])
				performer.silenceVoice(i);
		ringing[s] = 0;
	};

	// The module's default settings, and a phrase of four bars.
	picker.variation.accent = 0.5f;
	picker.variation.voicing = 0.3f;
	picker.variation.picking = 0.25f;
	picker.variation.ornament = 0.25f;
	const uint32_t seed = 12345u;
	const int cMajor = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11);
	auto send = [&](const PickedNote& p) {
		// As the module: the thumb off the shape's bass string stops the other bass strings.
		if (p.finger == THUMB && p.string != picker.shape().bassString(picker.tuning))
			for (int s = BASS_STRING_FIRST; s < 6; s++)
				if (s != p.string)
					stop(s);
		PerformNote pn;
		pn.handle = handle++;
		pn.pitch = (p.note - 60) / 12.f;
		pn.level = p.level;
		pn.seconds = 8.f;
		pn.string = p.string + 1;
		pn.fret = p.fret;
		pn.technique = (1u << 8) | p.technique;   // let ring
		performer.note(pn);
		ringing[p.string] = pn.handle;
		ringingFret[p.string] = p.fret;
	};
	const int BARS = 16;
	for (int bar = 0; bar <= BARS; bar++) {
		const bool last = bar == BARS;
		// As the module: a string the new shape frets where it rings rings on; an open treble
		// string rings on unless the new shape frets it; an open bass string rings on if it is a
		// tone of the new chord; anything else is stopped.
		picker.setChord(bars[bar % 4], pickChance(seed, bar * 8, 100));
		for (int s = 0; s < 6; s++) {
			if (!ringing[s])
				continue;
			const Shape& now = picker.shape();
			if (now.fret[s] == ringingFret[s])
				continue;
			if (ringingFret[s] == 0 && s < BASS_STRING_FIRST && now.fret[s] <= 0)
				continue;
			const int pc = picker.tuning.open[s] % 12;
			if (ringingFret[s] == 0 && (bars[bar % 4].contains(pc) || bars[bar % 4].bass == pc)
					&& now.fret[s] <= 0)
				continue;
			stop(s);
		}
		for (int step = 0; step < (last ? 1 : 8); step++) {
			PickContext at;
			at.step = step;
			at.clock = bar * 8 + step;
			at.seed = seed;
			at.chordStart = step == 0;
			at.stepsToChange = last ? -1 : 8 - step;
			at.nextBass = bars[(bar + 1) % 4].bass;
			at.phraseStepsLeft = (4 - bar % 4) * 8 - step;
			at.motif = (uint32_t) (bar / 4) + 1;
			Shape coming;
			if (!last && step == 7 && picker.preview(bars[(bar + 1) % 4],
					pickChance(seed, (bar + 1) * 8, 100), &coming))
				at.nextShape = &coming;
			at.scaleMask = cMajor;
			PickedNote notes[MAX_STEP_NOTES];
			const int n = picker.notesAt(at, notes);
			float later = 0.f;
			for (int i = 0; i < n; i++) {
				if (notes[i].delay <= 0.f)
					send(notes[i]);
				else
					later = notes[i].delay;
			}
			if (later > 0.f) {
				run(stepSeconds * later);
				for (int i = 0; i < n; i++)
					if (notes[i].delay > 0.f)
						send(notes[i]);
				run(stepSeconds * (1.f - later));
			}
			else
				run(stepSeconds);
		}
	}
	run(3.f);

	static const char* NAMES[] = {"forward-roll", "pinch-and-roll", "thumb-and-pinch",
		"broken-arpeggio", "arpeggio-eighths"};
	const std::string path = std::string("build/picker-") + NAMES[pattern % 5] + ".wav";
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return 1;
	const uint32_t frames = (uint32_t) l.size(), bytes = frames * 4;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(2); u32((uint32_t) RATE); u32((uint32_t) RATE * 4); u16(4);
	u16(16); std::fwrite("data", 1, 4, f); u32(bytes);
	float peak = 0.f;
	for (uint32_t i = 0; i < frames; i++) {
		peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
		const int16_t a = (int16_t) std::lround(std::fmax(-1.f, std::fmin(1.f, l[i])) * 32000.f);
		const int16_t b = (int16_t) std::lround(std::fmax(-1.f, std::fmin(1.f, r[i])) * 32000.f);
		std::fwrite(&a, 2, 1, f);
		std::fwrite(&b, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s, peak %.2f\n", path.c_str(), peak);
	}
	return 0;
}
