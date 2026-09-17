/** Reads the layout back as text, so a chart can be checked by eye before any of it is drawn.

  make charttest                 — the census over every playlist
  make charttest ARGS="Title"    — print the charts whose title contains that

The census is the part that matters: a fault in the layout shows up as a chart that lays out to
no bars, or to a bar count that disagrees with what the player expanded, and neither is visible
by looking at one song.
*/
#include "../src/ChartLayout.hpp"

#include <cmath>
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
	// A CENSUS OF THE PHRASING, which is the only way to tell whether the rules ported from GXW
	// behave over real charts rather than over the two anybody thinks to try. It prints how
	// many phrases each chart is cut into and how long they are, and counts the ones that came
	// out at lengths a phrase should not have.
	// A CENSUS OF CADENCES WITH NO PHRASE-LENGTH PREFERENCE APPLIED: where the harmony
	// actually arrives, what kind of arrival it is, and how many bars fall between one arrival
	// and the next. This is the evidence for how long a phrase is in real tunes, rather than
	// the four and eight bars the phrasing rule assumed.
	const bool cadences_ = !want.empty() && want == "--cadences";
	if (cadences_)
		want.clear();
	std::map<std::string, int> cadenceKinds;
	std::map<int, int> closeGaps, anyGaps;
	std::map<std::string, int> arrivalBeat;
	int cadenceSongs = 0, songsWithNoClose = 0;
	// WHAT THE CABLE SAYS ABOUT THE FORM, CHECKED AGAINST WHAT THE CHART PLAYS. The chord changes
	// published for each phrase are compared with the changes found by walking the playback through
	// the module's own chord resolver; section appearances and phrase numbers within a section are
	// checked for being consecutive and restarting where they should.
	const bool form_ = !want.empty() && want == "--form";
	if (form_)
		want.clear();
	int formSongs = 0, changeMismatchSongs = 0, changeMismatches = 0, changesChecked = 0;
	int sectionFaults = 0, phrasesOver16 = 0, phrasesChecked = 0;
	std::map<int, int> perPass;
	std::map<int, int> appearanceMax;
	const bool phrases_ = !want.empty() && want == "--phrases";
	if (phrases_)
		want.clear();
	int phraseCount = 0, oddLen = 0, uncovered = 0, phrasedSongs = 0;
	std::map<int, int> phraseBars;
	std::map<std::string, int> phraseEnds;
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

			if (cadences_ && !bars.empty()) {
				cadenceSongs++;
				const ChartPlayback pb = chartPlayback(bars);
				// THE MODULE'S OWN CHANGES AND ITS OWN CLASSIFIER, so the census and the chart
				// cannot disagree about what a cadence is.
				const std::vector<ChartChange> changes = chartChanges(bars, pb);
				std::vector<double> closes, any;
				const double beatsPerBar = bars[0].beats > 0 ? bars[0].beats : 4;
				for (size_t i = 1; i < changes.size(); i++) {
					const float held = (i + 1 < changes.size())
						? changes[i + 1].beat - changes[i].beat : pb.totalBeats - changes[i].beat;
					const float barBeats = (float) bars[pb.timeline[changes[i].playedBar].bar].beats;
					const ChartCadence c = chartCadenceOf(changes[i - 1].chord, changes[i].chord,
						held, barBeats);
					if (c == CADENCE_NONE)
						continue;
					cadenceKinds[chartCadenceName(c)]++;
					const float inBar = changes[i].beat - pb.timeline[changes[i].playedBar].startBeat;
					arrivalBeat[inBar < 0.01f ? "on beat 1"
						: (std::fabs(inBar - barBeats / 2.f) < 0.01f ? "mid-bar" : "elsewhere")]++;
					any.push_back(changes[i].beat);
					if (c != CADENCE_HALF && c != CADENCE_DECEPTIVE)
						closes.push_back(changes[i].beat);
				}
				if (closes.empty())
					songsWithNoClose++;
				for (size_t i = 1; i < closes.size(); i++)
					closeGaps[(int) std::lround((closes[i] - closes[i - 1]) / beatsPerBar)]++;
				for (size_t i = 1; i < any.size(); i++)
					anyGaps[(int) std::lround((any[i] - any[i - 1]) / beatsPerBar)]++;
			}

			if (form_ && !bars.empty()) {
				formSongs++;
				const ChartPlayback pb = chartPlayback(bars);
				const std::vector<ChartPhrase> ph = chartPhrases(bars, pb);
				perPass[std::min(64, (int) ph.size())]++;

				// The published changes, as beats from the top of the cycle.
				std::vector<float> published;
				for (const ChartPhrase& one : ph) {
					phrasesChecked++;
					if (one.changes.size() > 16)
						phrasesOver16++;
					for (float c : one.changes)
						published.push_back(one.startBeat + c);
				}
				// The played changes: step through every bar a twelfth of a beat at a time and
				// note where the resolved chord differs from the one before. A place with nothing
				// resolved holds what was sounding, as the module does.
				std::vector<float> played;
				Chord sounding;
				for (int b = 0; b < (int) pb.timeline.size(); b++) {
					const float barBeats = pb.timeline[b].endBeat - pb.timeline[b].startBeat;
					// ON THE BAR'S OWN SLOT GRID, so a boundary is sampled exactly wherever it falls.
					// A fixed twelfth of a beat cannot land on a boundary at four fifths of a beat,
					// and reported the changes in five- and seven-slot bars as disagreements when
					// every one of them was placed right.
					const int perBeat = 12 * std::max(1, (int) bars[pb.timeline[b].bar].slots.size());
					const int samples = (int) std::lround(barBeats * perBeat);
					for (int k = 0; k < samples; k++) {
						// COMPUTED, NOT ACCUMULATED, and nudged just past the grid point. Adding a
						// fraction of a beat over and over lands a hair short of a boundary, which
						// once reported thousands of changes one step late that the chart placed
						// exactly right.
						const float w = (float) k / (float) perBeat + 1e-4f;
						Chord c;
						float toNext = 0.f;
						if (!chartChordAt(bars, pb, b, w, c, toNext))
							continue;
						if (!sounding.valid || c.degree != sounding.degree
							|| c.accidental != sounding.accidental || c.quality != sounding.quality) {
							played.push_back(pb.timeline[b].startBeat + (float) k / (float) perBeat);
							sounding = c;
						}
					}
				}
				bool songBad = false;
				size_t i = 0, j = 0;
				while (i < published.size() || j < played.size()) {
					changesChecked++;
					if (i < published.size() && j < played.size()
						&& std::fabs(published[i] - played[j]) < 0.002f) {
						i++; j++;
						continue;
					}
					songBad = true;
					changeMismatches++;
					if (j >= played.size() || (i < published.size() && published[i] < played[j]))
						i++;
					else
						j++;
				}
				changeMismatchSongs += songBad;

				// Sections: each letter's appearances run one, two, three in order; the phrase
				// number within a section starts at nought at every new appearance and counts up.
				std::map<char, int> lastAppearance;
				char prevSection = 0;
				int prevAppearance = 0, prevInSection = -1;
				for (const ChartPhrase& one : ph) {
					if (one.section == 0)
						continue;
					const bool newAppearance = one.section != prevSection
						|| one.sectionAppearance != prevAppearance;
					if (newAppearance) {
						if (one.sectionAppearance != lastAppearance[one.section] + 1)
							sectionFaults++;
						if (one.phraseInSection != 0)
							sectionFaults++;
						lastAppearance[one.section] = one.sectionAppearance;
						appearanceMax[std::min(16, one.sectionAppearance)]++;
					}
					else if (one.phraseInSection != prevInSection + 1) {
						sectionFaults++;
					}
					prevSection = one.section;
					prevAppearance = one.sectionAppearance;
					prevInSection = one.phraseInSection;
				}
			}

			if (phrases_ && !bars.empty()) {
				const ChartPlayback pb = chartPlayback(bars);
				const std::vector<ChartPhrase> ph = chartPhrases(bars, pb);
				if (!ph.empty()) {
					phrasedSongs++;
					phraseCount += (int) ph.size();
					// CONTIGUOUS AND COMPLETE is the property that matters: phrasing says where
					// phrases begin and end and must never introduce a gap.
					if (ph.front().startBar != 0 || ph.back().endBar != (int) pb.timeline.size())
						uncovered++;
					for (size_t i = 1; i < ph.size(); i++) {
						if (ph[i].startBar != ph[i - 1].endBar)
							uncovered++;
					}
					for (const ChartPhrase& one : ph) {
						const int n = one.endBar - one.startBar;
						phraseBars[n]++;
						phraseEnds[chartCadenceName(one.cadence)]++;
						if (n < 1)
							oddLen++;
					}
				}
			}

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
	if (cadences_) {
		std::printf("%d charts, %d with no closing cadence at all\n\n", cadenceSongs,
			songsWithNoClose);
		std::printf("kinds of cadence:\n");
		for (const auto& kv : cadenceKinds)
			std::printf("  %-26s %6d\n", kv.first.c_str(), kv.second);
		std::printf("\nwhere in the bar the arrival falls:\n");
		for (const auto& kv : arrivalBeat)
			std::printf("  %-26s %6d\n", kv.first.c_str(), kv.second);
		auto show = [](const char* title, const std::map<int, int>& gaps) {
			int total = 0;
			for (const auto& kv : gaps) total += kv.second;
			std::printf("\n%s (%d gaps):\n", title, total);
			int shown = 0;
			for (const auto& kv : gaps) {
				if (kv.first > 16) { shown += kv.second; continue; }
				std::printf("  %2d bars  %6d  %4.1f%%\n", kv.first, kv.second,
					100.0 * kv.second / std::max(1, total));
			}
			if (shown)
				std::printf("  over 16  %6d  %4.1f%%\n", shown, 100.0 * shown / std::max(1, total));
		};
		show("bars between one CLOSING cadence and the next", closeGaps);
		show("bars between ANY cadence and the next, half and deceptive included", anyGaps);
		return 0;
	}
	if (form_) {
		std::printf("%d charts\n\n", formSongs);
		std::printf("chord changes published for phrases, against changes played:\n");
		std::printf("  %d compared, %d disagreements, in %d charts\n", changesChecked,
			changeMismatches, changeMismatchSongs);
		std::printf("\nsection appearances and phrase numbers within sections:\n");
		std::printf("  %d faults\n", sectionFaults);
		std::printf("\nphrases with more chord changes than the cable carries (16): %d of %d\n",
			phrasesOver16, phrasesChecked);
		std::printf("\nphrases in one pass of the form:\n");
		for (const auto& kv : perPass)
			if (kv.second >= 20)
				std::printf("  %2d%s  %5d charts\n", kv.first, kv.first == 64 ? "+" : " ", kv.second);
		return 0;
	}
	if (phrases_) {
		std::printf("rule: a cadence ends a phrase if it closes the section or is %d or more "
			"bars after the last end; fallback %d bars\n\n",
			PHRASE_MIN_BARS, PHRASE_FALLBACK_BARS);
		std::printf("%d songs phrased, %d phrases\n", phrasedSongs, phraseCount);
		std::printf("  empty phrases: %d\n", oddLen);
		std::printf("  charts whose phrases do not cover the cycle: %d\n", uncovered);
		int total = 0;
		for (const auto& kv : phraseBars) total += kv.second;
		std::printf("\nphrase length:\n");
		int over = 0;
		for (const auto& kv : phraseBars) {
			if (kv.first > 16) { over += kv.second; continue; }
			std::printf("  %2d bars  %6d  %4.1f%%\n", kv.first, kv.second,
				100.0 * kv.second / std::max(1, total));
		}
		if (over)
			std::printf("  over 16  %6d  %4.1f%%\n", over, 100.0 * over / std::max(1, total));
		std::printf("\nhow phrases end:\n");
		for (const auto& kv : phraseEnds)
			std::printf("  %-12s %6d  %4.1f%%\n", kv.first.c_str(), kv.second,
				100.0 * kv.second / std::max(1, total));
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
