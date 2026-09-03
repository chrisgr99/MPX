/** Reads the layout back as text, so a chart can be checked by eye before any of it is drawn.

  make charttest                 — the census over every playlist
  make charttest ARGS="Title"    — print the charts whose title contains that

The census is the part that matters: a fault in the layout shows up as a chart that lays out to
no bars, or to a bar count that disagrees with what the player expanded, and neither is visible
by looking at one song.
*/
#include "../src/ChartLayout.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace px;


static std::string readFile(const char* path) {
	std::ifstream file(path);
	std::stringstream ss;
	ss << file.rdbuf();
	return ss.str();
}

/** A chord as it would be read off the page. */
static std::string slotText(const ChartSlot& slot, const Key& key) {
	if (slot.simile == ChartSlot::SIMILE_ONE)
		return "%";
	if (slot.simile == ChartSlot::SIMILE_TWO)
		return "%%";
	if (slot.simile == ChartSlot::SIMILE_LAST)
		return "%|";
	if (slot.empty)
		return "";
	if (slot.noChord)
		return "N.C.";
	if (slot.plain)
		return slot.raw.empty() ? "?" : slot.raw;
	return chordLetter(slot.chord, key);
}

/** One bar, with the marks that sit on its edges. */
static std::string barText(const ChartBar& bar, const Key& key) {
	std::string s;
	s += bar.repeatOpen ? "|:" : "| ";
	std::string body;
	for (size_t i = 0; i < bar.slots.size(); i++) {
		if (i)
			body += " ";
		body += slotText(bar.slots[i], key);
	}
	if (bar.segno)
		body = "S " + body;
	if (bar.coda)
		body += " Q";
	if (bar.fine)
		body += " Fine";
	if (bar.nav.from != ChartNav::NAV_NONE) {
		body += (bar.nav.from == ChartNav::NAV_DS) ? " D.S." : " D.C.";
		if (bar.nav.target == ChartNav::TO_CODA)
			body += " al Coda";
		else if (bar.nav.target == ChartNav::TO_FINE)
			body += " al Fine";
		else if (bar.nav.target == ChartNav::TO_ENDING)
			body += " al ending";
	}
	if (bar.passes > 0) {
		char buf[16];
		std::snprintf(buf, sizeof(buf), " (%dx)", bar.passes);
		body += buf;
	}
	if (bar.timeBeats > 0) {
		char buf[16];
		std::snprintf(buf, sizeof(buf), "[%d/%d] ", bar.timeBeats, bar.timeUnit);
		body = std::string(buf) + body;
	}
	while (body.size() < 14)
		body += " ";
	s += body;
	if (bar.repeatClose)
		s += ":";
	else if (bar.end)
		s += "]";
	else if (bar.doubleRight)
		s += "|";
	else
		s += " ";
	return s;
}

static void printChart(const Song& song) {
	std::printf("\n%s — %s   [%s]   %s %s   %d/%d\n",
		song.title.c_str(), song.composer.c_str(), song.style.c_str(),
		pitchClassNameIn(song.key.tonic, song.key), song.key.minor ? "minor" : "major",
		song.beats, song.unit);
	if (!song.supported)
		std::printf("  (unsupported: %s)\n", song.why.c_str());

	const std::vector<ChartBar> bars = chartLayout(song);
	const std::vector<std::vector<RowCell> > rows = chartRows(bars, 4);

	for (const std::vector<RowCell>& row : rows) {
		// The line's opening mark: a section letter, an ending number, or nothing.
		std::string head = "     ";
		for (const RowCell& cell : row) {
			if (cell.empty)
				continue;
			const ChartBar& bar = bars[cell.bar];
			if (bar.section != 0) {
				head = std::string("  ") + bar.section + "  ";
			}
			else if (bar.ending > 0) {
				char buf[8];
				std::snprintf(buf, sizeof(buf), " %d.  ", bar.ending);
				head = buf;
			}
			break;
		}
		std::printf("%s", head.c_str());
		for (const RowCell& cell : row) {
			if (cell.empty)
				std::printf("%17s", "");
			else
				std::printf("%s", barText(bars[cell.bar], song.key).c_str());
		}
		std::printf("\n");
	}

	const std::vector<ChartSection> sections = chartSections(bars);
	if (!sections.empty()) {
		std::printf("     sections:");
		for (const ChartSection& sec : sections)
			std::printf("  %c[%d-%d]", sec.label, sec.first, sec.last);
		std::printf("\n");
	}
}


