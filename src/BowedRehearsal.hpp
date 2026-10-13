#pragma once
/** THE PLAYER WHO REHEARSES: how a bowed note starts, judged, and a player who tries attacks on a copy
of the string before playing one. Phase 3 of docs/bowed-strings.md found that almost no plain attack
starts cleanly under the friction curve, and that a player who rehearses each one on a copy of the
string, exactly as it stands, starts nearly all of them within a few periods; phase 7 gives the same
player every bow change. Here rather than in a bench so that the player and the module can use it.

NO RACK IN HERE. Everything runs at the string's own sample rate, whatever it is oversampled to. */
#include "BowedString.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace px {


/** SLIPPING, JUDGED FROM THE MOTION, as the measurements judge it, and not from the friction
model's own state: a slip is a flyback, the string going back against the bow at half the speed of
an ideal Helmholtz slip or more (the bow's speed over beta), or, while the bow is too slow for that
to mean anything, at more than the slip a twentieth of the bow force would cause. It ends when the
string has all but caught the bow again. Under the friction curve this agrees with the junction's
stuck flag, which marks the moment of release exactly and is used there instead; under bristles
the string can creep, sliding slowly with every bristle broken, which is not a slip; and under the
thermal friction the stuck flag flickers, the slip's flat friction letting the string catch the bow for
a sample as a ripple passes, which is not a slip either. */
struct SlipWatch {
	bool slipping = false;
	bool update(const BowedString& s, float force, float bowSpeed) {
		const float dir = bowSpeed < 0.f ? -1.f : 1.f;
		const float back = dir * (bowSpeed - s.velocityAtBow());
		const float ideal = std::fabs(bowSpeed) / std::fmax(0.01f, s.contact());
		const float floor = 0.05f * force / (2.f * s.impedance());
		const float start = std::fmax(0.5f * ideal, floor);
		slipping = slipping ? back > 0.2f * start : back > start;
		return slipping;
	}
};


/** WHAT A NOTE STARTED FROM REST DID. The bow is put on the string at its force and drawn from
standing still at a constant acceleration, as in Guettler's analysis and in the measurements. */
struct Attack {
	/** When the string first let go of the hair, in seconds; -1 if it never did. */
	double firstSlip = -1.0;
	/** NOMINAL PERIODS FROM THE FIRST SLIP UNTIL THE TRIGGERING TURNS REGULAR — one slip every
	period from then to the end of the run. Nought is Guettler's perfect attack. -1 if it never
	turned regular within the run. */
	int periodsBefore = -1;
	/** What went wrong first, when something did: a period too long (the string held on past the
	release, which is the creak Guettler calls prolonged periods), or too short (it let go early,
	multiple slipping). */
	bool prolonged = false;
	/** The friction coefficient while slipping: in the first slip, and in the later slips before
	the first one's reflection comes back from the nut. What Guettler's limits are worked out with. */
	double muFirst = 0.0, muLater = 0.0;
	/** The string's speed against the bow just after it first lets go, in metres a second. */
	double slipSpeed = 0.0;
	/** How long the transient lasted, in seconds: the first slip to regular triggering. */
	double seconds = 0.0;
};

/** ONE ATTACK on `s`, at its own sample rate: the bow at `force` from the first sample, drawn from
standing still at `accel` up to `topSpeed`, run until `periodsAfter` nominal periods after the first
slip or three seconds. `bridge`, when given, collects the force on the bridge. A gap between slips
counts as regular within `regular` of the period; with `firstRun` nought, regular means regular from
some slip to the end of the run, and with more, the first slip followed by that many regular gaps. */
Attack attackFromRest(BowedString& s, float force, float accel, double hz, int periodsAfter,
                      std::vector<float>* bridge = nullptr, float topSpeed = 1e9f,
                      double regular = 0.1, size_t firstRun = 0);

