/** Plays a patch's chart through mpxPhrase and mpxVoice and says, in words, what comes out.

  make phrasesim                                  the patch in Downloads, its own chart
  make phrasesim EXAMPLE=1                        the same settings, the first example
  make phrasesim PATCH=path/to/patch.vcv EXAMPLE=3 NOTES=1

WHY. Listening is the final test and a slow one, and a fault a listener can only call "not
musical" is often something a printout names at once: a three-beat hole inside a group, a breath
filled in by a restatement, a phrase that lands on the wrong degree. This runs the same code the
modules run — the chart's phrasing, the phrase generator, the voice's choice of note — on the
settings saved in a patch, so a change can be checked against what a listener would hear before
anybody listens.

WHAT IT DOES NOT SIMULATE: the voice's ARTICULATION, ACCENT and BREATH, which alter lengths,
levels and rests after the notes are chosen; and more than one voice. The timings are mpxPhrase's
and the pitches are the first voice's.

IN WORDS, not in symbols, so it reads aloud.
*/
#include "../src/IReal.hpp"
#include "../src/ChartLayout.hpp"
#include "../src/ChartExamples.hpp"
#include "../src/Phrasing.hpp"
#include "../src/PhraseParams.hpp"
#include "../src/Swing.hpp"
#include "../src/Melodic.hpp"
#include "../src/MelodyVoice.hpp"

#include <jansson.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace px;

static const char* NAMES[12] = {"C", "C sharp", "D", "E flat", "E", "F", "F sharp", "G",
	"A flat", "A", "B flat", "B"};

static Key gKey;

static std::string noteName(int midi) {
	const int pc = ((midi % 12) + 12) % 12;
	std::string name = NAMES[pc];
	// THE LEADING NOTE OF A MINOR KEY IS A SHARP: G sharp in A minor, not A flat.
	if (gKey.minor && pc == ((gKey.tonic + 11) % 12 + 12) % 12 && name.find("flat") != std::string::npos) {
		static const char* SHARPS[12] = {"B sharp", "C sharp", "C double sharp", "D sharp", "E",
			"E sharp", "F sharp", "F double sharp", "G sharp", "G double sharp", "A sharp", "B"};
		name = SHARPS[pc];
	}
	return name + std::to_string(midi / 12 - 1);
}

/** Which degree of the key a pitch class is, in words, or its distance from the tonic. */
static std::string degreeName(const Key& key, int pc) {
	int degrees[7];
	scalePitchClasses(key, degrees);
	static const char* WORDS[7] = {"the tonic", "the second", "the third", "the fourth",
		"the fifth", "the sixth", "the seventh"};
	for (int i = 0; i < 7; i++)
		if (degrees[i] == pc)
			return WORDS[i];
	// THE RAISED SEVENTH OF A MINOR KEY is outside its natural scale and inside its dominant
	// chord, which is exactly where a half cadence in minor puts it.
	if (key.minor && pc == ((key.tonic + 11) % 12 + 12) % 12)
		return "the raised seventh, the leading note";
	return "outside the key";
}

static std::string where(const ChartPlayback& pb, float beat) {
	int bar = 0;
	for (int b = 0; b < (int) pb.timeline.size(); b++)
		if (beat >= pb.timeline[b].startBeat - 0.001f)
			bar = b;
	const float inBar = beat - pb.timeline[bar].startBeat;
	char buf[64];
	// NAMED AS A MUSICIAN COUNTS: on the beat, on its "and", or a quarter either side. A swung
	// "and" arrives late, so anything between a third and two thirds of the way through is it.
	const float whole = std::floor(inBar + 0.001f);
	const float part = inBar - whole;
	const int b = (int) whole + 1;
	if (part < 0.05f)
		std::snprintf(buf, sizeof(buf), "bar %d beat %d", bar + 1, b);
	else if (part > 0.33f && part < 0.67f)
		std::snprintf(buf, sizeof(buf), "bar %d on the and of %d", bar + 1, b);
	else if (part <= 0.33f)
		std::snprintf(buf, sizeof(buf), "bar %d just after beat %d", bar + 1, b);
	else
		std::snprintf(buf, sizeof(buf), "bar %d just before beat %d", bar + 1, b + 1);
	return buf;
}