int main(int argc, char** argv) {
	std::vector<std::string> files;
	std::string want;
	for (int i = 1; i < argc; i++) {
		const std::string arg = argv[i];
		if (arg.size() > 5 && arg.compare(arg.size() - 5, 5, ".html") == 0)
			files.push_back(arg);
		else
			want = arg;
	}

	// A line per song — bars, rows, sections and their ranges — so the port can be diffed
	// against the output of the file it was ported from. Nothing checks a port like the thing
	// it came from disagreeing with it about a real chart.
	const bool tsv = !want.empty() && want == "--tsv";
	if (tsv)
		want.clear();
	const bool sections_ = !want.empty() && want == "--sections";
	if (sections_)
		want.clear();
	int checkedSections = 0, badSections = 0;
	const bool play = !want.empty() && want == "--play";
	if (play)
		want.clear();
	bool cells = false;
	if (want.size() > 8 && want.compare(0, 8, "--cells=") == 0) {
		cells = true;
		want = want.substr(8);
	}

	int songs = 0, empty = 0, printed = 0;
	int withSections = 0, withRepeats = 0, withEndings = 0, withNav = 0, withSimile = 0;
	int withMeterChange = 0, withPasses = 0;
	std::map<int, int> sectionCounts;
	int barsTotal = 0;

	for (const std::string& file : files) {
		const std::string payload = irealPayloadFromHtml(readFile(file.c_str()));
		if (payload.empty()) {
			std::printf("no irealb link in %s\n", file.c_str());
			continue;
		}
		for (const Song& song : irealParsePlaylist(payload)) {
			songs++;
			const std::vector<ChartBar> bars = chartLayout(song);
			barsTotal += (int) bars.size();
			if (bars.empty())
				empty++;

			bool repeats = false, endings = false, nav = false, simile = false;
			bool meter = false, passes = false;
			for (const ChartBar& bar : bars) {
				repeats = repeats || bar.repeatOpen || bar.repeatClose;
				endings = endings || bar.ending > 0;
				nav = nav || bar.nav.from != ChartNav::NAV_NONE || bar.fine
					|| bar.segno || bar.coda;
				// Only a change PART WAY THROUGH counts. Nearly every chart states its meter
				// at the head, and that is not a change.
				meter = meter || (bar.timeBeats > 0 && bar.index > 0);
				passes = passes || bar.passes > 0;
				for (const ChartSlot& slot : bar.slots)
					simile = simile || slot.simile != ChartSlot::SIMILE_NONE;
			}
			withRepeats += repeats;
			withEndings += endings;
			withNav += nav;
			withSimile += simile;
			withMeterChange += meter;
			withPasses += passes;

			const std::vector<ChartSection> sections = chartSections(bars);
			sectionCounts[(int) sections.size()]++;
			if (sections.size() >= 2)
				withSections++;

			if (sections_) {
				// Every label of every chart: the section timeline must be non-empty and must
				// contain only bars that belong to that label.
				const ChartPlayback whole = chartPlayback(bars);
				for (const ChartSection& sec : chartSections(bars)) {
					const ChartPlayback part =
						chartPlaybackForLabel(bars, whole, sec.label);
					bool ok = !part.timeline.empty();
					const std::vector<ChartSection> ranges =
						chartRangesForLabel(bars, sec.label);
					for (size_t k = 0; k < part.timeline.size() && ok; k++) {
						bool inside = false;
						for (size_t r = 0; r < ranges.size() && !inside; r++) {
							inside = part.timeline[k].bar >= ranges[r].first
								&& part.timeline[k].bar <= ranges[r].last;
						}
						ok = inside;
					}
					if (!ok) {
						std::printf("BAD  %s  section %c  range %d-%d  kept %d of %d: ",
							song.title.c_str(), sec.label, sec.first, sec.last,
							(int) part.timeline.size(), (int) whole.timeline.size());
						for (size_t k = 0; k < part.timeline.size() && k < 12; k++)
							std::printf("%d ", part.timeline[k].bar);
						std::printf("\n");
						badSections++;
					}
					checkedSections++;
				}
			}

			if (play) {
				const ChartPlayback pb = chartPlayback(bars);
				std::printf("%s\t%d\t%g\t", song.title.c_str(),
					(int) pb.timeline.size(), pb.totalBeats);
				for (size_t k = 0; k < pb.timeline.size(); k++)
					std::printf("%s%d", k ? "," : "", pb.timeline[k].bar);
				std::printf("\n");
			}

			if (tsv) {
				std::printf("%s\t%d\t%d\t%d\t", song.title.c_str(), (int) bars.size(),
					(int) chartRows(bars, 4).size(), (int) sections.size());
				for (size_t k = 0; k < sections.size(); k++) {
					std::printf("%s%c:%d-%d", k ? "," : "",
						sections[k].label, sections[k].first, sections[k].last);
				}
				std::printf("\n");
			}

			if (!want.empty() && song.title.find(want) != std::string::npos && printed < 8) {
				if (cells) {
					std::printf("%s:\n", song.title.c_str());
					static const char* NAMES[] = {"chord", "bar", "section", "repeatOpen",
						"repeatClose", "ending", "end", "time", "repeatBar", "repeatTwo",
						"repeatHeld", "empty", "divider", "segno", "coda", "comment"};
					for (const Cell& cell : song.cells) {
						std::printf("%s%s ", NAMES[cell.kind],
							cell.kind == Cell::SECTION ? (std::string(":") + cell.label).c_str()
							: cell.kind == Cell::COMMENT ? (":" + cell.raw).c_str() : "");
					}
					std::printf("\n");
				}
				else
					printChart(song);
				printed++;
			}
		}
	}

	if (sections_) {
		std::printf("%d sections checked, %d wrong\n", checkedSections, badSections);
		return 0;
	}
	if (tsv || play)
		return 0;
	std::printf("\n%d songs, %d bars laid out, %d laid out to nothing\n",
		songs, barsTotal, empty);
	std::printf("  with repeats        %5d\n", withRepeats);
	std::printf("  with endings        %5d\n", withEndings);
	std::printf("  with a repeat count %5d\n", withPasses);
	std::printf("  with navigation     %5d\n", withNav);
	std::printf("  with similes        %5d\n", withSimile);
	std::printf("  meter change mid-chart %5d\n", withMeterChange);
	std::printf("  with 2+ sections    %5d\n", withSections);
	for (std::map<int, int>::const_iterator it = sectionCounts.begin();
		it != sectionCounts.end(); ++it) {
		std::printf("    %d sections: %d songs\n", it->first, it->second);
	}
	return 0;
}
