/** See BrassTable.hpp. */
#include "BrassTable.hpp"

#include <cmath>

namespace px {


BrassTablePoint brassTableAt(const BrassTable& table, const BrassTableNote& note, double mouth) {
	const BrassTablePoint* p = table.points + note.first;
	if (note.count <= 0)
		return BrassTablePoint{(float) mouth, 1.0f, note.resonanceHz * 1.011f, 0.0f, 1.0f};
	if (mouth <= p[0].mouth)
		return p[0];
	if (mouth >= p[note.count - 1].mouth)
		return p[note.count - 1];
	int i = 0;
	while (i + 2 < note.count && p[i + 1].mouth < mouth)
		i++;
	const double u = std::log(mouth / p[i].mouth) / std::log(p[i + 1].mouth / p[i].mouth);
	auto mix = [&](float a, float b) { return (float) (a + u * (b - a)); };
	return BrassTablePoint{(float) mouth, mix(p[i].lips, p[i + 1].lips), mix(p[i].hz, p[i + 1].hz), mix(p[i].level, p[i + 1].level),
		mix(p[i].centroid, p[i + 1].centroid)};
}


double brassTablePressureFor(const BrassTable& table, const BrassTableNote& note, double level) {
	const BrassTablePoint* p = table.points + note.first;
	if (note.count <= 0)
		return note.threshold;
	if (level <= p[0].level)
		return p[0].mouth;
	if (level >= p[note.count - 1].level)
		return p[note.count - 1].mouth;
	// The level rises with the pressure; the first span that reaches it.
	int i = 0;
	while (i + 2 < note.count && p[i + 1].level < level)
		i++;
	const double span = p[i + 1].level - p[i].level;
	const double u = span > 1e-6 ? (level - p[i].level) / span : 0.0;
	return p[i].mouth * std::pow((double) p[i + 1].mouth / p[i].mouth, std::fmin(1.0, std::fmax(0.0, u)));
}


} // namespace px