static std::string beatsWords(float beats) {
	char buf[48];
	if (std::fabs(beats - 0.5f) < 0.06f) return "half a beat";
	if (std::fabs(beats - 1.f) < 0.06f) return "a beat";
	std::snprintf(buf, sizeof(buf), "%.1f beats", beats);
	return buf;
}

struct Params {
	std::vector<float> phrase, voice;
	float tempo = 120.f, swing = 0.f;
	unsigned chartSeed = 0;
	float melodySeed = 0.f, melodyCycle = 4.f, melodyOwn = 0.f;
	std::string chunk;
	/** The section chosen on the chart, or nought for the whole chart. */
	char section = 0;
	/** The loop on the chart, as written bars, or -1. */
	int loopFirst = -1, loopLast = -1;
};

static std::vector<float> paramsOf(json_t* m, int count, float missing = 0.f) {
	std::vector<float> v(count, missing);
	json_t* ps = json_object_get(m, "params");
	size_t i;
	json_t* p;
	json_array_foreach(ps, i, p) {
		const int id = (int) json_integer_value(json_object_get(p, "id"));
		if (id >= 0 && id < count)
			v[id] = (float) json_number_value(json_object_get(p, "value"));
	}
	return v;
}

static bool readPatch(const char* path, Params& out) {
	json_error_t err;
	json_t* root = json_load_file(path, 0, &err);
	if (!root) {
		std::fprintf(stderr, "phrasesim: cannot read %s\n", path);
		return false;
	}
	json_t* mods = json_object_get(root, "modules");
	size_t i;
	json_t* m;
	json_array_foreach(mods, i, m) {
		const char* model = json_string_value(json_object_get(m, "model"));
		if (!model)
			continue;
		if (!std::strcmp(model, "mpxPhrase")) {
			out.phrase = paramsOf(m, PHP_LEN);
			// A PARAMETER THE PATCH PREDATES is marked, so its default is used rather than nought.
			const std::vector<float> probe = paramsOf(m, PHP_LEN, -99.f);
			if (probe[PHP_SHAPE] < -98.f)
				out.phrase[PHP_SHAPE] = -1.f;
			if (probe[PHP_MOTIF] < -98.f)
				out.phrase[PHP_MOTIF] = -1.f;
		}
		else if (!std::strcmp(model, "mpxMelodyVoice"))
			out.voice = paramsOf(m, 14, -1.f);
		else if (!std::strcmp(model, "mpxMelody")) {
			std::vector<float> v = paramsOf(m, 5);
			out.melodySeed = v[1];
			out.melodyCycle = v[3] > 0.f ? v[3] : 4.f;
			out.melodyOwn = v[4];
		}
		else if (!std::strcmp(model, "mpxChart")) {
			std::vector<float> v = paramsOf(m, 8);
			out.tempo = v[1];
			out.chartSeed = (unsigned) v[6];
			out.swing = v[7];
			json_t* data = json_object_get(m, "data");
			json_t* chunk = data ? json_object_get(data, "chunk") : NULL;
			if (chunk && json_is_string(chunk))
				out.chunk = json_string_value(chunk);
			json_t* loop = data ? json_object_get(data, "loop") : NULL;
			if (json_is_array(loop) && json_array_size(loop) == 2) {
				out.loopFirst = (int) json_integer_value(json_array_get(loop, 0));
				out.loopLast = (int) json_integer_value(json_array_get(loop, 1));
			}
			json_t* section = data ? json_object_get(data, "section") : NULL;
			if (section && json_is_string(section) && json_string_value(section)[0])
				out.section = json_string_value(section)[0];
		}
	}
	json_decref(root);
	return !out.phrase.empty();
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: phrasesim patch.json [example number] [notes]\n");
		return 2;
	}
	Params P;
	if (!readPatch(argv[1], P)) {
		std::fprintf(stderr, "phrasesim: no mpxPhrase in that patch\n");
		return 1;
	}
	const int example = argc > 2 ? std::atoi(argv[2]) : 0;
	const bool listNotes = argc > 3 && std::atoi(argv[3]) != 0;
	if (example > 0) {
		if (example > (int) chartExamples().size()) {
			std::fprintf(stderr, "phrasesim: there are %d examples\n", (int) chartExamples().size());
			return 1;
		}
		P.chunk = chartExampleChunk(chartExamples()[example - 1]);
	}
	if (P.chunk.empty()) {
		std::fprintf(stderr, "phrasesim: the patch's chart has no song loaded\n");
		return 1;
	}

	Song song = irealParseSong(P.chunk);
	gKey = song.key;
	std::vector<ChartBar> bars = chartLayout(song);
	ChartPlayback pb = chartPlayback(bars);
	// THE SECTION CHOSEN ON THE CHART, played as the chart plays it: on its own, renumbered.
	if (P.section && example <= 0) {
		pb = chartPlaybackForLabel(bars, pb, P.section);
		std::printf("Section %c only.\n", P.section);
	}
	std::vector<ChartPhrase> phrases = chartPhrases(bars, pb);
	// THE LOOP, as the chart plays it: the song's own phrases cut to the looped bars, counted as
	// the song counts them.
	size_t songPhrases = phrases.size();
	if (P.loopFirst >= 0 && example <= 0) {
		const ChartPlayback whole = chartPlayback(bars);
		const std::vector<ChartPhrase> all = chartPhrases(bars, whole);
		int from = -1;
		const ChartPlayback loop = chartPlaybackForBars(whole, P.loopFirst, P.loopLast, &from);
		if (!loop.timeline.empty()) {
			pb = loop;
			phrases = chartPhrasesInLoop(all, whole, loop, from);
			songPhrases = all.size();
			std::printf("Loop, bars %d to %d as written.\n", P.loopFirst + 1, P.loopLast + 1);
		}
	}
	std::vector<ChartChange> changes = chartChanges(bars, pb);

	PhraseControls c;
	const std::vector<float>& p = P.phrase;
	c.subdivision = (int) p[PHP_SUBDIVISION]; c.groupSeconds = p[PHP_GROUP]; c.vary = p[PHP_VARY];
	c.start = p[PHP_START]; c.ending = p[PHP_ENDING]; c.pause = p[PHP_PAUSE];
	c.phrasePause = p[PHP_PHRASE_PAUSE]; c.hold = p[PHP_HOLD]; c.silentPhrases = p[PHP_SILENT];
	c.density = p[PHP_DENSITY]; c.syncopation = p[PHP_SYNCOPATION]; c.onChanges = p[PHP_ONCHANGES];
	c.length = p[PHP_LENGTH]; c.dynamics = p[PHP_DYNAMICS]; c.variation = p[PHP_VARIATION];
	c.repeat = p[PHP_REPEAT]; c.cycle = (int) p[PHP_CYCLE]; c.sections = p[PHP_SECTIONS];
	c.elide = p[PHP_ELIDE]; c.triplets = p[PHP_TRIPLETS];
	// SHAPE came after the others, so a patch saved before it takes the default.
	if ((int) p.size() > PHP_SHAPE && p[PHP_SHAPE] >= 0.f)
		c.shape = p[PHP_SHAPE];
	// MOTIF likewise; SIM_MOTIF overrides it, to hear what a setting would do.
	if (p[PHP_MOTIF] >= 0.f)
		c.motif = p[PHP_MOTIF];
	else {
		PhraseControls song;
		phraseStyle(STYLE_SONG, song);
		c.motif = song.motif;
	}
	if (getenv("SIM_MOTIF"))
		c.motif = (float) atof(getenv("SIM_MOTIF"));

	VoiceSettings vs;
	const bool haveVoice = P.voice.size() >= 11;
	if (haveVoice) {
		vs.smooth = P.voice[0]; vs.lock = P.voice[1]; vs.centre = (int) P.voice[2];
		vs.span = (int) P.voice[3]; vs.leading = P.voice[6]; vs.scale = (int) P.voice[7];
		// REPEATED NOTES came after the role, so a patch saved before it has no value there and
		// takes the knob's default.
		vs.repeats = P.voice[11] >= 0.f ? P.voice[11] : 0.25f;
		vs.motif = P.voice[12] >= 0.f ? P.voice[12] : 0.5f;
		vs.contour = P.voice[13] >= 0.f ? P.voice[13] : 0.5f;
	}
	if (getenv("SIM_VOICE_MOTIF"))
		vs.motif = (float) atof(getenv("SIM_VOICE_MOTIF"));
	if (getenv("SIM_CONTOUR"))
		vs.contour = (float) atof(getenv("SIM_CONTOUR"));
	VoiceMemory memory;

	// THE TEMPO THE CHART IS REALLY RUNNING AT, when a clock drives it: SIM_TEMPO, in beats a
	// minute. The chart's own knob is ignored while its clock input is patched.
	if (const char* t = getenv("SIM_TEMPO"))
		P.tempo = (float) atof(t);
	const float bps = P.tempo / 60.f;
	float e8 = 1.f, s16 = 1.f;
	swingRatios(P.swing, P.tempo, e8, s16);
	std::printf("%s. %d bars and %d phrases, at %.0f beats a minute, swing ratio %.2f.\n",
		song.title.c_str(), (int) pb.timeline.size(), (int) phrases.size(), P.tempo, e8);
	if (!haveVoice)
		std::printf("There is no mpxVoice in the patch, so only the rhythm is described.\n");

	const bool lockPhrase = p[PHP_OWN_SEED] > 0.5f || P.chartSeed == 0;
	unsigned phraseSeed = lockPhrase ? (unsigned) p[PHP_SEED]
		: P.chartSeed + (unsigned) p[PHP_SEED];
	// ANOTHER SEED, for a census over many: SIM_SEED replaces the phrase seed and moves the
	// melody's by the same amount.
	if (const char* ss = getenv("SIM_SEED")) {
		phraseSeed = (unsigned) atoi(ss);
		P.melodySeed += (float) atoi(ss);
	}

	PhrasePattern prev, cur;
	bool havePrev = false;
	float prevBeats = 0.f;
	int prevCadence = 0;
	int line = -1, lineBefore = -1;
	for (size_t k = 0; k < phrases.size(); k++) {
		const ChartPhrase& ph = phrases[k];
		PhraseAsk ask;
		// THE WHOLE PHRASE, even where a loop hears only part of it; `base` is where it begins in
		// the loop's beats, before the loop does when the loop has cut into it.
		const float base = ph.fullStart;
		ask.phraseBeats = ph.fullEnd - ph.fullStart;
		auto heard = [&](const PhraseNote& note) {
			const float at = base + note.offset;
			return !ph.cut || (at >= ph.startBeat - 1e-3f && at < ph.endBeat - 1e-3f);
		};
		ask.barBeats = (float) (bars.empty() ? 4 : bars[pb.timeline[ph.startBar].bar].beats);
		ask.beatsPerSecond = bps;
		ask.swingEighth = e8;
		ask.swingSixteenth = s16;
		ask.cadence = ph.cadence;
		std::vector<float> ch(ph.changes.begin(), ph.changes.end());
		ask.changes = ch.empty() ? NULL : ch.data();
		ask.changeCount = (int) ch.size();
		ask.seed = phraseSeed;
		const int number = ph.number >= 0 ? ph.number : (int) k;
		const int position = number % std::max(1, c.cycle);
		ask.cyclePosition = position;
		ask.previous = (havePrev && position != 0) ? &prev : NULL;
		// THE PICKUP'S ROOM, as mpxPhrase works it out two beats before this phrase begins: the
		// silence the phrase before left at its end.
		ask.pickupRoom = havePrev ? phrasePickupRoom(prev, prevBeats, c.subdivision) : 0.f;
		ask.leadKind = havePrev ? prev.nextLead : -1;
		ask.phraseInSection = ph.phraseInSection;
		ask.contrasting = ph.section != 0 && ph.section != 'A';
		ask.intoForm = (float) number / (float) std::max<size_t>(1, songPhrases);
		ask.leadBeats = havePrev ? prev.nextLeadBeats : 0.f;
		phraseGenerate(ask, c, cur);
		if (getenv("SIM_DEBUG"))
			std::printf("[room %.2f, start kind %d]\n", ask.pickupRoom, cur.startKind[0]);

		std::printf("\nPhrase %zu, bars %d to %d, ends on %s cadence.%s\n", k + 1,
			ph.startBar + 1, ph.endBar,
			ph.cadence == CADENCE_NONE ? "no" :
			(std::string(ph.cadence == CADENCE_AUTHENTIC ? "an " : "a ")
				+ chartCadenceName(ph.cadence)).c_str(),
			cur.silent ? " It is left silent." : "");

		{
			int pick = 0;
			while (pick < cur.noteCount && cur.notes[pick].offset < -1e-4f)
				pick++;
			if (pick > 0)
				std::printf("  It begins with a pickup of %d note%s, %s before its first bar line.\n",
					pick, pick == 1 ? "" : "s", beatsWords(-cur.notes[0].offset).c_str());
		}

		int landing = -1;
		for (int g = 0; g < cur.groupCount; g++) {
			int n = 0;
			float firstOn = 1e9f, lastOff = 0.f, hole = 0.f, prevEnd = -1.f;
			for (int i = 0; i < cur.noteCount; i++) {
				const PhraseNote& note = cur.notes[i];
				if (note.group != g || !heard(note))
					continue;
				n++;
				firstOn = std::min(firstOn, note.offset);
				lastOff = std::max(lastOff, note.offset + note.duration);
				if (prevEnd >= 0.f)
					hole = std::max(hole, note.offset - prevEnd);
				prevEnd = note.offset + note.duration;
			}
			if (n == 0)
				continue;
			const float next = (g + 1 < cur.groupCount) ? cur.starts[g + 1] : ask.phraseBeats;
			const float silence = std::max(0.f, std::min(next, next) - lastOff);
			std::printf("  Group %d, %s to %s: %d notes over %.1f seconds, then %.1f seconds of "
				"silence.", g + 1, where(pb, base + firstOn).c_str(),
				where(pb, base + lastOff).c_str(), n, (lastOff - firstOn) / bps,
				silence / bps);
			if (hole > 0.05f)
				std::printf(" The longest gap inside it is %s.", beatsWords(hole).c_str());
			std::printf("\n");

			for (int i = 0; i < cur.noteCount; i++) {
				const PhraseNote& note = cur.notes[i];
				if (note.group != g || !heard(note))
					continue;
				int pitch = -1;
				if (haveVoice) {
					// THE HARMONY AT THIS NOTE, as the chart publishes it.
					const float at = base + note.offset;
					size_t ci = 0;
					for (size_t j = 0; j < changes.size(); j++)
						if (changes[j].beat <= at + 0.001f)
							ci = j;
					Harmony h;
					h.valid = true;
					h.key = song.key;
					h.current = changes.empty() ? Chord() : changes[ci].chord;
					h.next = (ci + 1 < changes.size()) ? changes[ci + 1].chord
						: (changes.empty() ? Chord() : changes[0].chord);
					h.beatsToNext = (ci + 1 < changes.size()) ? changes[ci + 1].beat - at
						: pb.totalBeats - at;
					int bar = 0;
					for (int b = 0; b < (int) pb.timeline.size(); b++)
						if (at >= pb.timeline[b].startBeat - 0.001f)
							bar = b;
					h.bar = bar;
					h.beatInBar = at - pb.timeline[bar].startBeat;
					h.barBeats = (uint8_t) bars[pb.timeline[bar].bar].beats;
					// A PICKUP SOUNDS IN THE PHRASE BEFORE, and the harmony block says so.
					const bool pickup = note.offset < -1e-4f;
					h.phraseBeats = pickup ? prevBeats : ask.phraseBeats;
					h.beatsToPhraseEnd = pickup ? base - at : ph.fullEnd - at;
					h.phraseCadence = pickup ? prevCadence : ph.cadence;
					h.phrase = (uint16_t) (pickup ? number - 1 : number);
					h.phrasesPerPass = (uint16_t) songPhrases;
					h.seed = P.chartSeed;
					const bool lockMelody = P.melodyOwn > 0.5f || P.chartSeed == 0;
					const float dice = melodyDraw(P.chartSeed, (uint32_t) P.melodySeed,
						lockMelody, (int) P.melodyCycle, 0, (uint32_t) songPhrases,
						(uint32_t) number, note.offset);
					const VoiceLine vline = memory.lineFor(note.echo, note.along);
					pitch = voiceNoteFor(h, vs, line, dice, NULL, 0, 0.f, NULL,
						(uint8_t) ((note.arrival ? Event::ARRIVAL : 0)
							| (note.groupEnd ? Event::GROUP_END : 0)
							| (note.approach ? Event::APPROACH : 0)
							| (pickup ? Event::PICKUP : 0)
							| (note.onChange ? Event::ON_CHANGE : 0)), lineBefore, note.duration,
						&vline);
					memory.remember(pitch, note.along);
					if (getenv("SIM_DEBUG"))
					{
						int ct[MAX_CHORD_TONES];
						const int cn = chordPitchClasses(h.current, h.key, ct);
						bool echoInChord = false;
						for (int t = 0; t < cn && vline.echoPitch >= 0; t++)
							echoInChord = echoInChord || ((ct[t] - vline.echoPitch) % 12 + 12) % 12 == 0;
						std::printf("[motif %d %d %d %.3f %d %.2f %d]\n", pitch, note.echo, vline.echoPitch,
							note.along, echoInChord ? 1 : 0, note.duration, line);
					}
					lineBefore = line;
					line = pitch;
					landing = pitch;
					// A CLASH: a note outside the chord, a semitone from one of its tones.
					if (getenv("SIM_DEBUG")) {
						int ct[MAX_CHORD_TONES];
						const int cn = chordPitchClasses(h.current, h.key, ct);
						const int pc = ((pitch % 12) + 12) % 12;
						bool in = false, near = false;
						for (int t = 0; t < cn; t++) {
							const int d = ((ct[t] - pc) % 12 + 12) % 12;
							in = in || d == 0;
							near = near || d == 1 || d == 11;
						}
						const int rpc = ((chordRootPitchClass(h.current, h.key) % 12) + 12) % 12;
						const bool tritone = !in && ((pc - rpc + 12) % 12) == 6;
						int kp[7];
						scalePitchClasses(h.key, kp);
						bool inKey = false;
						for (int t = 0; t < 7; t++)
							inKey = inKey || ((kp[t] % 12) + 12) % 12 == pc;
						std::printf("[pitch %d %d %.3f %d %d %d]\n", pitch, (!in && near) ? 1 : 0,
							note.duration, (int) ((note.arrival ? 1 : 0) | (note.groupEnd ? 2 : 0)),
							tritone ? 1 : 0, (!in && !inKey) ? 1 : 0);
						std::printf("[chord %d %d %d %.3f %d]\n", rpc, (int) h.current.quality, in ? 1 : 0,
							note.duration, std::fabs(h.beatInBar - std::round(h.beatInBar)) < 0.05f ? 1 : 0);
					}
				}
				if (getenv("SIM_DEBUG"))
					std::printf("[note %.4f %.4f %.3f %d %.3f]\n", base + note.offset,
						note.duration, note.level, (int) k, note.offset / std::max(1.f, ask.phraseBeats));
				if (listNotes)
					std::printf("      %s%s for %s\n",
						pitch >= 0 ? (noteName(pitch) + " on ").c_str() : "a note on ",
						where(pb, base + note.offset).c_str(),
						beatsWords(note.duration).c_str());
			}
		}
		if (landing >= 0)
			std::printf("  It ends on %s, %s.\n", noteName(landing).c_str(),
				degreeName(song.key, ((landing % 12) + 12) % 12).c_str());
		prev = cur;
		havePrev = true;
		prevBeats = ask.phraseBeats;
		prevCadence = ph.cadence;
	}
	return 0;
}
