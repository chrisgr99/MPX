/** See BowedTable.hpp. */
#include "BowedTable.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace px {


bool BowedTable::load(const std::string& path, const std::string& instrument) {
	cells.clear();
	std::ifstream in(path);
	if (!in)
		return false;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		std::istringstream words(line);
		std::string who, kind, ringing, start;
		int string = 0, step = 0, level = 0;
		Start s;
		if (!(words >> who >> kind >> string >> step >> level >> ringing >> start >> s.a >> s.b >> s.c))
			continue;
		if (who != instrument)
			continue;
		const int k = kind == "fresh" ? FRESH : (kind == "cross" ? CROSS : (kind == "change" ? CHANGE : -1));
		const int r = ringing == "silent" ? SILENT : (ringing == "faint" ? FAINT : (ringing == "ringing" ? RINGING : NOT_ASKED));
		if (k < 0)
			continue;
		s.kind = start == "bite" ? 'b' : (start == "glide" ? 'g' : (start == "change" ? 'c' : 0));
		if (!s.kind)
			continue;
		cells[key(k, string, step, level, r)] = s;
	}
	return !cells.empty();
}


const BowedTable::Start* BowedTable::find(Kind kind, int string, int semitone, int level, Ringing ringing) const {
	const auto it = cells.find(key(kind, string, std::max(0, semitone) / 3, level, kind == CHANGE ? NOT_ASKED : ringing));
	return it == cells.end() ? nullptr : &it->second;
}


BowedTable::Ringing BowedTable::ringingOf(double meanSquare) {
	const double db = 10.0 * std::log10(meanSquare + 1e-30);
	if (db < -90.0)
		return SILENT;
	if (db < -50.0)
		return FAINT;
	return RINGING;
}


} // namespace px
