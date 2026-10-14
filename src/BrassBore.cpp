/** See BrassBore.hpp. */
#include "BrassBore.hpp"

#include <cmath>

namespace px {

using cd = std::complex<double>;


BrassAir::BrassAir(double degrees) : celsius(degrees) {
	const double kelvin = 273.15 + degrees;
	c = 331.45 * std::sqrt(kelvin / 273.15);
	rho = 1.2929 * 273.15 / kelvin;
	// Keefe's (1984) linear fit about 300 K.
	eta = 1.846e-5 * (1.0 + 0.0025 * (kelvin - 300.0));
}


BrassBore BrassBore::trombone(double extension, double tuningPull) {
	BrassBore b;
	// THE MOUTHPIECE, A TENOR'S: a cup of 5 cubic centimetres; a throat 6.6 mm across and 7 mm long and a
	// backbore widening from it to 11 mm over 70 mm, lumped as one inertance, about 1700 kg a metre to the
	// fourth, so that the cup and throat resonate together near 650 Hz.
	b.mouthpiece.cupVolume = 5.0e-6;
	b.mouthpiece.inertance = 1.2047 * (0.007 / (M_PI * 0.0033 * 0.0033) + 0.070 / (M_PI * 0.0033 * 0.0055));
	b.mouthpiece.resistance = 2.0e5;
	auto cylinder = [&](double length, double r) {
		if (length > 0.0)
			b.sections.push_back(BrassSection{length, r, r});
	};
	cylinder(0.708, 0.0069);                 // inner slide, top
	cylinder(extension, 0.0072);             // outer slide, top, drawn out
	cylinder(0.177, 0.0074);                 // the crook
	cylinder(extension, 0.0072);             // outer slide, bottom, drawn out
	cylinder(0.711, 0.0069);                 // inner slide, bottom
	cylinder(0.241, 0.0071);                 // gooseneck
	// The tuning slide, widening, with what it is pulled out added at its narrow end.
	cylinder(2.0 * tuningPull, 0.0075);
	b.sections.push_back(BrassSection{0.254, 0.0075, 0.0107});
	// THE BELL, a Bessel horn a(x) = b (x + x0) to the minus gamma, x from the mouth: b 0.0063, x0 0.0174,
	// gamma 0.7, 0.502 m long. As frustums 5 mm long, which the calculation divides finer.
	const double bellLength = 0.502, bb = 0.0063, x0 = 0.0174, flare = 0.7;
	auto radius = [&](double fromMouth) { return bb * std::pow(fromMouth + x0, -flare); };
	const int pieces = 100;
	for (int i = 0; i < pieces; i++) {
		const double near = bellLength * (1.0 - (double) i / pieces), far = bellLength * (1.0 - (double) (i + 1) / pieces);
		b.sections.push_back(BrassSection{bellLength / pieces, radius(near), radius(far)});
	}
	return b;
}


BrassBore BrassBore::tromboneWhole(double extension, double tuningPull) {
	BrassBore b = trombone(extension, tuningPull);
	b.lumped = false;
	std::vector<BrassSection> front;
	const double rim = 0.01275;
	front.push_back(BrassSection{5.0e-6 / (M_PI * rim * rim), rim, rim});    // the cup
	front.push_back(BrassSection{0.007, 0.00335, 0.00335});                  // the throat
	front.push_back(BrassSection{0.070, 0.00335, 0.0055});                   // the backbore
	front.push_back(BrassSection{0.230, 0.0055, 0.0069});                    // the leadpipe
	// The leadpipe sits in the first 0.23 m of the inner slide.
	b.sections[0].length -= 0.230;
	b.sections.insert(b.sections.begin(), front.begin(), front.end());
	return b;
}


BrassBore BrassBore::tromboneTimed(double extension, double tuningPull) {
	BrassBore b = trombone(extension, tuningPull);
	b.mouthpiece.cupVolume = 5.0e-6;
	// THE THROAT, 7 mm at 3.35 mm radius, as an inertance: the density times its length over its area. The
	// backbore stays a tube: lumped with the throat its 70 mm went missing, and every resonance came out
	// 25 to 65 cents sharp of the whole.
	b.mouthpiece.inertance = 0.0;   // from the air: see mouthpieceInertance
	b.inertanceFromGeometry = true;
	b.sections[0].length -= 0.230;
	const BrassSection front[2] = {BrassSection{0.070, 0.00335, 0.0055}, BrassSection{0.230, 0.0055, 0.0069}};
	b.sections.insert(b.sections.begin(), front, front + 2);
	return b;
}


double BrassBore::mouthpieceInertance(const BrassAir& air) const {
	if (!inertanceFromGeometry)
		return mouthpiece.inertance;
	return air.rho * 0.007 / (M_PI * 0.00335 * 0.00335);
}


double BrassBore::length() const {
	double l = 0.0;
	for (const BrassSection& s : sections)
		l += s.length;
	return l;
}


void brassLossyLine(double r, double omega, const BrassAir& air, cd* series, cd* shunt) {
	const double area = M_PI * r * r;
	const cd root = std::sqrt(cd(0.0, omega));
	const cd heavier = 1.0 + 2.0 * std::sqrt(air.nu()) / (r * root);
	const cd softer = 1.0 + 2.0 * (air.gamma - 1.0) * std::sqrt(air.nu() / air.prandtl) / (r * root);
	*series = cd(0.0, omega) * air.rho / area * heavier;
	*shunt = cd(0.0, omega) * area / (air.rho * air.c * air.c) * softer;
}


cd brassRadiationImpedance(double a, double hz, const BrassAir& air, BrassRadiation how) {
	const double area = M_PI * a * a, z0 = air.rho * air.c / area;
	const double omega = 2.0 * M_PI * hz, ka = omega / air.c * a;
	if (how == BrassRadiation::PARALLEL_RL) {
		const cd jwl = cd(0.0, omega * air.rho * 0.6133 * a / area);
		return jwl * z0 / (z0 + jwl);
	}
	// Silva et al. 2009, unflanged: the modulus of the reflection and the end correction.
	const double modulus = (1.0 + 0.2 * ka - 0.084 * ka * ka) / (1.0 + 0.2 * ka + (0.5 - 0.084) * ka * ka);
	const double s = std::sin(2.0 * ka);
	const double delta = a * (0.6133 * (1.0 + 0.044 * ka * ka) / (1.0 + 0.19 * ka * ka) - 0.02 * s * s);
	const cd reflection = -modulus * std::exp(cd(0.0, -2.0 * omega / air.c * delta));
	return z0 * (1.0 + reflection) / (1.0 - reflection);
}


/** THE IMPEDANCE AT THE START of a lossy frustum, given the impedance at its end: as cylinders no longer
than 2 mm, each at its own middle radius. */
static cd throughSection(const BrassSection& s, cd zEnd, double omega, const BrassAir& air) {
	const int steps = std::max(1, (int) std::ceil(s.length / 0.002));
	const double piece = s.length / steps;
	cd z = zEnd;
	for (int i = steps - 1; i >= 0; i--) {
		const double r = s.r0 + (s.r1 - s.r0) * ((double) i + 0.5) / steps;
		cd series, shunt;
		brassLossyLine(r, omega, air, &series, &shunt);
		const cd gamma = std::sqrt(series * shunt), zc = std::sqrt(series / shunt);
		const cd ch = std::cosh(gamma * piece), sh = std::sinh(gamma * piece);
		z = (ch * z + zc * sh) / (sh / zc * z + ch);
	}
	return z;
}


cd BrassBore::impedanceFrom(size_t first, double hz, const BrassAir& air) const {
	const double omega = 2.0 * M_PI * hz;
	cd z = brassRadiationImpedance(mouthRadius(), hz, air, radiation);
	for (size_t i = sections.size(); i-- > first; )
		z = throughSection(sections[i], z, omega, air);
	return z;
}


cd BrassBore::impedance(double hz, const BrassAir& air) const {
	const double omega = 2.0 * M_PI * hz;
	const cd bore = impedanceFrom(0, hz, air);
	if (!lumped)
		return bore;
	const cd throat = cd(mouthpiece.resistance, omega * mouthpieceInertance(air));
	const cd cup = cd(0.0, omega * mouthpiece.cupVolume / (air.rho * air.c * air.c));
	return 1.0 / (cup + 1.0 / (throat + bore));
}


void brassPeaks(const std::vector<double>& hz, const std::vector<double>& magnitude,
                std::vector<double>* peakHz, std::vector<double>* peakMagnitude) {
	for (size_t i = 1; i + 1 < hz.size(); i++) {
		if (!(magnitude[i] > magnitude[i - 1] && magnitude[i] >= magnitude[i + 1]))
			continue;
		const double a = std::log(magnitude[i - 1]), b = std::log(magnitude[i]), c = std::log(magnitude[i + 1]);
		const double den = a - 2.0 * b + c;
		const double shift = den != 0.0 ? 0.5 * (a - c) / den : 0.0;
		const double step = (shift >= 0.0 ? hz[i + 1] - hz[i] : hz[i] - hz[i - 1]);
		peakHz->push_back(hz[i] + shift * step);
		peakMagnitude->push_back(std::exp(b - 0.25 * (a - c) * shift));
	}
}


} // namespace px
