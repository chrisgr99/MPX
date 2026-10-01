/** See SoundCore.hpp. */
#include "SoundCore.hpp"

namespace px {


SoundCore::SoundCore() {
	for (int p = 0; p < MAX_PARTS; p++) {
		wantCount[p] = 0;
		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[p][i] = -1;
			wantGenerations[p][i] = 0;
		}
	}
	for (int i = 0; i < MAX_GROUPS * 2; i++)
		dryAt[i] = dry[i];
	for (int i = 0; i < 4; i++)
		wetAt[i] = wet[i];
}


SoundCore::~SoundCore() {
	if (worker.joinable())
		worker.join();
}


void SoundCore::setParts(int count, bool outputPerVoice) {
	parts = math::clamp(count, 1, (int) MAX_PARTS);
	voiceOutputs = outputPerVoice;
	voices = voiceOutputs ? std::min((int) VOICES, MAX_GROUPS / parts) : VOICES;
}


float SoundCore::partLeft(int part) const {
	if (!voiceOutputs)
		return dry[2 * part][blockAt - 1];
	float sum = 0.f;
	for (int v = 0; v < voices; v++)
		sum += dry[2 * channel(part, v)][blockAt - 1];
	return sum;
}


float SoundCore::partRight(int part) const {
	if (!voiceOutputs)
		return dry[2 * part + 1][blockAt - 1];
	float sum = 0.f;
	for (int v = 0; v < voices; v++)
		sum += dry[2 * channel(part, v) + 1][blockAt - 1];
	return sum;
}


void SoundCore::link(int part, const int* slots, const uint32_t* generations, int n) {
	if (part < 0 || part >= parts)
		return;
	bool same = (n == wantCount[part].load());
	for (int i = 0; i < n && same; i++) {
		same = (wantSlots[part][i].load() == slots[i]
			&& wantGenerations[part][i].load() == generations[i]);
	}
	if (same)
		return;
	for (int i = 0; i < n; i++) {
		wantSlots[part][i] = slots[i];
		wantGenerations[part][i] = generations[i];
	}
	wantCount[part] = n;
	relink = true;
}


std::string SoundCore::bankFolder() {
	return asset::user("DreamerMPX/banks");
}


std::string SoundCore::firstBank() {
	const std::string dir = bankFolder();
	if (!system::isDirectory(dir))
		return "";
	std::vector<std::string> names = system::getEntries(dir);
	std::sort(names.begin(), names.end());
	for (size_t i = 0; i < names.size(); i++) {
		const std::string ext = system::getExtension(names[i]);
		if (ext == ".sf2" || ext == ".SF2")
			return names[i];
	}
	return "";
}


void SoundCore::loadBank(const std::string& path) {
	if (busy.load())
		return;
	if (worker.joinable())
		worker.join();
	wantBank = path;
	busy = true;
	haveBank = false;
	failed = false;
	message = "loading";
	worker = std::thread([this]() {
		const bool ok = engine.running() && engine.loadBank(wantBank);
		if (ok) {
			message = engine.bankName();
			if (onBankRead)
				onBankRead();
			// The programs the cables asked for before there was a bank to take them from.
			for (int p = 0; p < parts; p++)
				instrumentChange[p] = 0;
			haveBank = true;
			// SAID ONCE, IN THE LOG. Whether a bank was found and read is the first question
			// asked when the module makes no sound, and reading it off the panel means being at
			// the panel at the moment it happened.
			INFO("%s: read %d sounds from %s", logName.c_str(), (int) engine.presets().size(),
				wantBank.c_str());
		}
		else {
			message = engine.reason();
			failed = true;
			WARN("%s: %s: %s", logName.c_str(), wantBank.c_str(), engine.reason().c_str());
		}
		busy = false;
	});
}


