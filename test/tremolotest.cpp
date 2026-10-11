/** TREMOLOS BETWEEN TWO NOTES, read and written out.

A two-note tremolo is two notes joined by beams not joined to their stems: they alternate quickly for
the whole length they are written at between them. MusicXML marks the first `<tremolo type="start">`
and the second `<tremolo type="stop">`, each written at the whole length and squeezed to half of it
by a time modification; the number in the element is how many beams. This builds a small score with
each case in it and checks what the timeline makes of it; then, given a file, says how many it found.

    make tremolotest                       the small score
    make tremolotest ARGS="a-score.mxl"    and a file's tremolos
*/
#include "../src/GpTimeline.hpp"
#include "../src/GuitarPro.hpp"
#include "../src/MusicXML.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;

static int failures = 0;

static void check(bool ok, const char* what, double got) {
	std::printf("%s  %-66s %g\n", ok ? "ok  " : "FAIL", what, got);
	if (!ok)
		failures++;
}

/** ONE NOTE of the small score: its pitch, how long it is in divisions (24 a quarter), what it is
written as, the tremolo it carries, and whether it is a chord note on the one before. */
static std::string note(const char* step, int octave, int duration, const char* type, const char* tremolo,
                        int beams, bool chord = false, bool squeezed = true) {
	std::string s = "<note>";
	if (chord)
		s += "<chord/>";
	s += std::string("<pitch><step>") + step + "</step><octave>" + std::to_string(octave) + "</octave></pitch>";
	s += "<duration>" + std::to_string(duration) + "</duration><voice>1</voice><type>" + type + "</type>";
	if (squeezed)
		s += "<time-modification><actual-notes>2</actual-notes><normal-notes>1</normal-notes></time-modification>";
	if (tremolo)
		s += std::string("<notations><ornaments><tremolo type=\"") + tremolo + "\">" + std::to_string(beams)
			+ "</tremolo></ornaments></notations>";
	s += "</note>";
	return s;
}

/** A BAR OF 2/2 at 120 quarters a minute: two half-note beats of a second each. */
static std::string score() {
	std::string m1 = note("C", 3, 24, "half", "start", 3) + note("C", 4, 24, "half", "stop", 3)
		+ note("E", 3, 24, "half", "start", 2) + note("E", 4, 24, "half", "stop", 2);
	std::string m2 = note("C", 3, 24, "half", "start", 3) + note("E", 3, 24, "half", "start", 3, true)
		+ note("G", 3, 24, "half", "stop", 3) + note("C", 4, 24, "half", "stop", 3, true)
		+ note("D", 3, 48, "half", "start", 3, false, false);
	return std::string("<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list>")
		+ "<score-part id=\"P1\"><part-name>Piano</part-name></score-part></part-list><part id=\"P1\">"
		+ "<measure number=\"1\"><attributes><divisions>24</divisions><time><beats>2</beats>"
		+ "<beat-type>2</beat-type></time></attributes>"
		+ "<direction><direction-type><words>Andante</words></direction-type><sound tempo=\"120\"/></direction>"
		+ m1 + "</measure><measure number=\"2\">" + m2 + "</measure></part></score-partwise>";
}

int main(int argc, char** argv) {
	std::printf("TREMOLOS BETWEEN TWO NOTES, in a small score.\n");
	GpSong song;
	std::string why;
	if (!mxRead(score(), song, &why)) {
		std::printf("could not read the small score: %s\n", why.c_str());
		return 1;
	}
	GpTimeline line;
	gpBuildTimeline(song, line, &why);
	const std::vector<GpPlayedNote>& notes = line.tracks[0];
	auto between = [&](double from, double to) {
		std::vector<GpPlayedNote> out;
		for (const GpPlayedNote& n : notes)
			if (n.startSeconds >= from - 1e-6 && n.startSeconds < to - 1e-6)
				out.push_back(n);
		return out;
	};
	// THREE BEAMS: thirty-seconds, sixteen of them in the half-note second, C3 and C4 by turns.
	{
		const std::vector<GpPlayedNote> a = between(0.0, 1.0);
		bool alternate = a.size() == 16;
		for (size_t k = 0; alternate && k < a.size(); k++)
			alternate = a[k].note.midi == (k % 2 ? 60 : 48) && std::fabs(a[k].lengthSeconds - 1.0 / 16) < 1e-4
				&& std::fabs(a[k].startSeconds - k / 16.0) < 1e-4;
		check(a.size() == 16, "  three beams: notes in the half-note beat", (double) a.size());
		check(alternate, "  and they alternate, the lower first, each a sixteenth of the beat", alternate);
	}
	// TWO BEAMS: sixteenths, eight.
	{
		const std::vector<GpPlayedNote> a = between(1.0, 2.0);
		bool alternate = a.size() == 8;
		for (size_t k = 0; alternate && k < a.size(); k++)
			alternate = a[k].note.midi == (k % 2 ? 64 : 52);
		check(a.size() == 8 && alternate, "  two beams: eight notes, E3 and E4 by turns", (double) a.size());
	}
	// CHORDS: C3 with E3, then G3 with C4, by turns, sixteen strokes of two notes each.
	{
		const std::vector<GpPlayedNote> a = between(2.0, 3.0);
		bool whole = a.size() == 32;
		for (size_t k = 0; whole && k + 1 < a.size(); k += 2) {
			const int lo = std::min(a[k].note.midi, a[k + 1].note.midi), hi = std::max(a[k].note.midi, a[k + 1].note.midi);
			const bool odd = (k / 2) % 2;
			whole = std::fabs(a[k].startSeconds - a[k + 1].startSeconds) < 1e-6
				&& lo == (odd ? 55 : 48) && hi == (odd ? 60 : 52);
		}
		check(whole, "  chords: alternating whole, two notes a stroke, notes", (double) a.size());
	}
	// A START WITH NO STOP: left as written, one note.
	{
		const std::vector<GpPlayedNote> a = between(3.0, 4.0);
		check(a.size() == 1 && a[0].note.midi == 50, "  a start with no stop after it: as written, notes", (double) a.size());
	}
	for (const GpPlayedNote& n : notes)
		if (n.note.tremoloPair != 0) {
			check(false, "  a tremolo mark left on a note", 1);
			break;
		}

	if (argc > 1) {
		GpSong file;
		if (!mxReadFile(argv[1], file, &why)) {
			std::printf("could not read %s: %s\n", argv[1], why.c_str());
			return 1;
		}
		int starts = 0;
		for (const GpTrack& t : file.tracks)
			for (const GpBar& b : t.bars)
				for (const GpBeat& beat : b.beats)
					for (const GpNote& n : beat.notes)
						starts += n.tremoloPair == 1;
		GpTimeline written;
		gpBuildTimeline(file, written, &why);
		size_t total = 0;
		for (const auto& t : written.tracks)
			total += t.size();
		std::printf("\n%s: %d notes beginning a tremolo between two; %zu notes played in all.\n", argv[1], starts, total);
	}
	std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all passed");
	return failures ? 1 : 0;
}