/** HOW ROUGH A START IS: in the first 80 ms after the string first lets go, the energy above two
kilohertz over the energy below one, through second-order filters. A clean start is no brighter
than the note it becomes; a rough one, its first periods irregular or pressed too hard, is several
times brighter. Cheap enough to measure on every attack the player tries. `x` at `rate`. */
double startRoughness(const std::vector<float>& x, double firstSlip, double rate);

/** HOW MUCH A NOTE SQUEAKS: the energy between three and ten kilohertz that lies off the note's
harmonics — further from every one than `tolerance`, a fraction, allows — against the energy on them,
in decibels. A squeak is the short length of string between the bow and the bridge ringing at its own
pitch, a few kilohertz and no harmonic of the note: a pure tone where none belongs, which the ear finds
at once even 25 dB down. `x` the string's push on the bridge at `rate`; it is averaged down to near 48
kHz and its spectrum taken whole. */
double squeakOf(const std::vector<float>& x, double rate, double hz, double tolerance);

/** HOW ROUGH A NOTE IS: one less the best normalised correlation of each four periods with the four
after them, the lag sought within three per cent, averaged, in decibels. A bow that grips and lets go
cleanly repeats itself period after period, about -45 to -60 dB in the radiated sound; the rasp heard in
the Fauré's pianissimo scored -24 dB, and the slip count passed it, since its slips still came about
once a period. For a note without vibrato, which changes the period on purpose. `x` at `rate` from
sample `from`; averaged down to near 48 kHz first but never below a hundred samples a period, the lag
found to the sample and then between samples by golden section, the later stretch read by straight
lines between samples. */
double roughnessOf(const std::vector<float>& x, double rate, double hz, size_t from = 0);

/** WHAT THE PLAYER CHOSE: where the bow goes, how much harder than the note wants it starts, the
bow's acceleration, how many periods that attack takes to turn regular, how rough its start, and the
soonest any attack it tried at the planned position turned regular. */
struct Rehearsed {
	float beta = 0.1f;
	double bite = 1.0;
	float accel = 0.f;
	int periods = -1;
	double roughness = 0.0;
	int soonest = -1;
	/** How much it squeaked, as played on the instrument; see squeakOf. */
	double squeak = 0.0;
};

/** A PLAYER WHO REHEARSES EACH NOTE on a copy of the string before playing it (phase 3 of
docs/bowed-strings.md): seven starting forces from the note's own to four times it, each at
twenty-four bow accelerations scaled as Guettler's limits scale, beta (1 - beta) F / (T Z). Of the
attacks that turn regular within five periods, the smoothest start; if none does, the soonest. The
finer search and the smoothness were settled by ear on the violin's open G and D: the coarser
search, twelve accelerations at four forces taking the soonest, found only attacks at four times
the force, and they started harshly.

WHERE NOTHING TURNS REGULAR WITHIN THREE PERIODS, A LITTLE NEARER THE BRIDGE OR FURTHER FROM IT: the
search again at 0.7, 0.85, 1.2 and 1.4 of the planned bow position, more finely — thirteen forces
to eight times the note's, forty accelerations — and the smoothest of the attacks found anywhere
that turn regular within three periods, played where it was found. Settled by ear on the cello's open C, whose
clean starts are so few that at a tenth of the length the best took five periods, about 76 ms;
at 0.085 one starts at once, and sounded best.

EVERY ATTACK IT TRIED, BEST FIRST, on a copy of `model`, tuned, stopped and weighted as it will be
played, at the model's own sample rate. The smoothest of those that turn regular within five
periods lead, if the best of them is regular within three; otherwise the second search's smoothest
regular within three. Then the rest by how soon they turned regular. A player in an instrument plays
the first of these that starts cleanly there, since a string on a bridge that moves does not start
quite as it does alone. */
struct Crew;
/** `crew`, where there is one, shares the trials out over its threads; the list is the same. */
std::vector<Rehearsed> rehearseShortlist(const BowedString& model, double hz, float beta,
                                         float force, float speed, Crew* crew = nullptr);


} // namespace px