// THE SYNTH IS MADE ON THE MAIN THREAD, when the module is added and when the rate changes.
// Making it inside process() would allocate in the audio callback, and creating a synthesiser is
// a great deal of allocation.
void SoundCore::startEngine(float sampleRate, bool reverb, bool chorus,
		const std::string& preferred) {
	if (worker.joinable())
		worker.join();
	rate = sampleRate;
	// Two hundred and fifty-six notes a part is more than a part plays; it is the whole band's
	// worth that has to fit, and twelve busy parts can reach it.
	if (!engine.start((double) rate, channels(), 256 + 32 * parts, groups()))
		return;
	for (int c = 0; c < channels(); c++)
		engine.bendRange(c, 12);
	reverbOn = reverb;
	chorusOn = chorus;
	engine.effects(reverb, chorus);
	if (!preferred.empty()) {
		loadBank(preferred);
		return;
	}
	if (!engine.bankPath().empty()) {
		loadBank(engine.bankPath());
		return;
	}
	// A BANK THAT IS ALREADY THERE IS THE ONE TO USE. A module that comes up silent and asks for
	// a file is a module somebody thinks is broken, so the folder is looked in first and the only
	// thing in it is taken. Choosing another is still a press away.
	const std::string found = firstBank();
	if (!found.empty())
		loadBank(found);
	else
		INFO("%s: no SoundFont in %s", logName.c_str(), bankFolder().c_str());
}


void SoundCore::stopEngine() {
	if (worker.joinable())
		worker.join();
	engine.stop();
}


void SoundCore::setProgram(int part, int bank, int program) {
	if (part < 0 || part >= parts)
		return;
	for (int v = 0; v < voices; v++)
		engine.program(channel(part, v), bank, program);
}


void SoundCore::readRules() {
	const std::string path = asset::user("DreamerMPX/perform.txt");
	PerformRules rules;
	if (system::isFile(path)) {
		FILE* f = std::fopen(path.c_str(), "rb");
		std::string text;
		if (f) {
			char buf[4096];
			size_t n = 0;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
				text.append(buf, n);
			std::fclose(f);
		}
		std::string complaint;
		performRulesRead(text, rules, &complaint);
	}
	loadedRules = rules;
	rulesReady.store(true);
}


/** THE PROGRAM THE FILE NAMES, from the bank. A drum track takes the kit of that number in bank
128, or the first kit if the bank has no such one; anything else takes the program in bank 0. */
void SoundCore::followProgram(int part, const Instrument& in) {
	if (!in.valid)
		return;
	const std::vector<FluidPreset>& list = engine.presets();
	const int bank = in.percussion ? 128 : 0;
	const int wanted = math::clamp((int) in.program, 0, 127);
	int program = -1;
	for (size_t i = 0; i < list.size() && program < 0; i++) {
		if (list[i].bank == bank && list[i].program == wanted)
			program = wanted;
	}
	if (program < 0 && in.percussion) {
		for (size_t i = 0; i < list.size() && program < 0; i++) {
			if (list[i].bank == 128)
				program = list[i].program;
		}
	}
	if (program >= 0)
		setProgram(part, bank, program);
	// AND WHERE IT SITS, on every one of the part's channels.
	const int pan = math::clamp((int) std::lround(64.f + in.pan * 63.f), 0, 127);
	for (int v = 0; v < voices; v++)
		engine.controller(channel(part, v), 10, pan);
}


