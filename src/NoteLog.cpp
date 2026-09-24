/** See NoteLog.hpp. */
#include "NoteLog.hpp"

#include <cmath>
#include <ctime>

namespace px {


/** A pitch in volts written the way a musician reads it: nought volts is middle C, which this
plugin calls C4, and twelve semitones to the volt. */
static std::string pitchName(float volts) {
	static const char* NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#",
		"B"};
	const int semis = (int) std::lround(volts * 12.f);
	const int octave = 4 + (int) std::floor(semis / 12.f);
	int pc = semis % 12;
	if (pc < 0)
		pc += 12;
	return std::string(NAMES[pc]) + std::to_string(octave);
}


void NoteLogWriter::open(const std::string& name) {
	close();
	const std::string folder = asset::user("DreamerMPX");
	system::createDirectories(folder);
	char stamp[32];
	std::time_t now = std::time(NULL);
	std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
	path = folder + "/" + name + "-log-" + stamp + ".jsonl";
	file = std::fopen(path.c_str(), "w");
}


void NoteLogWriter::close() {
	if (!file)
		return;
	std::fclose(file);
	file = NULL;
}


void NoteLogWriter::drain(NoteLog& log, const char* const* names, int nameCount) {
	LogNote n;
	if (!file) {
		// Not recording: whatever the engine queued belongs to no take.
		while (log.pop(n)) {}
		droppedSeen = log.dropped.load();
		return;
	}
	while (log.pop(n)) {
		if (n.kind == LogNote::SETTINGS) {
			std::fprintf(file, "{\"settings\":{");
			for (int i = 0; i < n.valueCount && i < 24; i++) {
				const char* name = (names && i < nameCount) ? names[i] : "";
				std::fprintf(file, "%s\"%s\":%.4f", i ? "," : "", name, n.values[i]);
			}
			std::fprintf(file, "},\"t\":%.3f}\n", n.seconds);
			continue;
		}
		if (n.kind == LogNote::VOICING) {
			std::fprintf(file, "{\"voicing\":[");
			for (int i = 0; i < n.count && i < 8; i++) {
				std::fprintf(file, "%s\"%s\"", i ? "," : "", pitchName(n.pitches[i]).c_str());
			}
			std::fprintf(file, "],\"t\":%.3f,\"beat\":%.3f,\"bar\":%d", n.seconds, n.beat,
				n.bar);
			if (n.haveChord) {
				std::fprintf(file, ",\"chord\":\"%s\",\"roman\":\"%s\"",
					chordLetter(n.chord, n.key).c_str(), chordRoman(n.chord).c_str());
			}
			std::fprintf(file, "}\n");
			continue;
		}
		std::fprintf(file,
			"{\"t\":%.3f,\"beat\":%.3f,\"bar\":%d,\"inBar\":%.3f,\"voice\":%d,"
			"\"pitch\":\"%s\",\"volts\":%.4f,\"level\":%.3f,\"dur\":%.3f",
			n.seconds, n.beat, n.bar, n.beatInBar, n.voice, pitchName(n.pitch).c_str(),
			n.pitch, n.level, n.duration);
		if (n.haveChord) {
			std::fprintf(file, ",\"chord\":\"%s\",\"roman\":\"%s\"",
				chordLetter(n.chord, n.key).c_str(), chordRoman(n.chord).c_str());
		}
		std::fprintf(file, "}\n");
	}
	const uint32_t dropped = log.dropped.load();
	if (dropped != droppedSeen) {
		std::fprintf(file, "{\"dropped\":%u}\n", (unsigned) (dropped - droppedSeen));
		droppedSeen = dropped;
	}
	// FLUSHED EVERY FRAME, so the file on disk is always what has been played so far: the point
	// of the log is to be read while the patch is still running.
	std::fflush(file);
}


std::string NoteLogWriter::where() const {
	return path.empty() ? ("writes to " + asset::user("DreamerMPX")) : path;
}


} // namespace px
