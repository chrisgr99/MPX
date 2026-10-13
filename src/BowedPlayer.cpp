/** See BowedPlayer.hpp. */
#include "BowedPlayer.hpp"
#include "Crew.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <cmath>

namespace px {


/** HOW FAR INTO A NOTE FROM THE STRING AN ATTACK IS JUDGED FOR ROUGHNESS, seconds: most of a short
note, and most of what is heard of a long one before the vibrato comes in. */
static const double ROUGH_UNTIL = 0.33;
/** AN ATTACK NO ROUGHER THAN THIS, dB in the string's push on the bridge, plays cleanly. */
static const double ROUGH_CLEAN = -35.0;
/** HOW LONG THE START OF A NOTE IS, for its click, seconds; and how much brighter than what follows it
may be, dB above 4 kHz, before it is heard as a pluck. The cello's D3 at piano, started with a click
at -42 dB against its body where its neighbours were -50 to -65, was heard as "a slight very
high-pitched plucking sound as the note is beginning". */
static const double CLICK_SECONDS = 0.02;
static const double CLICK_CLEAN = 0.0;
/** HOW FAR UNDER ITS FULL LEVEL A NOTE MAY STILL BE 60 TO 100 MS IN, dB, and be heard as starting rather
than swelling in. Judged on roughness and click alone, the cello's F#3 and G#3 were given starts that
spoke on time and took 0.4 s to reach their level, and in a passage of separate notes they were the weak
ones. */
static const double RISE_PROMPT = -3.0;
/** HOW MANY PERIODS AN ATTACK MAY TAKE TO COME TO ONE SLIP A PERIOD, when none starts on time, and still
be counted as starting. */
static const int SETTLES = 10;
/** HOW OFTEN AN ATTACK'S LEVEL IS READ, seconds, over 10 ms each time. */
static const double ENVELOPE_HOP = 0.005;
/** A GAP BETWEEN TWO NOTES SHORTER THAN THIS IS NO GAP: the bow goes on from one to the next. */
static const double JOINED = 0.03;
/** HOW LONG A NOTE WITH NOTHING AFTER IT TAKES TO END: the bow slows and lifts over this. */
static const double RELEASE = 0.04;
/** HOW SOON A NOTE FROM THE STRING HAS TO SPEAK, seconds: its first slip. An attack eased in so
gently that the string has not caught the bow by then is a note starting late, however clean. */
static const double SPEAKS = 0.03;
/** HOW LONG A FINGER TAKES TO LAND, seconds: the pitch moves to the new note over it rather than
jumping, which clicked. */
static const double LANDING = 0.006;
/** HOW LONG THE BOW TAKES TO SETTLE ONTO THE STRING at the start of a note, seconds: its force rises
over this rather than arriving in one sample. On a silent string it makes no difference; on one still
ringing from the note before, a bow clamped onto the moving string in a sample stopped it dead, and
the cello's D3 after C3 on the same string began with a burst above 6 kHz 50 dB over a start from
silence — heard as "a slight very high-pitched plucking sound as the note is beginning". */
static const double BOW_LANDS = 0.004;
/** WHEN A NOTE STARTED AWAY FROM ITS PLACE GOES BACK TO IT, and how long the move takes, seconds. */
static const double RETURN_AFTER = 0.08, RETURN_OVER = 0.12;


BowedPlayer::Habits BowedPlayer::violinHabits() {
	Habits h;
	// CHOSEN BY EAR in the Fauré's third movement, bars 6 to 9, as the viola's were and the same three
	// (see violaHabits), against the violin as it was, each of them alone, the bow further from the
	// bridge and a darker body: the cello's lighter bow, its notes evened out — notes marked alike had
	// been up to 9 dB apart, and came to within 3 — and quiet before quick.
	h.evenness = 1.f;
	h.startSqueakMost = -10.f;
	// LESS BRIGHT, AS A PLAYER MAKES IT: the bow an eighth further from the bridge at every dynamic,
	// where the string vibrates with fewer of its upper harmonics, the force lightened in step since the
	// string takes less of it there before it turns raucous, and the soft dynamics drawn faster so they
	// stay full and start surely — chosen by ear in the same bars, of the bow an eighth and a quarter
	// further, with and without the force lightened, only the soft dynamics moved, and the soft drawn
	// faster by two amounts. The top above 8 kHz 3 dB down, the worst start's squeak from 10 dB under
	// the note to 14, the pianissimo from 18.6 dB under the loudest notes to 16.8, and a quiet A flat
	// from speaking in 110 ms to 42.
	h.forcePiano = 1.244f;
	h.forceForte = 1.956f;
	h.speedPiano = 0.18f;
	h.speedPianissimo = 0.16f;
	h.betaPiano *= 1.125f;
	h.betaForte *= 1.125f;
	h.betaLeast *= 1.125f;
	h.betaMost *= 1.125f;
	h.betaPianissimo *= 1.125f;
	return h;
}


/** THE VIOLIST: the violinist's habits — the bow's place a share of the string, its force a multiple of
the string's impedance, both of which carry over to heavier strings by their own measure — with a viola
bow's shorter hair, 0.63 m against 0.65, and a vibrato between the violinist's and the cellist's,
violists having been measured slightly slower and narrower than violinists (MacLeod 2008, the figures
not published in what could be read). See research/bowed-strings/viola-sources.md. */
BowedPlayer::Habits BowedPlayer::violaHabits() {
	Habits h;
	h.bowLength = 0.63f;
	h.vibratoHz = 5.7f;
	h.vibratoCents = 18.f;
	// CHOSEN BY EAR in the Fauré's third movement, bars 6 to 9, against versions with a darker body, a
	// fuller one, and the bow further from the bridge: the cello's lighter bow, 1.4 impedances at piano
	// and 2.2 at forte against the violinist's 1.8 and 2.5; its notes evened out as the cello's are,
	// which took the spread between notes marked alike from 4.5 and 5.4 dB to 3 and 3.6, and a quiet E
	// flat on the C string, whose second and third harmonics sit on the body's modes at 310 and 448 Hz,
	// from 4.5 dB over its neighbours to within 1 to 3; and, of starts from the string that do not begin
	// within three periods, a quiet one before a quick one (startSqueakMost), which took a crossing to C4
	// from speaking in 101 ms to 36 and the passage's first note from squeaking 4 dB under itself to 20.
	h.forcePiano = 1.4f;
	h.forceForte = 2.2f;
	h.evenness = 1.f;
	h.startSqueakMost = -10.f;
	return h;
}


/** THE CELLIST: a shorter bow, and a slower, narrower vibrato, chosen by ear on the cello's D string
(bow-changes.md) and narrower than the violin's as the measurements have it. */
BowedPlayer::Habits BowedPlayer::celloHabits() {
	Habits h;
	// THE CELLO'S BOW IS LIGHTER, against its strings' impedance, than the violin's. Bowed steadily on
	// every string at two speeds, four places and six forces, its G, D and A strings play cleanly up to
	// about 1.8 impedances at a slow bow and 3 at a fast one near the bridge, and turn rough fast above
	// that: the violin's 1.8 at piano and 2.5 at forte sat at the edge. Its pianissimo is lighter still
	// and stays at piano's place, since far from the bridge only the lightest bow holds (research/
	// bowed-strings/bow-changes.md).
	h.forcePiano = 1.4f;
	h.forceForte = 2.2f;
	// ITS QUIET END NEARER THE BRIDGE AND DRAWN FASTER. Out at an eighth to a sixth of the string the
	// cello's piano was weak and muffled by ear, 4 to 6 dB quieter and 2 to 3 dB darker held, and its
	// quiet bars sat 18 to 20 dB under its loud ones; of three tried at one level, Chris chose this:
	// the bow at about a ninth, never past an eighth, piano at 0.2 m/s and pianissimo at 0.16.
	h.speedPiano = 0.2f;
	h.betaPiano = 0.11f;
	h.betaMost = 0.13f;
	h.speedPianissimo = 0.16f;
	h.forcePianissimo = 1.2f;
	h.betaPianissimo = 0.11f;
	// EVENED OUT: in the Fauré's quiet bars some notes came out weaker than their neighbours by ear —
	// the cello's body radiates 146.8 Hz 15 dB less than 138.6 — and a player draws those a little
	// harder.
	h.evenness = 1.f;
	h.bowLength = 0.62f;
	h.vibratoHz = 5.5f;
	h.vibratoCents = 15.f;
	// ITS TWO STARTS: the glide, and the moderate bite at the planned place that its rehearsal chose most
	// often.
	h.guessTurn = 0.02f;
	h.twoStarts = FewStarts();
	h.twoStarts.bites = {1.59f};
	h.twoStarts.accels = {-41.f};
	h.twoStarts.shares = {1.f};
	h.twoStarts.glideRamps = {0.02f};
	h.twoStarts.glideFloors = {0.2f};
	return h;
}


void BowedPlayer::setInstrument(const BowedInstrument::Design& design, float engineRate, float highestHz) {
	engine = engineRate;
	factor = BowedString::oversampling(highestHz, engineRate);
	rate = (double) engineRate * factor;
	live = State();
	live.inst.setDesign(design);
	live.inst.setRate(engineRate, factor);
	// THE INSTRUMENT'S MIDDLE, for evenness: the median of what every note of the first position on
	// every string is expected to give.
	{
		std::vector<double> all;
		for (size_t k = 0; k < design.strings.size(); k++)
			for (int semitone = 0; semitone < 12; semitone++)
				all.push_back(expectedDb(design.strings[k].openHz * std::pow(2.f, semitone / 12.f), (int) k));
		std::sort(all.begin(), all.end());
		evenRef = all.empty() ? 0.0 : all[all.size() / 2];
	}
	for (int k = 0; k < live.inst.strings(); k++) {
		live.voices[k].fingerHz = design.strings[(size_t) k].openHz;
		live.voices[k].distance.value = 0.1f * design.length;
	}
	// AT REST FROM THE START: nothing is bowed and nothing rings, so there is nothing to step until the
	// first note — and a section's players that have nothing to play cost nothing from the first.
	live.asleep = true;
	upcoming.clear();
	played.clear();
	history.clear();
	memory.assign((size_t) (4 * MEMORY_SEMITONES * 8 * 2), Remembered());
	anyLast = false;
	lastReleased = true;
	releaseAt = -1;
	bowAt = 0.5 * habits.bowLength;
	direction = -1;
	strokeShare = 1.0;
	random = habits.seed ? habits.seed : 1u;
}


void BowedPlayer::add(const Note& n, double at) {
	addAt(n, (int64_t) std::llround(at * rate));
}


/** A NOTE THE INSTRUMENT CANNOT SOUND, moved by octaves until it can: up from below the lowest open
string, as a violinist reading a cello's line plays it an octave higher, and down from past the end of
the fingerboard on the highest. A hair under the open string is the open string, as stringFor takes
it. */
float BowedPlayer::playable(float hz) const {
	const BowedInstrument::Design& d = live.inst.design();
	const float low = d.lowestHz(), high = d.highestHz();
	if (!(hz > 0.f) || !(low > 0.f))
		return hz;
	while (hz < low * 0.9995f)
		hz *= 2.f;
	while (hz > high * 1.0005f && 0.5f * hz >= low * 0.9995f)
		hz *= 0.5f;
	return hz;
}


void BowedPlayer::addAt(const Note& n, int64_t start) {
	Pending p;
	p.note = n;
	p.note.hz = playable(n.hz);
	p.start = start;
	// IN THE ORDER THEY START, whatever order they come in: a note played from a keyboard now goes
	// before the notes a score has announced for later.
	auto at = std::upper_bound(upcoming.begin(), upcoming.end(), p,
		[](const Pending& x, const Pending& y) { return x.start < y.start; });
	upcoming.insert(at, p);
}


void BowedPlayer::endNow() {
	if (!anyLast || lastReleased)
		return;
	owner = decisions++;
	planRelease(live.clock);
	lastReleased = true;
	releaseAt = live.clock;
	if (mode == PLAYING)
		lost = true;
}


void BowedPlayer::withdraw() {
	upcoming.clear();
	// THE FIRST DECISION WHOSE MOMENT HAS NOT COME, and everything from it on: its actions taken out,
	// and the bookkeeping put back to how it stood before it.
	size_t k = 0;
	while (k < history.size() && history[k].start <= live.clock)
		k++;
	if (k < history.size()) {
		const int64_t from = history[k].serial;
		giveLine(history[k].before);
		live.actions.erase(std::remove_if(live.actions.begin(), live.actions.end(),
			[from](const Action& a) { return a.owner >= from; }), live.actions.end());
		history.erase(history.begin() + (long) k, history.end());
		const double now = (double) live.clock / rate;
		while (!played.empty() && played.back().at > now)
			played.pop_back();
	}
	if (mode == PLAYING)
		lost = true;
}


BowedPlayer::Line BowedPlayer::takeLine() const {
	Line l;
	l.bowAt = bowAt;
	l.direction = direction;
	l.strokeShare = strokeShare;
	l.anyLast = anyLast;
	l.last = last;
	l.lastString = lastString;
	l.lastStart = lastStart;
	l.lastEnd = lastEnd;
	l.lastForce = lastForce;
	l.lastSpeed = lastSpeed;
	l.lastReleased = lastReleased;
	l.releaseAt = releaseAt;
	l.random = random;
	l.retake = retake;
	return l;
}


void BowedPlayer::giveLine(const Line& l) {
	bowAt = l.bowAt;
	direction = l.direction;
	strokeShare = l.strokeShare;
	anyLast = l.anyLast;
	last = l.last;
	lastString = l.lastString;
	lastStart = l.lastStart;
	lastEnd = l.lastEnd;
	lastForce = l.lastForce;
	lastSpeed = l.lastSpeed;
	lastReleased = l.lastReleased;
	releaseAt = l.releaseAt;
	random = l.random;
	retake = l.retake;
}


int BowedPlayer::semitoneOf(float hz, int string) const {
	const float open = live.inst.design().strings[(size_t) string].openHz;
	return std::max(0, (int) std::lround(12.0 * std::log2((double) hz / (double) open)));
}


int BowedPlayer::levelStepOf(float level) {
	return std::max(0, std::min(7, (int) std::lround(level * 7.f)));
}


int BowedPlayer::memoryIndexFor(float hz, float level, int string, int change) const {
	const float open = live.inst.design().strings[(size_t) string].openHz;
	const int semitone = std::max(0, std::min(MEMORY_SEMITONES - 1,
		(int) std::lround(12.0 * std::log2((double) hz / (double) open))));
	const int step = std::max(0, std::min(7, (int) std::lround(level * 7.f)));
	return memoryIndex(string, semitone, step, change);
}


const BowedPlayer::Remembered* BowedPlayer::recall(int index) const {
	if (index < 0 || index >= (int) memory.size())
		return nullptr;
	if (memory[(size_t) index].known)
		return &memory[(size_t) index];
	// THE NEAREST FOUND: on the same string at the same dynamic a few semitones either way, then a
	// step of dynamic either way.
	const int change = index % 2, step = (index / 2) % 8, semitone = (index / 16) % MEMORY_SEMITONES;
	const int string = index / (16 * MEMORY_SEMITONES);
	for (int d = 1; d <= 4; d++)
		for (int sign : {-1, 1}) {
			const int t = semitone + sign * d;
			if (t < 0 || t >= MEMORY_SEMITONES)
				continue;
			const Remembered& r = memory[(size_t) memoryIndex(string, t, step, change)];
			if (r.known)
				return &r;
		}
	for (int d = 1; d <= 2; d++)
		for (int sign : {-1, 1}) {
			const int l = step + sign * d;
			if (l < 0 || l > 7)
				continue;
			const Remembered& r = memory[(size_t) memoryIndex(string, semitone, l, change)];
			if (r.known)
				return &r;
		}
	return nullptr;
}


void BowedPlayer::remember(int index, const Remembered& r) {
	if (index < 0 || index >= (int) memory.size())
		return;
	memory[(size_t) index] = r;
	memory[(size_t) index].known = true;
}


float BowedPlayer::noise() {
	random ^= random << 13;
	random ^= random >> 17;
	random ^= random << 5;
	return (float) (random % 20001u) / 10000.f - 1.f;
}


// ---- sample by sample ---------------------------------------------------------------------------

void BowedPlayer::schedule(State& s, const Action& given) {
	Action a = given;
	// ON THE LIVE STATE, tagged with the decision it belongs to, and kept where a planner is keeping
	// its decision for the playing copy.
	if (&s == &live) {
		a.owner = owner;
		if (capturing)
			captured.push_back(a);
	}
	auto at = std::upper_bound(s.actions.begin(), s.actions.end(), a,
		[](const Action& x, const Action& y) { return x.at < y.at; });
	s.actions.insert(at, a);
}


void BowedPlayer::apply(State& s, const Action& a, double rate) {
	Voice& v = s.voices[a.string];
	switch (a.kind) {
		case Action::FORCE:
			v.follow = false;
			v.force.to(a.value, a.samples);
			break;
		case Action::FOLLOW:
			v.follow = true;
			v.followForce = a.value;
			v.followFloor = a.a;
			v.followSpeed = std::fmax(0.05f, a.b);
			v.followFrom = a.at;
			break;
		case Action::SPEED:
			v.attacking = false;
			v.speed.to(a.value, a.samples);
			break;
		case Action::DISTANCE:
			v.distance.to(a.value, a.samples);
			break;
		case Action::FINGER: {
			// The old note lingering, copied before the finger moves (see SlurSmoothing).
			if (a.b > 0.f)
				s.inst.linger(a.string, a.b);
			v.glideSmooth = a.c == 2;
			const float open = s.inst.design().strings[(size_t) a.string].openHz;
			v.glideFrom = s.inst.string(a.string).frequency();
			v.fingerHz = a.value;
			v.stopped = a.value > open * 1.0005f;
			v.glideAt = a.at;
			v.glideLength = a.samples;
			if (a.samples <= 0)
				s.inst.stop(a.string, a.value);
			break;
		}
		case Action::VIBRATO:
			v.vibratoCents = a.value;
			v.vibratoHz = a.a;
			v.vibratoFrom = a.at;
			v.vibratoFade = std::max<int64_t>(1, a.c);
			if (a.value <= 0.f)
				s.inst.stop(a.string, v.fingerHz);
			break;
		case Action::ATTACK:
			v.attacking = true;
			v.easing = false;
			v.accel = a.a;
			v.target = a.value;
			v.settle = a.b;
			v.attackFrom = a.at;
			v.easeSamples = a.c;
			v.slips.clear();
			v.wasStuck = true;
			v.force.to(a.d, std::max<int64_t>(1, (int64_t) (BOW_LANDS * rate)));
			v.speed.to(0.f, 0);
			break;
	}
}


void BowedPlayer::applyDue(State& s, double rate) {
	while (!s.actions.empty() && s.actions.front().at <= s.clock) {
		apply(s, s.actions.front(), rate);
		s.actions.erase(s.actions.begin());
	}
}


/** HOW QUIET IS SILENT, pascals a metre away: 118 dB under a violin's fff, 128 under a cello's. */
static const double QUIET = 1e-6;
/** HOW LONG QUIET, with no bow on any string, before the instrument rests, seconds. */
static const double SLEEP_AFTER = 1.0;
/** HOW LIGHT A BOW IS NO BOW, newtons. A bow ramped off a string in many small steps is left at the
rounding of their sum — 27 millionths of a newton was seen — and counted as bowing that kept the
instrument from ever resting, at its full cost, after the music stopped. The lightest real touch is
a twentieth of a newton. */
static const float NO_BOW = 1e-4f;


bool BowedPlayer::step(State& s, double rate, float* sound) {
	// AT REST: no bow, nothing to be heard, nothing to do. Engine samples go on coming, silent, every
	// `factor` of the strings' samples; it wakes at the engine sample before the next action is due, so
	// that waking never moves the engine samples against the strings'. A string's ringing a millionth of
	// a pascal down is held where it was, and the cooling its contact owes for the time is added on.
	// Copies rest and wake exactly as the instrument does, so a rehearsal is still what is played.
	const int factor = s.inst.oversampling();
	if (s.asleep) {
		if (s.phase == 0 && !s.actions.empty() && s.actions.front().at < s.clock + factor) {
			s.asleep = false;
			s.inst.idle(s.slept);
			s.slept = 0;
			s.quiet = 0;
		}
		else {
			s.clock++;
			s.slept++;
			if (++s.phase >= factor) {
				s.phase = 0;
				*sound = 0.f;
				return true;
			}
			return false;
		}
	}
	applyDue(s, rate);
	bool bowed = false;
	for (int k = 0; k < s.inst.strings(); k++) {
		Voice& v = s.voices[k];
		v.force.advance();
		v.distance.advance();
		if (v.attacking) {
			// THE BOW SPEEDING UP from standing still at the rehearsed rate, and the bite held until
			// four regular periods are heard — or half a second has gone without them.
			const double t = (double) (s.clock - v.attackFrom) / rate;
			const float top = std::fabs(v.target);
			const float sp = (float) std::fmin((double) top, (double) v.accel * t);
			v.speed.value = (v.target < 0.f) ? -sp : sp;
			if (!v.easing) {
				const bool stuck = stuckNow(s.inst.string(k), v.watch, v.force.value, v.speed.value);
				if (!stuck && v.wasStuck) {
					v.slips.push_back(s.clock);
					const double period = rate / (double) v.fingerHz;
					const size_t n = v.slips.size();
					bool regular = n >= 5;
					for (size_t q = (n >= 4 ? n - 4 : 0); regular && q < n; q++)
						regular = std::fabs((double) (v.slips[q] - v.slips[q - 1]) - period) <= 0.1 * period;
					if (regular || t > 0.5) {
						v.easing = true;
						v.force.to(v.settle, std::max<int64_t>(1, v.easeSamples));
					}
				}
				v.wasStuck = stuck;
			}
			if (v.easing && sp >= top)
				v.attacking = false;
		}
		else
			v.speed.advance();
		if (v.follow) {
			const float share = std::fmin(1.f, std::fabs(v.speed.value) / v.followSpeed);
			// The floor settles on over BOW_LANDS, as a bitten attack's force does.
			const float lands = (float) std::fmin(1.0, (double) (s.clock - v.followFrom) / (BOW_LANDS * rate));
			v.force.value = v.followForce * (v.followFloor * lands + (1.f - v.followFloor) * share);
		}
		// THE FINGER LANDING, the pitch moving to the new note's over the landing.
		if (v.glideLength > 0 && s.clock >= v.glideAt && (s.clock & 15) == 0) {
			double x = std::fmin(1.0, (double) (s.clock - v.glideAt) / (double) v.glideLength);
			if (v.glideSmooth)
				x = 0.5 - 0.5 * std::cos(M_PI * x);
			s.inst.stop(k, (float) ((double) v.glideFrom * std::pow((double) v.fingerHz / v.glideFrom, x)));
			if (x >= 1.0)
				v.glideLength = 0;
		}
		// VIBRATO, on a stopped string: the finger rocks, the pitch moving a sine either way, coming
		// in over its fade. The string is retuned every sixteenth sample, often enough that the
		// steps are far below a cent.
		if (v.vibratoCents > 0.f && v.stopped && v.glideLength == 0 && s.clock >= v.vibratoFrom
				&& (s.clock & 15) == 0) {
			const double t = (double) (s.clock - v.vibratoFrom);
			const double depth = (double) v.vibratoCents * s.vibratoScale * std::fmin(1.0, t / (double) v.vibratoFade);
			const double cents = depth * std::sin(2.0 * M_PI * (double) v.vibratoHz * t / rate);
			s.inst.stop(k, (float) ((double) v.fingerHz * std::pow(2.0, cents / 1200.0)));
		}
		s.inst.bow(k, std::fmax(0.f, v.force.value), v.speed.value, v.distance.value);
		bowed = bowed || v.force.value > NO_BOW;
		{
			const double f = s.inst.string(k).bridgeForce();
			v.ringing += (f * f - v.ringing) * (50.0 / rate);
		}
	}
	const bool ready = s.inst.step(sound);
	s.clock++;
	if (ready) {
		s.phase = 0;
		s.quiet = (!bowed && std::fabs(*sound) < QUIET) ? s.quiet + 1 : 0;
		if ((double) s.quiet * factor >= SLEEP_AFTER * rate) {
			s.asleep = true;
			s.slept = 0;
		}
	}
	else
		s.phase++;
	return ready;
}


float BowedPlayer::process() {
	float sound = 0.f;
	const int64_t prepare = (int64_t) (PREPARE * rate);
	// AT REST WITH NOTHING DUE before the next engine sample: the strings' samples the loop below would
	// pass through one at a time, asleep, passed over at once, to the same clock. A player in a section
	// with nothing to play costs this and no more.
	if (live.asleep && live.phase == 0 && !(mode != PLANNER && watchingOn)) {
		const int64_t next = live.clock + live.inst.oversampling();
		const bool actionDue = !live.actions.empty() && live.actions.front().at < next;
		const bool noteDue = !upcoming.empty() && upcoming.front().start - prepare < next;
		const bool releaseDue = anyLast && !lastReleased && releaseAt - prepare < next;
		if (!actionDue && !noteDue && !releaseDue) {
			live.slept += next - live.clock;
			live.clock = next;
			return 0.f;
		}
	}
	for (;;) {
		while (!upcoming.empty() && upcoming.front().start - prepare <= live.clock) {
			const Pending p = upcoming.front();
			upcoming.pop_front();
			decide(&p, 0);
		}
		// THE LAST NOTE ENDS WITH NOTHING JOINED TO IT: the bow slows and lifts.
		if (anyLast && !lastReleased && live.clock >= releaseAt - prepare) {
			const bool joined = !upcoming.empty()
				&& upcoming.front().start - releaseAt < (int64_t) (JOINED * rate);
			if (!joined)
				decide(nullptr, releaseAt);
		}
		const bool ready = step(live, rate, &sound);
		if (mode != PLANNER && watchingOn) {
			watchPlayed();
			hearPlayed();
		}
		if (ready)
			return sound;
	}
}


void BowedPlayer::share(int n, const std::function<void(int)>& f) const {
	if (crew)
		crew->each(n, f);
	else
		for (int i = 0; i < n; i++)
			f(i);
}


/** ONE START TRIED ON A COPY of the instrument: how soon it settled, and the rest attackSettles says. */
struct Trial {
	int got = -1;
	int64_t used = 0;
	double squeak = 0.0, late = 0.0, rough = 0.0, click = 0.0, rise = 0.0;
	std::vector<double> envelope;
};


void BowedPlayer::decide(const Pending* p, int64_t releaseWhen) {
	const int64_t serial = decisions++;
	const int64_t start = p ? std::max(p->start, live.clock) : releaseWhen;
	Decided d;
	d.serial = serial;
	d.start = start;
	d.before = takeLine();
	history.push_back(d);
	while (history.size() > 1 && history.front().start + (int64_t) (2.0 * rate) < live.clock)
		history.pop_front();
	owner = serial;
	// THE PLAYING COPY TAKES THE PLANNER'S DECISION where there is one for this moment and this note,
	// and decides for itself, quickly, where there is not.
	if (mode == PLAYING) {
		Plan* q = planning ? planning->planFor(generation, serial) : nullptr;
		if (q && q->release == (p == nullptr) && q->start == start && (!p || q->hz == p->note.hz)) {
			for (const Action& a : q->actions)
				schedule(live, a);
			giveLine(q->line);
			if (!q->release)
				played.push_back(q->record);
			if (q->learned)
				remember(q->learnedAt, q->value);
			planning->spent(q);
			return;
		}
		if (q)
			planning->spent(q);
		lost = true;
		quick = true;
		// COUNTED ONLY FOR A NOTE THAT WAS ANNOUNCED: see Note::unannounced.
		if (p && planning && !p->note.unannounced) {
			if (missed < MISSED_KEPT)
				missedAt[missed] = start;
			missed++;
		}
	}
	// FROM THE TABLE, ALL OF IT, where the player is told not to rehearse.
	if ((tableUse == EVERYTHING && table) || nothingRehearsed)
		quick = true;
	capturing = mode == PLANNER;
	captured.clear();
	learnedNow = false;
	if (p)
		plan(*p);
	else {
		planRelease(releaseWhen);
		lastReleased = true;
	}
	quick = false;
	if (capturing && planning) {
		Plan* q = new Plan;
		q->generation = generation;
		q->serial = serial;
		q->release = p == nullptr;
		q->start = start;
		q->hz = p ? p->note.hz : 0.f;
		q->actions = captured;
		q->line = takeLine();
		if (p && !played.empty())
			q->record = played.back();
		q->learned = learnedNow;
		q->learnedAt = learnedIndex;
		q->value = learnedValue;
		planning->decided(q);
		// What a planner played is the playing copy's to keep.
		played.clear();
		watching = 0;
	}
	capturing = false;
	// A LONG PART KEEPS ONLY ITS RECENT NOTES: the oldest records go once the one being watched is well
	// past them.
	if (mode == PLAYING && watching > 128) {
		played.erase(played.begin(), played.begin() + 64);
		watching -= 64;
		heardUpTo = heardUpTo > 64 ? heardUpTo - 64 : 0;
	}
}


void BowedPlayer::watchPlayed() {
	// The newest note that has begun is the one sounding.
	while (watching + 1 < played.size() && played[watching + 1].bodyFrom - (int64_t) (0.06 * rate) <= live.clock)
		watching++;
	if (watching >= played.size())
		return;
	Played& p = played[watching];
	const Voice& voice = live.voices[p.string];
	const bool stuck = stuckNow(live.inst.string(p.string), watchSlip, voice.force.value, voice.speed.value);
	// WHEN IT SPOKE: four gaps running a period within a tenth, from the bow's moving.
	if (p.spoke < 0.0 && live.clock >= p.movedAt && live.clock < p.bodyTo && watchWasStuck && !stuck) {
		const double period = rate / (double) p.hz;
		if (p.spokeLastSlip >= 0 && std::fabs((double) (live.clock - p.spokeLastSlip) - period) <= 0.1 * period)
			p.spokeRun++;
		else
			p.spokeRun = 0;
		p.spokeLastSlip = live.clock;
		if (p.spokeRun >= 4)
			p.spoke = (double) (live.clock - p.movedAt) / rate;
	}
	if (live.clock >= p.bodyFrom && live.clock < p.bodyTo && watchWasStuck && !stuck) {
		const double period = rate / (double) p.hz;
		if (p.lastSlip >= 0) {
			p.gaps++;
			if (std::fabs((double) (live.clock - p.lastSlip) - period) <= 0.1 * period)
				p.regularGaps++;
		}
		p.lastSlip = live.clock;
		p.slipCount++;
	}
	if (live.clock == p.bodyTo - 1 || (live.clock >= p.bodyFrom && live.clock < p.bodyTo)) {
		const double periods = (double) (std::min(live.clock + 1, p.bodyTo) - p.bodyFrom) * (double) p.hz / rate;
		p.slipsPerPeriod = periods > 0.0 ? (float) (p.slipCount / periods) : 0.f;
		p.regular = p.gaps > 0 ? (float) p.regularGaps / (float) p.gaps : 0.f;
	}
	watchWasStuck = stuck;
}


void BowedPlayer::planRelease(int64_t at) {
	const int64_t lead = (int64_t) (RELEASE * rate);
	Action a;
	a.at = std::max(live.clock, at - lead);
	a.string = lastString;
	a.samples = lead;
	a.kind = Action::SPEED;
	a.value = 0.f;
	schedule(live, a);
	a.kind = Action::FORCE;
	schedule(live, a);
}


// ---- the decisions -------------------------------------------------------------------------------

/** A HABIT AT A DYNAMIC: straight on a logarithmic scale against the ladder from `pianissimo` at pp to
`piano` at p, and from `piano` at p to `forte` at f, each line carried on beyond its ends. A negative
pianissimo carries the piano-to-forte line on below piano instead. */
static double byLevel(double level, double pianissimo, double piano, double forte) {
	if (level < 2.0 / 7.0 && pianissimo > 0.0)
		return pianissimo * std::pow(piano / pianissimo, (level - 1.0 / 7.0) / (1.0 / 7.0));
	return piano * std::pow(forte / piano, (level - 2.0 / 7.0) / (3.0 / 7.0));
}


/** HOW HARD, for a dynamic, on a string: a multiple of its impedance, by level between the piano and
forte measured and the pianissimo chosen by ear. */
float BowedPlayer::forceFor(float level, int string) const {
	double fz = byLevel(level, habits.forcePianissimo, habits.forcePiano, habits.forceForte);
	fz = std::fmax((double) habits.forceLeast, std::fmin((double) habits.forceMost, fz));
	return (float) (fz * live.inst.design().strings[(size_t) string].impedance);
}


float BowedPlayer::speedFor(float level) const {
	const double v = byLevel(level, habits.speedPianissimo, habits.speedPiano, habits.speedForte);
	return (float) std::fmax(0.08, std::fmin(0.6, v));
}


/** WHERE, for a dynamic: a fraction of the vibrating length, by level between the measured piano and
forte and the pianissimo chosen by ear. */
float BowedPlayer::betaFor(float level) const {
	const double b = byLevel(level, habits.betaPianissimo, habits.betaPiano, habits.betaForte);
	return (float) std::fmax((double) habits.betaLeast, std::fmin((double) habits.betaMost, b));
}


/** THE A-WEIGHTING, as a gain: the standard curve, nought decibels at 1 kHz. */
static double aWeighting(double f) {
	const double f2 = f * f;
	const double ra = (12194.0 * 12194.0 * f2 * f2)
		/ ((f2 + 20.6 * 20.6) * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
	return ra * 1.2589;
}


double BowedPlayer::expectedDb(float hz, int string) const {
	const BowedInstrument::Design& d = live.inst.design();
	double sum = 0.0;
	for (int k = 1; k <= 40 && k * hz < 8000.f; k++) {
		const double f = (double) k * hz;
		const double r = live.inst.radiativityAt((float) f) * aWeighting(f) / k;
		sum += r * r;
	}
	return 20.0 * std::log10((double) d.strings[(size_t) string].impedance) + 10.0 * std::log10(sum + 1e-30);
}


double BowedPlayer::evenFor(float hz, int string) const {
	if (habits.evenness <= 0.f)
		return 1.0;
	const double most = (double) habits.evenMostDb;
	const double db = std::fmax(-most, std::fmin(most, evenRef - expectedDb(hz, string))) * habits.evenness;
	return std::pow(10.0, db / 20.0);
}


/** WHETHER A NOTE WANTS VIBRATO, before the string is chosen: what the part says, and otherwise how
long the note is against the era's habit. Long enough, from the Romantic on, is nearly every note a
player can rock a finger on — the continuous vibrato of the recordings from the 1920s; in the
Classical a longer note, and in the Baroque only a long one, as an ornament
(research/bowed-strings/vibrato-sources.md). The lengths are judgement. */
bool BowedPlayer::vibratoWanted(const Note& n) const {
	if (n.vibrato == Note::SENZA)
		return false;
	if (n.vibrato == Note::ORDINARY || n.vibrato == Note::MOLTO)
		return true;
	const float shortest = habits.era == BAROQUE ? 0.8f : (habits.era == CLASSICAL ? 0.4f : 0.2f);
	return n.seconds >= shortest;
}


/** HOW WIDE, either way, in cents: none on an open string, where no finger can rock it; the
instrument's width, narrower in earlier music, wider the louder the note (Timmers and Desain's
violinist, extent rising with loudness), and half as wide again where the part says molto. */
float BowedPlayer::vibratoFor(const Note& n, int string) const {
	if (!vibratoWanted(n))
		return 0.f;
	const float open = live.inst.design().strings[(size_t) string].openHz;
	if (n.hz <= open * 1.0005f)
		return 0.f;
	const float era = habits.era == BAROQUE ? 0.6f : (habits.era == CLASSICAL ? 0.8f : 1.f);
	const float loud = 0.8f + 0.4f * std::fmax(0.f, std::fmin(1.f, n.level));
	return habits.vibratoCents * era * loud * (n.vibrato == Note::MOLTO ? 1.5f : 1.f);
}


/** WHICH STRING: the one that puts the note lowest on the fingerboard — except at an open string's
pitch, which is stopped on the string below instead where the note follows on from that string, so
that a phrase stays on one string rather than crossing for a single note, or where it is long and
wants vibrato, as a player takes it with the fourth finger so that it can vibrate like its
neighbours. Long is 0.6 s or more, a judgement: a quicker note there, crossing from another string,
is played open. */
int BowedPlayer::stringFor(float hz, bool longWithVibrato, int followsFrom) const {
	const int s = live.inst.stringFor(hz);
	const float open = live.inst.design().strings[(size_t) s].openHz;
	if (s > 0 && std::fabs(hz / open - 1.f) < 0.003f && (longWithVibrato || followsFrom == s - 1))
		return s - 1;
	return s;
}


void BowedPlayer::plan(const Pending& p) {
	const Note& n = p.note;
	const BowedInstrument::Design& design = live.inst.design();
	const int64_t t0 = std::max(p.start, live.clock);
	const double len = n.staccato ? std::fmin(0.5 * n.seconds, 0.25) : (double) n.seconds;
	const int64_t end = t0 + (int64_t) std::llround(len * rate);
	Played record;
	record.at = (double) t0 / rate;
	record.hz = n.hz;
	record.level = n.level;

	// WHAT THIS NOTE FOLLOWS FROM: the last one, if the bow is still on it and it ends where this
	// begins.
	const bool joined = anyLast && !lastReleased && (last.held || t0 - lastEnd < (int64_t) (JOINED * rate));
	bool slurIn = joined && (last.slurred || n.fromSlur);
	bool hidden = false;
	// A BOW CHANGE HIDDEN IN A SLUR: where the slur still has longer to run than the bow left this way
	// lasts at the habitual speed, the bow changes at this note, as a player changes bow unnoticed in
	// a long slur rather than letting it crawl — drawn out to a few centimetres a second, the notes came
	// out twenty decibels down and were heard dropping out.
	if (slurIn) {
		const double remaining = (n.slurred && n.slurSeconds > 0.f) ? (double) n.slurSeconds : len;
		const double left = (direction > 0) ? habits.bowLength - bowAt : bowAt;
		if (remaining > 0.25 && speedFor(n.level) * remaining > 0.95 * left) {
			slurIn = false;
			hidden = true;
		}
	}
	// THE BOW TAKEN AFRESH, as a player beginning the piece again sets it: in the middle, the last
	// stroke counted as up, so that a stroke from the string goes as it would on a first playing.
	// Asked for when the music goes back to its start (retakeBow), and done at the first note from the
	// string after it, since a note still sounding keeps its bow. Without it a second playing carried
	// on from where the first had left the bow, every stroke the other way from the first time.
	if (retake && !joined) {
		bowAt = 0.5 * habits.bowLength;
		direction = -1;
		strokeShare = 1.0;
		retake = false;
	}
	const int s = stringFor(n.hz, vibratoWanted(n) && n.seconds >= 0.6f, joined ? lastString : -1);
	const float open = design.strings[(size_t) s].openHz;
	const float vibrating = design.length * open / n.hz;
	// THE DYNAMIC'S SPEED, AND THIS NOTE'S: the latter evened out against the instrument (see evenFor).
	// A new stroke is drawn at it; a note under a slur keeps the stroke's speed and comes nearer the
	// bridge or further off instead, since the place follows the speed against it (see `sounding`).
	const double base = speedFor(n.level);
	const double even = evenFor(n.hz, s);
	const double habitual = base * std::fmax((double) habits.evenSlowest, even);
	// What the speed could not take off a strong note, the place does: further from the bridge.
	const double further = std::fmax(1.0, (double) habits.evenSlowest / even);

	// WHICH WAY THE BOW GOES. On under a slur; otherwise as marked, else down on the first beat of a
	// bar from the string, else the other way from the last stroke.
	int dir;
	if (slurIn)
		dir = direction;
	else if (n.bow != 0)
		dir = n.bow;
	else if (!joined && n.downbeat)
		dir = 1;
	else
		dir = -direction;

	Played::Kind kind;
	if (slurIn)
		kind = (s == lastString) ? Played::LEGATO : Played::CROSS;
	else if (joined && dir != direction)
		kind = (s == lastString) ? Played::CHANGE : Played::CROSS;
	else {
		// Not joined — or the same way twice running, which is the bow lifted and taken back.
		if (joined) {
			planRelease(t0);
			lastReleased = true;
		}
		kind = Played::FRESH;
	}

	// HOW FAST: a new stroke shares out the bow it has against how long it has to last, aiming to end
	// the stroke as far past the middle of the bow as it began before it, so that strokes alternating
	// keep the bow in the middle. Under a slur each note is drawn at its own habit, as much faster or
	// slower than it as the stroke began, and slower only if the bow would run out.
	//
	// ITS OWN HABIT, not the stroke's speed held: a slur begun on a note the instrument gives weakly is
	// drawn fast for it, and the notes after it, held to that speed, could not move far enough from the
	// bridge to take it back off — in the Fauré's quiet bars the cello's G3 marked between pianissimo and
	// piano came out 9 dB over the G3 a bar before marked louder. And so a hairpin under a slur is drawn
	// with the bow, as a player draws it.
	const double L = habits.bowLength;
	const double strokeSeconds = (n.slurred && n.slurSeconds > 0.f) ? (double) n.slurSeconds
		: (n.slurred ? 2.0 * len : len);
	double speed;
	if (slurIn) {
		speed = std::fmax(0.05, std::fmin(0.7, habitual * strokeShare));
		const double left = (dir > 0) ? L - bowAt : bowAt;
		if (speed * strokeSeconds > 0.95 * left)
			speed = std::fmax(0.05, 0.95 * left / strokeSeconds);
	}
	else {
		double left = (dir > 0) ? L - bowAt : bowAt;
		if (left < 0.05) {
			// NO BOW LEFT THAT WAY: it is lifted and put back down near the other end.
			bowAt = (dir > 0) ? 0.1 * L : 0.9 * L;
			left = (dir > 0) ? L - bowAt : bowAt;
		}
		// The bow's habitual speed, a little more of it where the stroke goes towards the middle of
		// the bow and a little less where it goes away, so that alternating strokes drift back to the
		// middle rather than off one end.
		const double want = habitual * strokeSeconds;
		const double towards = (0.5 * L - bowAt) * dir / (0.5 * L);
		double travel = std::fmax(0.5 * want, std::fmin(std::fmin(1.5 * want, 0.95 * left),
			want * (1.0 + 0.2 * towards)));
		// LOOKING ONE STROKE AHEAD, as a player saves bow for what is coming: where the stroke after
		// this one, the other way, will want more bow than this one would leave it, this one draws
		// more — up to half as fast again as its habit, further from the bridge for it (see
		// `sounding`), and so as loud. In the Fauré's third movement the cello's down-bow at 20 s,
		// begun at the frog, drew 19 cm, and the up-bow after it ran out on its first note: the A flat
		// 4 slurred after that got a third of its speed and force and came out 7 dB under the note
		// before it, in a crescendo. Only notes starting within LOOK_AHEAD of this one: a planner
		// running ahead of the music sees no further for certain.
		//
		// THE STROKE FOLLOWED NOTE BY NOTE, as the player joins notes into one: on while each runs
		// into the next without a gap and one of the two says slurred. Not by the length the score
		// gives the slur, which a swung score once gave as written while it played the notes swung,
		// and the next stroke's first note was taken for one of this one's.
		{
			const int64_t joinedGap = (int64_t) (JOINED * rate);
			const int64_t sees = t0 + (int64_t) (LOOK_AHEAD * rate);
			bool slurring = n.slurred;
			int64_t endsAt = end;
			for (const Pending& q : upcoming) {
				if (q.start > sees)
					break;
				const Note& m = q.note;
				const double mLen = m.staccato ? std::fmin(0.5 * m.seconds, 0.25) : (double) m.seconds;
				// A rest before it: the bow is lifted and placed again.
				if (q.start - endsAt >= joinedGap)
					break;
				// Still in this stroke.
				if (slurring || m.fromSlur) {
					slurring = m.slurred;
					endsAt = q.start + (int64_t) std::llround(mLen * rate);
					continue;
				}
				// Marked to go the same way: the bow is lifted and taken back.
				if (m.bow == dir)
					break;
				const double mStroke = (m.slurred && m.slurSeconds > 0.f) ? (double) m.slurSeconds
					: (m.slurred ? 2.0 * mLen : mLen);
				const double mHabit = speedFor(m.level)
					* std::fmax((double) habits.evenSlowest, evenFor(m.hz, stringFor(m.hz, false, -1)));
				const double need = std::fmin(0.95 * L, mHabit * mStroke);
				const double behind = (dir > 0) ? bowAt : L - bowAt;
				if (need > behind + travel)
					travel = std::fmax(travel, std::fmin(need - behind,
						std::fmin(0.95 * left, (double) habits.lookAheadMost * habitual * strokeSeconds)));
				break;
			}
		}
		// NOT SO SLOW THAT THE NOTE FADES: a long slur shared out over the whole bow would crawl, and
		// a player keeps the bow moving and changes it inside the slur instead (see above).
		speed = std::fmax(0.85 * habitual, std::fmax(0.05, std::fmin(0.7, travel / strokeSeconds)));
		strokeShare = speed / habitual;
	}
	if (std::getenv("BOWTRACE") && mode != PLAYING)
		std::fprintf(stderr, "BOW %.3f s %.1f Hz kind %d dir %d gap %.3f slurred %d len %.3f stroke %.3f speed %.3f habitual %.3f bowAt %.3f\n",
			(double) t0 / rate, n.hz, (int) kind, dir, anyLast ? (double) (t0 - lastEnd) / rate : -1.0, (int) n.slurred, len, strokeSeconds, speed, habitual, bowAt);
	bowAt = std::fmax(0.0, std::fmin(L, bowAt + dir * speed * len));
	const float signedSpeed = (float) (dir * speed);

	// A SLOWER BOW, A LIGHTER ONE: the force a string takes before the bow squeaks falls with the bow's
	// speed (Schelleng's upper limit goes as the speed), so a bow drawn slower than the habit to make
	// it last a long slur presses less in proportion. A floor held at four tenths left the bow at a
	// third of its speed pressing near that limit, and it rang high (heard at 8 s into the Fauré).
	const float slower = (float) std::fmax(0.1, std::fmin(2.0, speed / base));
	float force = forceFor(n.level, s) * (n.tenuto ? 1.1f : 1.f) * slower;
	// THE LOUDNESS IS THE BOW'S SPEED OVER ITS DISTANCE FROM THE BRIDGE, so a bow drawn faster or slower
	// than the habit for the dynamic moves away from the bridge or towards it in proportion, as a player
	// keeps the tone even across bow speeds. Only where the dynamic is in the speed.
	const double sounding = std::fmax((double) habits.betaLeast,
		std::fmin((double) habits.betaMost, betaFor(n.level) * speed / habitual * further));
	const float distance = (float) sounding * vibrating;
	const float cents = vibratoFor(n, s);
	const float vibHz = habits.vibratoHz * (1.f + 0.03f * noise());
	const int64_t delay = (int64_t) (habits.vibratoDelay * rate);
	const int64_t fade = (int64_t) (habits.vibratoFade * rate);

	auto act = [&](State& st, int64_t at, Action::Kind k, int string, float value, int64_t samples) {
		Action a;
		a.at = at;
		a.kind = k;
		a.string = string;
		a.value = value;
		a.samples = samples;
		schedule(st, a);
	};
	// THE FINGER TO A NEW NOTE: slid over `samples`, or, physically, at once (see setPhysicalFinger).
	auto vibrato = [&](State& st, int64_t from) {
		Action a;
		a.at = from + delay;
		a.kind = Action::VIBRATO;
		a.string = s;
		a.value = cents;
		a.a = vibHz;
		a.c = fade;
		schedule(st, a);
	};

	if (kind == Played::LEGATO) {
		// THE FINGER MOVES AND THE BOW GOES ON: the new force and place over 30 ms.
		const int64_t ramp = (int64_t) (0.03 * rate);
		act(live, t0, Action::VIBRATO, s, 0.f, 0);
		// A LARGE INTERVAL SMOOTHED: the finger slides over `slide`, and the old note lingers (see
		// SlurSmoothing).
		const double interval = anyLast ? std::fabs(12.0 * std::log2((double) n.hz / (double) last.hz)) : 0.0;
		if (slurSmoothing.on && interval >= (double) slurSmoothing.from - 0.01) {
			Action a;
			a.at = t0;
			a.kind = Action::FINGER;
			a.string = s;
			a.value = n.hz;
			a.samples = std::max<int64_t>(1, (int64_t) ((double) slurSmoothing.slide * rate));
			a.b = slurSmoothing.linger;
			a.c = 2;
			schedule(live, a);
		}
		else
			act(live, t0, Action::FINGER, s, n.hz, (int64_t) (LANDING * rate));
		act(live, t0, Action::FORCE, s, force, ramp);
		act(live, t0, Action::DISTANCE, s, distance, ramp);
		act(live, t0, Action::SPEED, s, signedSpeed, ramp);
		vibrato(live, t0);
	}
	else if (kind == Played::CHANGE) {
		// A BOW CHANGE, REHEARSED: the habit's three ways tried on a copy of the instrument as it
		// stands, the bow slowing over 10, 20 or 40 ms and speeding up again as long (bow-changes.md).
		// Or from the table, where it has the situation and is used for bow changes.
		const bool quickWas = quick;
		if (tableUse == CHANGES && table && table->find(BowedTable::CHANGE, s, semitoneOf(n.hz, s),
				levelStepOf(n.level), BowedTable::NOT_ASKED))
			quick = true;
		double best = 1e9;
		float bestDown = 0.02f, bestTurn = 0.f;
		auto change = [&](State& st, float down, float floor) {
			// THE BOW SLOWS TO THE TURN AND SPEEDS UP THE OTHER WAY, and the force follows its speed,
			// down to `floor` of itself at the turn: the force a string takes before it squeaks falls
			// with the bow's speed, and a bow slowed under its full weight drives the string into a
			// higher mode — the harmonic-like squeak heard before every change made that way.
			const int64_t d = (int64_t) (down * rate);
			const int64_t from = std::max(st.clock, t0 - d);
			const int64_t restart = t0;
			Action f;
			f.kind = Action::FOLLOW;
			f.string = s;
			f.at = from;
			f.value = lastForce;
			f.a = floor;
			f.b = std::fabs(lastSpeed);
			schedule(st, f);
			act(st, from, Action::SPEED, s, 0.f, std::max<int64_t>(1, t0 - from));
			act(st, t0, Action::VIBRATO, s, 0.f, 0);
			act(st, t0, Action::FINGER, s, n.hz, (int64_t) (LANDING * rate));
			act(st, t0, Action::DISTANCE, s, distance, 0);
			f.at = t0;
			f.value = force;
			f.b = (float) speed;
			schedule(st, f);
			act(st, restart, Action::SPEED, s, signedSpeed, d);
			act(st, restart + d, Action::FORCE, s, force, 0);
			return restart;
		};
		// WHERE NONE IS BACK WITHIN 90 MS, THE PLAYER LEANS IN: the same again with the new note pressed
		// half as hard again, then twice as hard, as a player presses a note that will not speak. The
		// note keeps the force that worked; failing all, the soonest is played.
		const float asked = force;
		float bestPress = 1.f;
		double bestSqueak = 1e18;
		bool bestClean = false;
		// For the log: every way of changing tried, and how it came out.
		struct Tried {
			float press, down;
			double got, squeak;
		};
		std::vector<Tried> changesTried;
		for (float press : {1.f, 1.5f, 2.f}) {
			// NOT REHEARSED WHEN THERE IS NO TIME: see below.
			if (quick || (press > 1.f && bestClean))
				break;
			force = asked * press;
			// A CHANGE HIDDEN IN A SLUR IS QUICK: a slow turn there is a gap in the line.
			// Each turn let down to nothing, and with the habit's share of the force kept where it has one.
			std::vector<float> downs, floors;
			for (float down : habits.changeSeconds) {
				if (hidden && down > 0.021f)
					continue;
				downs.push_back(down);
				floors.push_back(0.f);
				if (habits.changeFloor > 0.f) {
					downs.push_back(down);
					floors.push_back(habits.changeFloor);
				}
			}
			const bool clicks = habits.changeClickWeight > 0.f;
			std::vector<double> gots(downs.size()), squeaks(downs.size()), clickOfWay(downs.size(), 0.0);
			std::vector<int64_t> lengths(downs.size());
			const auto changesFrom = std::chrono::steady_clock::now();
			share((int) downs.size(), [&](int i) {
				State copy = live;
				const int64_t restart = change(copy, downs[(size_t) i], floors[(size_t) i]);
				vibrato(copy, restart);
				const int64_t until = restart + (int64_t) (std::fmax(60.0 / n.hz, 0.08) * rate);
				lengths[(size_t) i] = until - copy.clock;
				gots[(size_t) i] = settles(copy, s, n.hz, restart, lengths[(size_t) i], nullptr, &squeaks[(size_t) i],
					cents, clicks ? &clickOfWay[(size_t) i] : nullptr);
			});
			spentOn.changes += std::chrono::duration<double>(std::chrono::steady_clock::now() - changesFrom).count();
			for (size_t i = 0; i < downs.size(); i++) {
				const float down = downs[i];
				record.rehearsed += lengths[i];
				// ITS CLICK COUNTED BESIDE ITS SQUEAK, where the habit says.
				const double squeak = squeaks[i] + (double) habits.changeClickWeight * clickOfWay[i];
				const double got = gots[i];
				if (logFile)
					changesTried.push_back({press, down, got, squeak});
				const bool clean = got < 999.0 && got / n.hz <= 0.09;
				// OF THE CLEAN, THE ONE THAT SQUEAKS LEAST through the whole change, slowing and
				// speeding up again (see squeakOf).
				const bool better = clean ? (!bestClean || squeak < bestSqueak) : (!bestClean && got < best);
				if (better) {
					best = got;
					bestSqueak = squeak;
					bestClean = clean;
					bestDown = down;
					bestPress = press;
					bestTurn = floors[i];
				}
			}
		}
		// WITHOUT REHEARSING, the way rehearsal found for the nearest note like it on this string; and
		// rehearsed, kept for one that has to be played without.
		if (quick) {
			// FROM THE TABLE, where it has this situation; else as rehearsal found for a note like it.
			const BowedTable::Start* t = table ? table->find(BowedTable::CHANGE, s, semitoneOf(n.hz, s),
				levelStepOf(n.level), BowedTable::NOT_ASKED) : nullptr;
			const Remembered* r = (t && t->kind == 'c') ? nullptr : recall(memoryIndexFor(n.hz, n.level, s, 1));
			if (t && t->kind == 'c') {
				bestPress = t->a;
				bestDown = t->b;
				record.fromTable = 'c';
			}
			else if (r) {
				bestDown = r->down;
				bestPress = r->press;
			}
			else {
				bestDown = habits.guessTurn;
				bestPress = 1.f;
			}
			if (hidden && bestDown > 0.021f)
				bestDown = habits.changeSeconds[0];
		}
		else {
			learnedNow = true;
			learnedIndex = memoryIndexFor(n.hz, n.level, s, 1);
			learnedValue = Remembered();
			learnedValue.down = bestDown;
			learnedValue.press = bestPress;
			remember(learnedIndex, learnedValue);
			if (logFile) {
				std::fprintf(logFile, "{\"instrument\":\"%s\",\"kind\":\"change\",\"string\":%d,\"hz\":%.2f,\"semitone\":%d,"
					"\"level\":%.3f,\"speed\":%.3f,\"force\":%.3f,\"beta\":%.4f,\"direction\":%d,\"hidden\":%d,"
					"\"previousHz\":%.2f,\"previousString\":%d,\"tried\":[",
					logName.c_str(), s, n.hz, (int) std::lround(12.0 * std::log2((double) n.hz / design.strings[(size_t) s].openHz)),
					n.level, speed, asked, distance / vibrating, dir, (int) hidden, last.hz, lastString);
				for (size_t i = 0; i < changesTried.size(); i++)
					std::fprintf(logFile, "%s{\"press\":%.2f,\"down\":%.3f,\"periods\":%.1f,\"squeak\":%.2f}", i ? "," : "",
						changesTried[i].press, changesTried[i].down, changesTried[i].got, changesTried[i].squeak);
				std::fprintf(logFile, "],\"chose\":{\"press\":%.2f,\"down\":%.3f}}\n", bestPress, bestDown);
			}
		}
		force = asked * bestPress;
		const int64_t restart = change(live, bestDown, bestTurn);
		vibrato(live, restart);
		record.changeSeconds = bestDown;
		record.changeForce = bestTurn;
		record.settled = best >= 999.0 ? -1.0 : best;
		quick = quickWas;
	}
	else {
		// FROM THE STRING, OR ONTO A NEW ONE: the bow on the old string, if it is there, lifted as it
		// goes; the attack rehearsed on this string as it will be when the note starts, and confirmed on
		// the instrument as a whole (phase 6), the first of its shortlist to start within three
		// periods played. Or the guess, where starts are not rehearsed.
		const bool quickBefore = quick;
		if (guessStarts)
			quick = true;
		if (kind == Played::CROSS && anyLast) {
			const int64_t lift = (int64_t) (0.02 * rate);
			act(live, std::max(live.clock, t0 - lift / 2), Action::FORCE, lastString, 0.f, lift);
			act(live, std::max(live.clock, t0 - lift / 2), Action::SPEED, lastString, 0.f, lift);
		}
		act(live, t0, Action::VIBRATO, s, 0.f, 0);
		// THE FINGER DOWN BEFORE THE BOW, over LANDING, as a player stops the string in the gap. Put
		// down in the sample the bow lands, the pitch of a string still ringing from the note before
		// jumped, and the cello's D3 after C3 on the same string began with a pluck.
		{
			const int64_t landing = std::max<int64_t>(1, (int64_t) (LANDING * rate));
			act(live, std::max(live.clock, t0 - 2 * landing), Action::FINGER, s, n.hz, landing);
		}
		act(live, t0, Action::DISTANCE, s, distance, 0);
		// The string as it will be at t0: a copy run up to then. Not when there is no time to rehearse.
		const float beta = distance / vibrating;
		State at;
		std::vector<Rehearsed> list;
		// For the log: how strongly the string is ringing in the last 20 ms before the note.
		double ringing = 0.0;
		int64_t ringingFor = 0;
		if (!quick) {
			auto clockFrom = std::chrono::steady_clock::now();
			at = live;
			float sound;
			const int64_t ringFrom = t0 - (int64_t) (0.02 * rate);
			while (at.clock < t0) {
				step(at, rate, &sound);
				if (logFile && at.clock >= ringFrom) {
					const double f = at.inst.string(s).bridgeForce();
					ringing += f * f;
					ringingFor++;
				}
			}
			applyDue(at, rate);
			record.rehearsed += t0 - live.clock;
			spentOn.runUp += std::chrono::duration<double>(std::chrono::steady_clock::now() - clockFrom).count();
			clockFrom = std::chrono::steady_clock::now();
			spentOn.starts++;
			if (!few.any()) {
				list = rehearseShortlist(at.inst.string(s), n.hz, beta, force, (float) speed, crew);
				spentOn.widened += (!list.empty() && list.front().soonest > 3) ? 1 : 0;
				spentOn.shortlist += std::chrono::duration<double>(std::chrono::steady_clock::now() - clockFrom).count();
			}
			else {
				// A FEW, FROM SOFT TO FIRM: no shortlist.
				const double zBow = (double) design.strings[(size_t) s].impedance * 3.27 / 4.27;
				for (size_t k = 0; k < few.bites.size() && k < few.accels.size(); k++) {
					Rehearsed r;
					r.beta = beta * (k < few.shares.size() ? few.shares[k] : 1.f);
					r.bite = few.bites[k];
					r.accel = (float) (std::exp((double) few.accels[k] / 12.0) * (double) n.hz * (double) r.beta
						* (1.0 - (double) r.beta) * (double) force * r.bite / zBow);
					list.push_back(r);
				}
			}
		}
		Rehearsed chose;
		int bestPeriods = 999;
		const float accent = n.accent ? 1.3f : 1.f;
		auto attack = [&](State& st, const Rehearsed& c) {
			Action a;
			a.at = t0;
			a.kind = Action::ATTACK;
			a.string = s;
			a.value = signedSpeed;
			a.a = c.accel;
			a.b = force;
			a.d = (float) (force * c.bite) * accent;
			a.c = (int64_t) ((double) habits.biteEase * rate);
			schedule(st, a);
			act(st, t0, Action::DISTANCE, s, c.beta * vibrating, 0);
			// AND BACK TO WHERE THE NOTE WANTS IT once the start is made: an attack found at 0.7 to
			// 1.4 of the planned place, kept for the whole note, left it up to 3 dB quieter or louder
			// than its neighbours.
			if (std::fabs(c.beta * vibrating - distance) > 1e-4f)
				act(st, t0 + (int64_t) (RETURN_AFTER * rate), Action::DISTANCE, s, distance,
					(int64_t) (RETURN_OVER * rate));
		};
		// OF THE FIRST TWELVE, THOSE THAT START WITHIN THREE PERIODS ON THE INSTRUMENT, THE ONE THAT
		// PLAYS CLEANLY AFTER (see roughnessOf), and of those the one that SQUEAKS LEAST in its first
		// 150 ms (see squeakOf): a bite pressed hard on a bow barely moving makes the short length by the
		// bridge ring. Where none plays cleanly, the least rough. Failing all, the one at full level
		// soonest.
		//
		// CLEANLY AFTER, because how soon it starts and how little it squeaks say nothing about the rest
		// of the note: a bite eases off over `biteEase`, and the attacks chosen on those two alone left
		// notes from the string at piano rough for most of a second (research/bowed-strings/
		// bow-changes.md).
		// AND THEN WITHOUT A CLICK: a start brighter than the note it becomes is heard as a pluck.
		// AND THAT REACHES ITS LEVEL PROMPTLY: one that speaks on time and swells in is heard as weak.
		double leastSqueak = 1e9, leastRough = 1e9, leastClick = 1e9, bestRise = -1e9;
		bool anyQuick = false, anyClean = false, anyPrompt = false, anyQuiet = false;
		auto better = [&](double squeak, double rough, double click, double rise) {
			const bool clean = rough <= ROUGH_CLEAN, prompt = rise >= RISE_PROMPT, quiet = click <= CLICK_CLEAN;
			if (!anyQuick)
				return true;
			if (clean != anyClean)
				return clean;
			if (!clean)
				return rough < leastRough;
			if (prompt != anyPrompt)
				return prompt;
			if (!prompt)
				return rise > bestRise;
			if (quiet != anyQuiet)
				return quiet;
			return quiet ? squeak < leastSqueak : click < leastClick;
		};
		auto take = [&](double squeak, double rough, double click, double rise) {
			anyQuick = true;
			anyClean = rough <= ROUGH_CLEAN;
			anyPrompt = anyClean && rise >= RISE_PROMPT;
			anyQuiet = anyPrompt && click <= CLICK_CLEAN;
			leastSqueak = squeak;
			leastRough = rough;
			leastClick = click;
			bestRise = rise;
		};
		// FAILING ALL, kept to choose from after: see below.
		struct Fallback {
			bool glide;
			size_t index;
			float ramp, floor;
			int got;
			double rough;
			std::vector<double> envelope;
			double squeak;
		};
		std::vector<Fallback> fallbacks;
		// EACH TRIED ON A COPY, shared out over the crew where there is one; then compared in order.
		const size_t bites = std::min<size_t>(list.size(), 12);
		std::vector<Trial> tried(bites);
		const auto bitesFrom = std::chrono::steady_clock::now();
		share((int) bites, [&](int i) {
			State copy = at;
			attack(copy, list[(size_t) i]);
			Trial& t = tried[(size_t) i];
			t.got = attackSettles(copy, s, n.hz, t0, &t.used, &t.squeak, cents, &t.late, &t.rough, &t.click,
				&t.rise, &t.envelope);
		});
		if (bites)
			spentOn.bites += std::chrono::duration<double>(std::chrono::steady_clock::now() - bitesFrom).count();
		for (size_t i = 0; i < bites; i++) {
			const Trial& t = tried[i];
			record.rehearsed += t.used;
			const bool speaks = t.got >= 0 && t.got <= 3 && t.late <= SPEAKS;
			if (speaks && better(t.squeak, t.rough, t.click, t.rise)) {
				take(t.squeak, t.rough, t.click, t.rise);
				bestPeriods = t.got;
				chose = list[i];
			}
			else if (!anyQuick && t.got >= 0)
				fallbacks.push_back({false, i, 0.f, 0.f, t.got, t.rough, t.envelope, t.squeak});
		}
		// OR NO BITE AT ALL: the bow drawn up to speed over 20, 50 or 100 ms with the force rising with
		// it, from nothing or a fifth, as the bow comes out of the turn of a bow change. On the cello's
		// heavy strings under the thermal friction every bite squeaked; this did not.
		auto glide = [&](State& st, float ramp, float floor) {
			act(st, t0, Action::SPEED, s, 0.f, 0);
			act(st, t0, Action::SPEED, s, signedSpeed, (int64_t) (ramp * rate));
			Action f;
			f.kind = Action::FOLLOW;
			f.string = s;
			f.at = t0;
			f.value = force * accent;
			f.a = floor;
			f.b = (float) speed;
			schedule(st, f);
			act(st, t0 + (int64_t) (ramp * rate), Action::FORCE, s, force, (int64_t) (0.05 * rate));
		};
		float glideRamp = 0.f, glideFloor = 0.f;
		std::vector<Trial> glidesTried;
		static const float RAMPS[3] = {0.02f, 0.05f, 0.1f}, FLOORS[2] = {0.f, 0.2f};
		{
			// THE SIX, or a few's own.
			std::vector<float> ramps, floors;
			if (!quick && !few.any())
				for (int i = 0; i < 6; i++) {
					ramps.push_back(RAMPS[i / 2]);
					floors.push_back(FLOORS[i % 2]);
				}
			else if (!quick) {
				ramps = few.glideRamps;
				floors = few.glideFloors;
				floors.resize(ramps.size(), 0.f);
			}
			const int glides = (int) ramps.size();
			std::vector<Trial> slid((size_t) glides);
			const auto glidesFrom = std::chrono::steady_clock::now();
			share(glides, [&](int i) {
				State copy = at;
				glide(copy, ramps[(size_t) i], floors[(size_t) i]);
				Trial& t = slid[(size_t) i];
				t.got = attackSettles(copy, s, n.hz, t0, &t.used, &t.squeak, cents, &t.late, &t.rough, &t.click,
					&t.rise, &t.envelope);
			});
			if (glides)
				spentOn.glides += std::chrono::duration<double>(std::chrono::steady_clock::now() - glidesFrom).count();
			for (int i = 0; i < glides; i++) {
				const Trial& t = slid[(size_t) i];
				const float ramp = ramps[(size_t) i], floor = floors[(size_t) i];
				record.rehearsed += t.used;
				const bool speaks = t.got >= 0 && t.got <= 3 && t.late <= SPEAKS;
				if (speaks && better(t.squeak, t.rough, t.click, t.rise)) {
					take(t.squeak, t.rough, t.click, t.rise);
					bestPeriods = t.got;
					glideRamp = ramp;
					glideFloor = floor;
				}
				else if (!anyQuick && t.got >= 0)
					fallbacks.push_back({true, 0, ramp, floor, t.got, t.rough, t.envelope, t.squeak});
			}
			if (logFile)
				glidesTried = slid;
		}
		// FAILING ALL, OF THOSE AT ONE SLIP A PERIOD WITHIN TEN PERIODS, THE ONE WITHIN 3 dB OF THE NOTE'S
		// FULL LEVEL SOONEST, and of two within 10 ms of each other the less rough. The note's full level
		// is the most any of them has from 200 ms to ROUGH_UNTIL, when all are back at the planned speed
		// and place: an attack judged against its own later level counts one that stays weak throughout
		// as at full level at once. Only those that settle, since a string slipping twice a period is
		// loud from the start: the violin's C4 at pianissimo was given one, and played it so. And not the
		// one settled in the fewest periods from its first slip: the cello's F#3 at piano, alone, was given
		// a glide that slid without catching for 180 ms and then swelled in over 0.3 s; after F3, a bite
		// so gentle that the note stayed 10 dB under its neighbours.
		if (!anyQuick && !fallbacks.empty()) {
			int settleLimit = SETTLES;
			// QUIET BEFORE QUICK, where the habit asks: only the starts that squeak no more than it and
			// settle within twice SETTLES, where there are any (see Habits::startSqueakMost).
			if (habits.startSqueakMost < 0.f) {
				std::vector<Fallback> quiet;
				for (const Fallback& f : fallbacks)
					if (f.squeak <= (double) habits.startSqueakMost && f.got >= 0 && f.got <= 2 * SETTLES)
						quiet.push_back(f);
				if (!quiet.empty()) {
					fallbacks = quiet;
					settleLimit = 2 * SETTLES;
				}
			}
			bool anySettle = false;
			for (const Fallback& f : fallbacks)
				anySettle = anySettle || f.got <= settleLimit;
			const size_t settled = (size_t) (0.2 / ENVELOPE_HOP);
			double most = 0.0;
			for (const Fallback& f : fallbacks) {
				if (anySettle && f.got > settleLimit)
					continue;
				double sum = 0.0;
				for (size_t k = settled; k < f.envelope.size(); k++)
					sum += f.envelope[k];
				if (f.envelope.size() > settled)
					most = std::max(most, sum / (double) (f.envelope.size() - settled));
			}
			const Fallback* pick = nullptr;
			double soonest = 1e9, pickRough = 1e9;
			for (const Fallback& f : fallbacks) {
				if (anySettle && f.got > settleLimit)
					continue;
				double full = 9.0;
				for (size_t k = 0; k < f.envelope.size(); k++)
					if (f.envelope[k] >= 0.5 * most) {
						full = (double) k * ENVELOPE_HOP + 0.01;
						break;
					}
				if (!pick || full < soonest - 0.01 || (full < soonest + 0.01 && f.rough < pickRough)) {
					pick = &f;
					soonest = std::min(soonest, full);
					pickRough = f.rough;
				}
			}
			bestPeriods = pick->got;
			glideRamp = pick->glide ? pick->ramp : 0.f;
			glideFloor = pick->glide ? pick->floor : 0.f;
			if (!pick->glide)
				chose = list[pick->index];
		}
		// WITHOUT REHEARSING, the start rehearsal found for the nearest note like it on this string,
		// glided or bitten; for none yet, a glide over 20 ms from a fifth of the force.
		bool recalled = false;
		const BowedTable::Start* fromTable = (quick && table && !guessOnly) ? table->find(kind == Played::CROSS
			? BowedTable::CROSS : BowedTable::FRESH, s, semitoneOf(n.hz, s), levelStepOf(n.level),
			BowedTable::ringingOf(live.voices[s].ringing)) : nullptr;
		if (fromTable && fromTable->kind == 'b') {
			// A BITE FROM THE TABLE: its place a share of the planned one, its force over the note's,
			// and its acceleration a share of what Guettler's limits allow (see rehearseShortlist).
			const double zBow = (double) design.strings[(size_t) s].impedance * 3.27 / 4.27;
			chose.beta = beta * fromTable->a;
			chose.bite = fromTable->b;
			chose.accel = (float) (std::exp((double) fromTable->c / 12.0) * (double) n.hz * (double) chose.beta
				* (1.0 - (double) chose.beta) * (double) force * chose.bite / zBow);
			recalled = true;
			record.fromTable = 'b';
		}
		else if (fromTable && fromTable->kind == 'g') {
			glideRamp = fromTable->a;
			glideFloor = fromTable->b;
			record.fromTable = 'g';
		}
		else if (quick && guessOnly) {
			// THE GUESS: the instrument's middle bite, at the planned place.
			const double zBow = (double) design.strings[(size_t) s].impedance * 3.27 / 4.27;
			chose.beta = beta;
			chose.bite = habits.guessBite;
			chose.accel = (float) (std::exp((double) habits.guessAccel / 12.0) * (double) n.hz * (double) chose.beta
				* (1.0 - (double) chose.beta) * (double) force * chose.bite / zBow);
			recalled = true;
		}
		else if (quick) {
			const Remembered* r = recall(memoryIndexFor(n.hz, n.level, s, 0));
			if (r && !r->glide) {
				chose.beta = beta * r->betaShare;
				chose.bite = r->bite;
				chose.accel = r->accel;
				recalled = true;
			}
			else {
				glideRamp = r ? r->ramp : 0.02f;
				glideFloor = r ? r->floor : 0.2f;
			}
		}
		if (glideRamp > 0.f)
			glide(live, glideRamp, glideFloor);
		else if (recalled)
			attack(live, chose);
		else {
			if (bestPeriods == 999 && !list.empty())
				chose = list.front();
			if (list.empty()) {
				chose.beta = beta;
				chose.bite = 1.0;
				chose.accel = (float) (speed / 0.05);
			}
			attack(live, chose);
		}
		if (!quick) {
			learnedNow = true;
			learnedIndex = memoryIndexFor(n.hz, n.level, s, 0);
			learnedValue = Remembered();
			learnedValue.glide = glideRamp > 0.f;
			learnedValue.ramp = glideRamp;
			learnedValue.floor = glideFloor;
			learnedValue.betaShare = beta > 0.f ? chose.beta / beta : 1.f;
			learnedValue.bite = (float) chose.bite;
			learnedValue.accel = chose.accel;
			remember(learnedIndex, learnedValue);
			if (logFile) {
				// THE NOTE'S FULL LEVEL, as the fallback reads it: the most any start has from 200 ms on.
				const size_t settled = (size_t) (0.2 / ENVELOPE_HOP);
				double most = 0.0;
				auto settledOf = [&](const Trial& t) {
					double sum = 0.0;
					for (size_t k = settled; k < t.envelope.size(); k++)
						sum += t.envelope[k];
					return t.envelope.size() > settled ? sum / (double) (t.envelope.size() - settled) : 0.0;
				};
				for (const Trial& t : tried)
					most = std::max(most, settledOf(t));
				for (const Trial& t : glidesTried)
					most = std::max(most, settledOf(t));
				auto fullOf = [&](const Trial& t) {
					for (size_t k = 0; k < t.envelope.size(); k++)
						if (t.envelope[k] >= 0.5 * most)
							return (double) k * ENVELOPE_HOP + 0.01;
					return 9.0;
				};
				const double zBow = (double) design.strings[(size_t) s].impedance * 3.27 / 4.27;
				BowedString probe = at.inst.string(s);
				std::fprintf(logFile, "{\"instrument\":\"%s\",\"kind\":\"%s\",\"string\":%d,\"hz\":%.2f,\"semitone\":%d,"
					"\"level\":%.3f,\"speed\":%.3f,\"force\":%.3f,\"beta\":%.4f,\"direction\":%d,\"accent\":%d,"
					"\"previousHz\":%.2f,\"previousString\":%d,\"gap\":%.3f,\"ringingDb\":%.1f,\"temperature\":%.2f,"
					"\"settledDb\":%.1f,\"tried\":[",
					logName.c_str(), kind == Played::CROSS ? "cross" : "fresh", s, n.hz,
					(int) std::lround(12.0 * std::log2((double) n.hz / design.strings[(size_t) s].openHz)), n.level, speed,
					force, beta, dir, (int) n.accent, anyLast ? last.hz : 0.f, anyLast ? lastString : -1,
					anyLast ? (double) (t0 - lastEnd) / rate : -1.0,
					ringingFor > 0 ? 10.0 * std::log10(ringing / (double) ringingFor + 1e-30) : -300.0,
					probe.contactTemperature(), 10.0 * std::log10(most + 1e-30));
				bool first = true;
				for (size_t i = 0; i < tried.size(); i++) {
					const Trial& t = tried[i];
					const Rehearsed& c = list[i];
					const double kk = (double) c.accel * zBow / ((double) n.hz * (double) c.beta * (1.0 - (double) c.beta)
						* (double) force * c.bite);
					std::fprintf(logFile, "%s{\"start\":\"bite\",\"betaShare\":%.3f,\"bite\":%.3f,\"accelShare\":%.5f,"
						"\"periods\":%d,\"late\":%.4f,\"rough\":%.1f,\"click\":%.1f,\"rise\":%.1f,\"squeak\":%.1f,\"full\":%.3f}",
						first ? "" : ",", beta > 0.f ? c.beta / beta : 1.f, c.bite, kk, t.got, t.late, t.rough, t.click, t.rise,
						t.squeak, fullOf(t));
					first = false;
				}
				for (size_t i = 0; i < glidesTried.size(); i++) {
					const Trial& t = glidesTried[i];
					std::fprintf(logFile, "%s{\"start\":\"glide\",\"ramp\":%.2f,\"floor\":%.1f,"
						"\"periods\":%d,\"late\":%.4f,\"rough\":%.1f,\"click\":%.1f,\"rise\":%.1f,\"squeak\":%.1f,\"full\":%.3f}",
						first ? "" : ",", RAMPS[i / 2], FLOORS[i % 2], t.got, t.late, t.rough, t.click, t.rise, t.squeak, fullOf(t));
					first = false;
				}
				if (glideRamp > 0.f)
					std::fprintf(logFile, "],\"chose\":{\"start\":\"glide\",\"ramp\":%.2f,\"floor\":%.1f}}\n", glideRamp, glideFloor);
				else
					std::fprintf(logFile, "],\"chose\":{\"start\":\"bite\",\"betaShare\":%.3f,\"bite\":%.3f}}\n",
						beta > 0.f ? chose.beta / beta : 1.f, chose.bite);
			}
		}
		vibrato(live, t0);
		record.settled = bestPeriods >= 999 ? -1.0 : bestPeriods;
		quick = quickBefore;
	}

	record.string = s;
	record.hidden = hidden;
	record.bodyFrom = t0 + (int64_t) (0.06 * rate);
	record.movedAt = t0;
	record.bodyTo = end;
	record.kind = kind;
	record.direction = dir;
	record.force = force;
	record.speed = (float) speed;
	record.distance = distance;
	record.vibratoCents = cents;
	played.push_back(record);

	anyLast = true;
	last = n;
	lastString = s;
	lastStart = t0;
	lastEnd = end;
	lastForce = force;
	lastSpeed = signedSpeed;
	direction = dir;
	lastReleased = false;
	// A NOTE HELD UNTIL LET GO is not let go at its end: endNow does it.
	releaseAt = n.held ? std::numeric_limits<int64_t>::max() / 4 : end;
}


static double clickOf(const std::vector<float>& push, double rate);


double BowedPlayer::settles(State s, int string, float hz, int64_t restart, int64_t samples,
                            double* roughness, double* squeak, float cents, double* click) const {
	const double period = rate / (double) hz;
	std::vector<int64_t> slips;
	std::vector<float> force;
	// From the restart on, for the click.
	std::vector<float> after;
	bool was = s.inst.string(string).sticking();
	SlipWatch watch;
	float sound;
	for (int64_t i = 0; i < samples; i++) {
		step(s, rate, &sound);
		const Voice& v = s.voices[string];
		const bool stuck = stuckNow(s.inst.string(string), watch, v.force.value, v.speed.value);
		if (was && !stuck && s.clock >= restart)
			slips.push_back(s.clock);
		was = stuck;
		if (roughness || squeak)
			force.push_back(s.inst.string(string).bridgeForce());
		if (click && s.clock > restart)
			after.push_back(s.inst.string(string).bridgeForce());
	}
	if (click)
		*click = clickOf(after, rate);
	if (roughness) {
		const double from = (double) (restart - (s.clock - samples)) / rate;
		*roughness = startRoughness(force, std::fmax(0.0, from), rate);
	}
	if (squeak)
		*squeak = squeakOf(force, rate, hz, 0.01 + (std::pow(2.0, cents / 1200.0) - 1.0));
	// REGULAR FROM SLIP k TO THE END OF THE RUN, and at least four gaps of it: a change that turns
	// regular and then falls back into slipping twice a period has not settled.
	size_t k = slips.size();
	for (size_t j = slips.size(); j-- > 1; ) {
		if (std::fabs((double) (slips[j] - slips[j - 1]) - period) > 0.1 * period)
			break;
		k = j - 1;
	}
	if (k + 4 >= slips.size())
		return 999.0;
	return (double) (slips[k] - restart) / period;
}


/** HOW MUCH BRIGHTER A START IS THAN WHAT FOLLOWS: the push above 4 kHz, through two second-order
sections at the string's rate, its first CLICK_SECONDS against the rest of the first 150 ms, dB. */
static double clickOf(const std::vector<float>& push, double rate) {
	const double w = 2.0 * 3.14159265358979 * 4000.0 / rate;
	double e[2] = {0.0, 0.0};
	int n[2] = {0, 0};
	const double qs[2] = {0.5412, 1.3066};
	double x1[2] = {0, 0}, x2[2] = {0, 0}, y1[2] = {0, 0}, y2[2] = {0, 0};
	double b0[2], b1[2], b2[2], a1[2], a2[2];
	for (int k = 0; k < 2; k++) {
		const double al = std::sin(w) / (2.0 * qs[k]), c = std::cos(w), a0 = 1.0 + al;
		b0[k] = (1.0 + c) / 2.0 / a0;
		b1[k] = -(1.0 + c) / a0;
		b2[k] = (1.0 + c) / 2.0 / a0;
		a1[k] = -2.0 * c / a0;
		a2[k] = (1.0 - al) / a0;
	}
	const size_t first = (size_t) (CLICK_SECONDS * rate), last = std::min(push.size(), (size_t) (0.15 * rate));
	for (size_t i = 0; i < last; i++) {
		double v = push[i];
		for (int k = 0; k < 2; k++) {
			const double y = b0[k] * v + b1[k] * x1[k] + b2[k] * x2[k] - a1[k] * y1[k] - a2[k] * y2[k];
			x2[k] = x1[k];
			x1[k] = v;
			y2[k] = y1[k];
			y1[k] = y;
			v = y;
		}
		const int part = i < first ? 0 : 1;
		e[part] += v * v;
		n[part]++;
	}
	return (n[0] && n[1] && e[1] > 0.0) ? 10.0 * std::log10((e[0] / n[0]) / (e[1] / n[1]) + 1e-12) : 0.0;
}


void BowedPlayer::hearPlayed() {
	while (heardUpTo < played.size() && played[heardUpTo].movedAt <= live.clock) {
		const Played& p = played[heardUpTo++];
		// THE NOTE ALONE: a start is heard no further than the next note's, which a short note's
		// window would otherwise take in — a trial is heard on a copy that plays nothing after it.
		for (Hearing& h : hearings)
			h.until = std::min(h.until, p.movedAt);
		if (p.kind == Played::LEGATO)
			continue;
		Hearing h;
		h.from = p.movedAt;
		h.string = p.string;
		h.hz = p.hz;
		h.cents = p.vibratoCents;
		h.until = std::min(p.bodyTo, p.movedAt + (int64_t) (ROUGH_UNTIL * rate));
		h.push.reserve((size_t) (ROUGH_UNTIL * rate) + 1);
		hearings.push_back(h);
	}
	for (auto it = hearings.begin(); it != hearings.end(); ) {
		Hearing& h = *it;
		const Voice& v = live.voices[h.string];
		const BowedString& string = live.inst.string(h.string);
		const bool stuck = stuckNow(string, h.watch, v.force.value, v.speed.value);
		if (live.clock > h.from) {
			if (h.was && !stuck && h.firstSlip < 0)
				h.firstSlip = live.clock;
			h.push.push_back(string.bridgeForce());
		}
		h.was = stuck;
		if (live.clock < h.until) {
			++it;
			continue;
		}
		// HEARD TO ROUGH_UNTIL, or the note's end: judged as a trial is (attackSettles), and given to
		// its note.
		const double period = rate / (double) h.hz;
		const size_t squeakFor = std::min(h.push.size(), (size_t) (0.15 * rate));
		const double squeak = squeakOf(std::vector<float>(h.push.begin(), h.push.begin() + (long) squeakFor), rate,
			h.hz, 0.01 + (std::pow(2.0, h.cents / 1200.0) - 1.0));
		const double click = clickOf(h.push, rate);
		const int64_t start = h.firstSlip < 0 ? h.from : h.firstSlip + (int64_t) (4.0 * period);
		const size_t first = (size_t) std::max<int64_t>(0, start - h.from);
		const size_t last = h.push.size();
		const double rough = (first + (size_t) (8.0 * period) < last)
			? roughnessOf(std::vector<float>(h.push.begin() + (long) first, h.push.end()), rate, h.hz) : 0.0;
		for (size_t k = played.size(); k-- > 0; )
			if (played[k].movedAt == h.from && played[k].string == h.string) {
				played[k].heardSqueak = squeak;
				played[k].heardClick = click;
				played[k].heardRough = rough;
				break;
			}
		it = hearings.erase(it);
	}
}


int BowedPlayer::attackSettles(State s, int string, float hz, int64_t from, int64_t* simulated,
                               double* squeak, float cents, double* late, double* rough, double* click,
                               double* rise, std::vector<double>* envelope) const {
	const double period = rate / (double) hz;
	std::vector<int64_t> slips;
	std::vector<float> force, push;
	bool was = true;
	int64_t end = from + (int64_t) (1.5 * rate);
	const int64_t roughUntil = from + (int64_t) (ROUGH_UNTIL * rate);
	SlipWatch watch;
	float sound;
	while (s.clock < end) {
		step(s, rate, &sound);
		const Voice& v = s.voices[string];
		const bool stuck = stuckNow(s.inst.string(string), watch, v.force.value, v.speed.value);
		if (s.clock > from && was && !stuck) {
			slips.push_back(s.clock);
			if (slips.size() == 1) {
				end = std::min(end, s.clock + (int64_t) (20.0 * period));
				if (rough || rise || envelope)
					end = std::max(end, roughUntil);
				if (click)
					end = std::max(end, from + (int64_t) (0.15 * rate));
			}
		}
		was = stuck;
		const float bridge = s.inst.string(string).bridgeForce();
		if (squeak && s.clock - from < (int64_t) (0.15 * rate))
			force.push_back(bridge);
		if ((rough || click || rise || envelope) && s.clock > from)
			push.push_back(bridge);
	}
	if (simulated)
		*simulated = end - from;
	if (squeak)
		*squeak = squeakOf(force, rate, hz, 0.01 + (std::pow(2.0, cents / 1200.0) - 1.0));
	if (late)
		*late = slips.empty() ? 9.0 : (double) (slips[0] - from) / rate;
	if (click)
		*click = clickOf(push, rate);
	if (rise || envelope) {
		// THE VIBRATION, NOT THE PULL: the bow drags the string sideways from the moment it lands, and
		// that steady push on the bridge is at its full level at once however slowly the note's
		// vibration builds. Taken out by a one-pole high-pass at 30 Hz before the levels are compared.
		std::vector<double> ac(push.size());
		{
			const double pole = std::exp(-2.0 * 3.14159265358979 * 30.0 / rate);
			double x1 = 0.0, y1 = 0.0;
			for (size_t i = 0; i < push.size(); i++) {
				const double y = pole * (y1 + push[i] - x1);
				x1 = push[i];
				y1 = y;
				ac[i] = y;
			}
		}
		auto level = [&](double a, double b) {
			const size_t i0 = std::min(ac.size(), (size_t) (a * rate)), i1 = std::min(ac.size(), (size_t) (b * rate));
			double e = 0.0;
			for (size_t i = i0; i < i1; i++)
				e += ac[i] * ac[i];
			return (i1 > i0) ? e / (double) (i1 - i0) : 0.0;
		};
		const double early = level(0.06, 0.10), later = level(0.2, ROUGH_UNTIL);
		if (rise)
			*rise = (early > 0.0 && later > 0.0) ? 10.0 * std::log10(early / later) : -99.0;
		if (envelope) {
			envelope->clear();
			const double after = slips.empty() ? ROUGH_UNTIL : (double) (slips[0] - from) / rate;
			for (double t = 0.0; t + 0.01 <= ROUGH_UNTIL; t += ENVELOPE_HOP)
				envelope->push_back(t + 0.01 > after ? level(t, t + 0.01) : 0.0);
		}
	}
	if (rough) {
		// FROM THE FOURTH PERIOD AFTER THE FIRST SLIP: the first periods of any attack are its own.
		const int64_t start = slips.empty() ? from : slips[0] + (int64_t) (4.0 * period);
		const size_t first = (size_t) std::max<int64_t>(0, start - from);
		const size_t last = (size_t) std::max<int64_t>(0, std::min<int64_t>((int64_t) push.size(), roughUntil - from));
		*rough = (first + (size_t) (8.0 * period) < last)
			? roughnessOf(std::vector<float>(push.begin() + (long) first, push.begin() + (long) last), rate, hz)
			: 0.0;
	}
	if (slips.size() < 5)
		return -1;
	size_t k = slips.size();
	for (size_t j = slips.size(); j-- > 1; ) {
		if (std::fabs((double) (slips[j] - slips[j - 1]) - period) > 0.1 * period)
			break;
		k = j - 1;
	}
	if (k + 4 > slips.size())
		return -1;
	return (int) std::lround((double) (slips[k] - slips[0]) / period);
}


} // namespace px