void SoundCore::step(float sampleTime, const bool* muted, float humanise, bool reverb,
		bool chorus) {
	if (relink.exchange(false)) {
		for (int p = 0; p < parts; p++) {
			reader[p].clear();
			const int n = wantCount[p].load();
			for (int i = 0; i < n; i++)
				reader[p].add(wantSlots[p][i].load(), wantGenerations[p][i].load());
		}
	}
	const bool on = playing();
	for (int p = 0; p < parts; p++) {
		const bool attached = reader[p].attached();
		if (!attached && attachedWas[p])
			performer[p].silence();
		attachedWas[p] = attached;
	}

	// The effects follow their buttons, and only when one moves.
	if (reverb != reverbOn || chorus != chorusOn) {
		reverbOn = reverb;
		chorusOn = chorus;
		engine.effects(reverbOn, chorusOn);
	}

	// ---- the notes ----
	//
	// WHAT IS PLAYED IS DECIDED BY THE PERFORMER, and FluidSynth is told the result. It knows
	// note-on, note-off, pitch bend and control changes, and nothing about a hammer-on or a palm
	// mute: every articulation is turned into those four here.
	if (rulesReady.exchange(false)) {
		for (int p = 0; p < parts; p++)
			performer[p].rules = loadedRules;
	}

	for (int p = 0; p < parts; p++) {
		const bool mute = muted && muted[p];
		Instrument in;
		if (on && reader[p].instrument(in) && in.change != instrumentChange[p]) {
			instrumentChange[p] = in.change;
			instrument[p] = in;
			performer[p].strings(in.stringCount);
			if (followInstrument)
				followProgram(p, in);
		}
		performer[p].humanise(humanise);

		Event e;
		while (reader[p].next(e)) {
			if (!on || mute)
				continue;
			if (e.kind == Event::ON) {
				PerformNote n;
				n.handle = e.handle;
				n.pitch = e.pitch;
				n.level = e.level;
				n.seconds = (e.duration > 0.f) ? e.duration : 0.25f;
				n.string = e.string;
				n.fret = e.fret;
				n.pan = e.pan;
				n.technique = e.technique;
				n.vibrato = e.vibrato;
				n.grace = e.grace;
				n.strum = e.strum;
				n.strumIndex = e.strumIndex;
				n.strumMs = e.strumMs;
				n.bendCount = e.bendCount;
				for (int k = 0; k < n.bendCount && k < 4; k++) {
					n.bendAt[k] = (float) e.bendPoints[k].at / 100.f;
					n.bendCents[k] = (float) e.bendPoints[k].cents;
				}
				performer[p].note(n);
				active[p] = 1.f;
			}
			else if (e.kind == Event::OFF) {
				for (int i = 0; i < performer[p].voiceCount(); i++) {
					if (performer[p].voice(i).gate && performer[p].voice(i).handle == e.handle)
						performer[p].silenceVoice(i);
				}
			}
			else if (e.lane == LANE_PRESSURE || e.lane == LANE_TIMBRE) {
				for (int i = 0; i < performer[p].voiceCount(); i++) {
					if (performer[p].voice(i).gate && performer[p].voice(i).handle == e.handle)
						performer[p].set(i, e.lane == LANE_TIMBRE, e.value);
				}
			}
		}

		performer[p].advance(sampleTime);

		PerformMessage m;
		while (performer[p].next(m)) {
			const int ch = channel(p, m.voice);
			switch (m.kind) {
				case PerformMessage::ATTACK:
					engine.noteOn(ch, math::clamp(m.key, 0, 127),
						math::clamp((int) std::lround(m.value * 127.f), 1, 127));
					break;
				case PerformMessage::RELEASE:
					engine.noteOff(ch, math::clamp(m.key, 0, 127));
					break;
				case PerformMessage::BEND:
					engine.bend(ch, m.value);
					break;
				case PerformMessage::LEVEL:
					// Expression rather than velocity: the note is already sounding, and a
					// second velocity would mean striking it again.
					engine.controller(ch, 11,
						math::clamp((int) std::lround(m.value * 127.f), 0, 127));
					break;
				case PerformMessage::TIMBRE:
					engine.controller(ch, 74,
						math::clamp((int) std::lround(m.value * 127.f), 0, 127));
					break;
				default:
					break;
			}
		}

		active[p] = std::fmax(0.f, active[p] - sampleTime * 3.f);
	}

	// ---- the audio ----
	if (blockAt >= BLOCK) {
		if (on)
			engine.renderGroups(dryAt, wetAt, BLOCK);
		else {
			for (int i = 0; i < groups() * 2; i++)
				std::memset(dry[i], 0, sizeof(dry[i]));
			for (int i = 0; i < 4; i++)
				std::memset(wet[i], 0, sizeof(wet[i]));
		}
		blockAt = 0;
	}
	blockAt++;
}


} // namespace px
