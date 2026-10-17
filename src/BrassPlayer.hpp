#pragma once
/** THE TROMBONE'S PLAYER: phase 6 of docs/brass.md.

It hears each note before it is due, decides how to play it, and moves the controls of a BrassInstrument —
the slide, the lips, the breath and the tongue — a sample at a time. It never touches the sound.

WHERE THE NOTE IS PLAYED. Every resonance the slide can sound the note on is a candidate; for each, the
slide is set where that resonance sounds the note in tune — the tables say where each resonance sounds at
every position, and between positions the slide is placed on a straight line in the logarithm of the
pitch — so the positions are where players put them, not seven fixed stops. Short extensions are preferred,
then the least movement from the note before, then joins that need no help: a slurred note on the same
resonance that needs the slide to move is helped by the legato tongue; one where the slide and the resonance
move the pitch opposite ways breaks cleanly by itself, the natural slur. The note after is looked at too.

HOW HARD. The written dynamic is a brightness, the spectral centroid the Iowa recordings have at that
dynamic in the note's register (BRIGHTNESS in BrassPlayer.cpp): from E2 to B2 590 Hz at pp, 877 at mf and
1526 at ff, from C3 to B3 602, 973 and 1927, from C4 to C5 676, 1039 and 1926, on straight lines between
the registers' middles, and carried on beyond pp and ff for ppp and fff. The player blows the
pressure at which the tables say the note has that centroid — so the dynamic is never a gain, and how loud
it is follows from the physics — and sets the lips the tables give for that pressure.

HOW IT JOINS THE NOTE BEFORE. Tongued: the tongue shuts, the slide moves and the lips set while it is shut,
the breath rises behind it, and it lets go over a few milliseconds, with the breath a little over its mark
for a moment, more for an accent. Tongued straight after a note, with no rest between, connected: as a
player tongues a line, the air kept up — the tongue stops it for the slide's quickest move and 10 ms more,
25 ms at least, the lips passing to the new note's setting while it is stopped — and the note speaks as
its start from silence was set by ear, from its floor over its own rise; or, where none was, at once at
its breath, a little over it. Slurred on one position: the lips glide to the next resonance over 50 ms,
the lip slur (Logie et al. 2009 measured 80 to 100 ms between steady states on the horn). Slurred with the
slide moving: the natural slur, or the legato tongue — the tongue half closes while the slide moves, and
opens on the note. Written as a glissando: the slide moves through the last part of the note before, on one
resonance, the lips following it.

HOW LOUD. The blowing makes a loud note brighter, and louder than a player's is: more so the higher it
is, where the model's ff is 16 dB over its mf and the Iowa recordings' 7. So each note's sound is then
turned up or down by a gain for its marking and register, which brings the markings to the recordings'
levels against mf (LOUDNESS in BrassPlayer.cpp), the brightness left as the blowing made it; over mf, by
the note's own level in the tables, since one note can need far more breath than its neighbours for the
same brightness.

HOW IT ENDS. The breath falls over 45 ms at the end of the note's written length, sooner for a staccato
(half its length) or before a tongued note that follows closely, so the slide has time to move.

VIBRATO by the slide, as trombonists make it: 10 cents slight and 22 wide, 5.5 Hz, from 180 ms into a note
that lasts long enough.

NO RACK IN HERE. */
#include "BrassInstrument.hpp"
#include "BrassJudge.hpp"
#include "BrassTable.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace px {


struct Crew;

struct BrassPlayer {
	/** A NOTE AS THE PLAYER IS TOLD IT. */
	struct Note {
		double hz = 233.08;
		/** Nought to one, the cable's ladder: ppp nought, pp a seventh, mf four sevenths, ff six, fff one. */
		double level = 4.0 / 7.0;
		double seconds = 0.5;
		/** Runs into the note after it without a fresh attack. */
		bool slurred = false;
		/** Slides into the note after it. */
		bool glissando = false;
		bool staccato = false, tenuto = false, accent = false, heavy = false, ghost = false;
		/** 0 none, 1 slight, 2 wide. */
		int vibrato = 0;
		/** Joined to the note before however long ago it began: a note held from a keyboard moving to the
		next, which nobody said was slurred because nobody knew how long it would be. */
		bool fromSlur = false;
		/** ITS OWN START AND TONE, set by ear (mpxTrombone's tuning): where `rise` is nought or more, a
		tongued start is played so, untried — the breath rising over `rise` seconds from 1.3 times the
		threshold, the lips set `lips` times their note's setting and gliding to it, nothing over the mark —
		its breath `breath` times what its dynamic asks, and its breath noise `air` (BrassInstrument::Design::
		breathNoise) where that is nought or more. */
		double rise = -1.0, lips = 1.0, breath = 1.0, air = -1.0;
		/** And where more than nought, the breath's start, times the threshold, in place of 1.3; and the curve it
		rises by (Habits::riseCurve). */
		double startFloor = -1.0, riseCurve = -1.0;
		/** ITS OWN LIPS, set by ear: their mass and quality open (BrassHingedLips::Shape), the trombone's own
		where nought or less; and where `tunedLips` is more than nought, their tension: their own frequency as a
		share of the resonance's, in place of the tables', which were found with the trombone's own lips. */
		double mass = -1.0, quality = -1.0, tunedLips = -1.0;
		/** And where more than nought, the mouth's pressure for its dynamic with those lips (tuneFor), in
		place of the tables', before `breath` trims it; and the slide moved out as far as would flatten the
		note by `slideCents`, in by as far as would sharpen it where less than nought, which brings it back in
		tune with them, as far as the slide reaches. */
		double tunedMouth = -1.0, slideCents = 0.0;
		/** And where `hasGap`, the lips' gap at rest, metres, less than nought pressed together
		(BrassHingedLips::Shape::restY), in place of the trombone's own. */
		bool hasGap = false;
		double gap = 0.0;
		/** ITS WAVER, as a living player's breath and lips are never quite still: each a slow random wander,
		forgetting itself in about 55 ms, its spread these shares of the breath and of the lips' tension. The
		Iowa trombonist's held notes wander 0.10 to 0.28 dB in level and 0.6 to 2.3 cents in pitch, most on
		the low notes (make brassplay ARGS=... see docs/brass.md). Nought, still. */
		double waverBreath = 0.0, waverLips = 0.0;
		/** TONGUED IN A PASSAGE, connected straight after the note before: where `passagePush` is more than
		minus one, the breath behind the stopped tongue that share over the note's, under it where less than
		nought, the tongue letting go over `passageRelease` seconds and the breath going to the note's over
		`passageSettle` after it — the breath's attack — in place of the note's start from silence, which the
		recordings of single notes set slow. */
		double passagePush = -1.0, passageRelease = 0.004, passageSettle = 0.03;
		/** And the tongue shutting over `passageClose` seconds; where `passageEased`, at a player's pace: both
		the tongue's moves on an eased curve, not a straight line, whose corners clicked; the lips' tension
		and their mass, quality and gap gliding over 20 ms while it is stopped, not changed at a stroke. */
		double passageClose = 0.004;
		bool passageEased = false;
		/** And the slide's move into it taking this long, seconds, where longer than its quickest. */
		double passageSlide = 0.0;
		/** THE VOCAL TRACT while it sounds: the three formants' frequencies, hertz, strengths, pascal seconds a
		cubic metre, and qualities (BrassHingedLips::setFormant), moving to them while the tongue is stopped
		before it, or in place before it starts from silence. Strength nought, none. */
		double formantHz[3] = {0.0, 0.0, 0.0}, formantStrength[3] = {0.0, 0.0, 0.0}, formantQuality[3] = {6.0, 6.0, 6.0};
	};

	/** HOW THE PLAYER PLAYS, as numbers a bench can vary. */
	struct Habits {
		/** The lip slur: the lips glide over this, seconds, centred on the note. Chosen with the push below by
		make brassplay ARGS=lipsweep: shorter glides and more push both make the dip at the join smaller, 13 dB
		under the quieter note at 50 ms without push, 9 at 30 ms with a push of a fifth. */
		double lipSlur = 0.03;
		/** The natural slur: the lips change resonance over this, seconds, centred on the note. */
		double naturalSlur = 0.024;
		/** The legato tongue: it closes until the lips feel this many times their threshold, but no less
		open than the first nor more than the second. At 1.25 times the slurred scale dipped 18 dB at its
		legato tongues, the low notes most, where the lips just over their threshold are very quiet; at twice,
		9 dB (make brassplay ARGS=legatosweep). */
		double legatoAbove = 2.0, legatoLeast = 0.45, legatoMost = 0.85;
		/** Through a slur, the breath rises by this share, gone again by the note's start plus the glide — the
		support players give a slur. Without it the natural slurs of the slurred scale dipped 12 dB, with a fifth
		6 (make brassplay ARGS=sweep). */
		double slurPush = 0.2;
		/** In a slur, the lips first aim this many times past the new note's setting, the way they move
		towards it, and settle on it over the glide after; one aims straight at it. */
		double lipsOvershoot = 1.0;
		/** In a slur, the lips change this many seconds later than centred on the note. */
		double slurShift = 0.0;
		/** In a slur, the slide moves this share of the time it takes between tongued notes. */
		double slurSlide = 1.0;
		/** In a natural slur, the lips change resonance this share of the way through the slide's movement. */
		double naturalLipsAt = 0.5;
		/** The tongue's release, seconds: accented, ordinary, soft. */
		double releaseAccent = 0.0015, releaseOrdinary = 0.003, releaseSoft = 0.008;
		/** The breath over its mark as the tongue lets go, for a moment: ordinary, accent, heavy accent. A
		quarter over for an accent sent F sharp 2 nearly a semitone sharp and 5 dB over the note at its front. */
		double overOrdinary = 0.08, overAccent = 0.12, overHeavy = 0.25;
		/** A TONGUED START: the lips set this many times their note's setting as the tongue lets go, gliding
		to it over `startGlide` seconds; and the breath behind the tongue at its mark at once, or, where
		`startRise` is more than nought, `startFloor` times the note's threshold, the lips set as for that,
		both rising to the mark over `startRise` seconds, the breath by equal ratios, quicker at first. */
		double startLips = 1.0, startGlide = 0.03, startFloor = 1.3, startRise = 0.14;
		/** The rise held to the note's length less 80 ms, not to half of it: a start set by ear. */
		bool riseWhole = false;
		/** THE CURVE THE BREATH RISES BY, by equal ratios: at a share u of the rise it has come 1 - (1 - u)^curve
		of the way, in its logarithm. Two, quick at first and settling; one, steadily; more, quicker still at
		first, the last of it slower. */
		double riseCurve = 2.0;
	};

	/** HOW A NOTE WAS JOINED TO THE ONE BEFORE. */
	enum Join { TONGUED, LIP_SLUR, NATURAL_SLUR, LEGATO_TONGUE, GLISSANDO };

	/** WHAT THE PLAYER DECIDED, kept for benches. */
	struct Decision {
		int64_t start = 0, end = 0;
		Note note;
		int position = 1, resonance = 2;
		double extension = 0.0, mouth = 0.0, lips = 1.0, sounds = 0.0, centroid = 0.0;
		Join join = TONGUED;
		/** How many ways of making the join were tried on a copy, and how the one played was judged. */
		int tried = 0;
		BrassJudged judged;
		/** Which of its join's ways it was played, by its place among them. */
		int way = -1;
		/** The gain its sound is played at, for its loudness (see HOW LOUD). */
		double gain = 1.0;
		/** A tongued note follows it at once, without a rest: its breath is not let go, and the next note is
		tongued into it connected (see HOW IT JOINS). */
		bool connectedNext = false;
	};

	void setup(const BrassInstrument::Design& design, const BrassTable& table, const Habits& habits);
	void setup(const BrassInstrument::Design& design, const BrassTable& table);
	/** TRY EACH SLUR'S AND EACH TONGUED START'S WAYS on a copy before playing it, and play the best
	(BrassJudge); and remember the best for each situation, so that one met again is played as it was
	without being tried again. */
	void setRehearsing(bool on) { rehearsing = on; }
	/** FEWER WAYS TRIED: a third of each join's, for a third of the computing. */
	void setFewerWays(bool on) { fewer = on; }
	/** Threads to share the trials of a rehearsal out over; none is the trials in order on the caller's. */
	void setCrew(Crew* c) { crew = c; }
	/** How many rehearsals were run, and how many decisions were taken from memory instead. */
	int rehearsals() const { return rehearsedCount; }
	int remembered() const { return rememberedCount; }
	/** How many notes were played by the habits alone: neither rehearsed nor remembered. */
	int unrehearsed() const { return habitCount; }
	/** A NOTE STARTING `start` samples from the player's beginning. Notes come in order of start. One not to
	be rehearsed, rehearsing or not — from a keyboard, due at once — is played the way remembered for its
	situation, or else by the habits. */
	void add(const Note& n, int64_t start, bool rehearse = true);
	/** THE NOTE SOUNDING ENDS NOW: a key let go. */
	void release();
	/** EVERY NOTE NOT YET BEGUN FORGOTTEN, and the one sounding ended now: the music stopped or taken back. */
	void withdraw();
	/** THE MARKINGS' LOUDNESS BROUGHT TO THE RECORDINGS', by a gain on each note (see HOW LOUD): on unless a
	bench measuring the model's own turns it off. */
	void setEvenLoudness(bool on) { evenLoudness = on; }
	/** A START TRIED BY EAR'S ALTERNATIVES: how a tongued note was started, and how it came out — how far
	from the recordings' starts, and whether clean (BrassStart). */
	struct StartTry {
		double rise = 0.0, lips = 1.0, distance = 0.0;
		bool clean = false;
	};
	/** THE WAYS TO START A NOTE, from silence, best first: the breath rising over 40 to 600 ms from 1.3 times
	the threshold, no longer than the note less 80 ms, the lips at their note's setting, each played on a
	player of its own and measured against the recordings' starts; the clean first, nearest the recordings
	first. The trials are shared over `crew` where there is one. For tuning by ear, where they are offered in
	turn. */
	static std::vector<StartTry> startsFor(const BrassInstrument::Design& design, const BrassTable& table, const Habits& habits, const Note& n,
		Crew* crew);
	/** A NOTE PLAYED WITH ITS OWN LIPS (Note::mass, quality), as the tables would have it were they found with
	those lips: on the resonance the player would give it, the mouth's pressure where its brightness reaches its
	dynamic's, and, where the note gives their tension (Note::tunedLips), the lips held at it and the slide
	moved to sound the note in tune, as a player does — by halving between 150 cents in and 150 out, the slide
	no further than it reaches; or else, the slide where the player would put it, the lips' setting that
	sounds it in tune there, by halving between 0.70 and 1.30 of the resonance. Six pressures from 0.8 to 4
	times the tables' for the dynamic, shared over `crew`; two or three seconds. The mouth nought or less where
	the note does not sound; and how far out of tune it is left, cents, where the slide could not reach. With the
	lips' own gap where the note gives one (Note::gap); and with their tension, the slide then put right for the
	breath as the note trims it (Note::breath), which pulls the pitch. With the tension and the mouth given
	(Note::tunedMouth), only the slide is found, at that mouth as the breath trims it. */
	struct Tuned {
		double mouth = -1.0, lips = -1.0, slideCents = 0.0, offCents = 0.0;
	};
	static Tuned tuneFor(const BrassInstrument::Design& design, const BrassTable& table, const Habits& habits, const Note& n, Crew* crew);
	/** THE DECISIONS KEPT, at most: the oldest are let go past it, for playing on for hours. */
	void keepDecisions(size_t most) { keptMost = most; }
	/** ONE SAMPLE: the pressure at the listener, pascals. */
	double process();

	int64_t clock() const { return live.now; }
	/** What the tables say a decision's note sounds at, at its pressure, dB at the listener. */
	double tableLevel(const Decision& d) const { return tableAt(d.resonance, d.extension, d.mouth).level; }
	const std::vector<Decision>& decisions() const { return decided; }
	BrassInstrument& instrument() { return live.inst; }

private:
	struct Pending {
		Note note;
		int64_t start = 0;
		bool rehearse = true;
	};
	/** WHERE A RESONANCE SOUNDS A PITCH: the slide's extension, and what is left for the lips. */
	struct Place {
		bool can = false;
		int resonance = 2;
		double extension = 0.0, left = 0.0;
	};
	/** A CONTROL MOVING FROM ONE VALUE TO ANOTHER over a time, on a smooth curve or a straight line. */
	struct Ramp {
		double from = 0.0, to = 0.0;
		int64_t at = 0, length = 0;
		bool straight = false, ratios = false;
		/** By ratios, the curve (Habits::riseCurve). */
		double curve = 2.0;
		double valueAt(int64_t t) const;
		void move(int64_t t, double target, int64_t samples, bool line = false, bool byRatios = false, double byCurve = 2.0) {
			from = valueAt(t);
			to = target;
			at = t;
			length = samples;
			straight = line;
			ratios = byRatios;
			curve = byCurve;
		}
	};
	struct Action {
		int64_t at = 0;
		enum Kind { SLIDE, LIPS, BREATH, TONGUE, VIBRATO, NOISE, LIPMASS, WAVER, FORMANT } kind = BREATH;
		/** LIPMASS: the mass, the quality open, the gap at rest, and the note's breath that holds it open.
		FORMANT: formant `resonance`'s frequency, strength and quality. */
		double value = 0.0, seconds = 0.0, value2 = 0.0, value3 = 0.0, value4 = 0.0;
		int resonance = 0;
		/** A straight line, not eased; or by equal ratios, easing out, along `curve`. */
		bool straight = false, ratios = false;
		double curve = 2.0;
	};

	/** EVERYTHING THAT MOVES WHILE PLAYING, so that a rehearsal can copy it and play on from it. */
	struct Live {
		BrassInstrument inst;
		std::vector<Action> actions;
		int64_t now = 0;
		Ramp slide, breath, tongue, lipsHz;
		/** After a glide, the lips follow the resonance as the slide moves: ratio times its frequency. */
		int lipsResonance = 2;
		double lipsRatio = 1.0;
		int64_t lipsFollowFrom = 0;
		double vibratoDepth = 0.0, vibratoPhase = 0.0;
		int64_t vibratoFrom = 0, vibratoUntil = 0;
		/** THE WAVER: its spreads, the wanders of the breath and the lips, each from and to, a step apart, and
		their random sequence, the same every time. */
		double waverBreath = 0.0, waverLips = 0.0;
		double wanderBreath[2] = {0.0, 0.0}, wanderLips[2] = {0.0, 0.0};
		int64_t wanderAt = 0;
		uint64_t wanderState = 0x2545f4914f6cdd1dull;
		/** THE FORMANTS, each moving from and to (frequency, strength, quality) over a time; and whether each is
		still to be told to the lips. */
		double formantFrom[3][3] = {}, formantTo[3][3] = {};
		int64_t formantAt[3] = {0, 0, 0}, formantLength[3] = {0, 0, 0};
		bool formantMoving[3] = {false, false, false};
		/** THE LIPS' MASS, QUALITY AND GAP now, and gliding from and to over a time. */
		double lipNow[3] = {0.0, 0.0, 0.0}, lipFrom[3] = {0.0, 0.0, 0.0}, lipTo[3] = {0.0, 0.0, 0.0};
		int64_t lipAt = 0, lipLength = 0;
		bool lipMoving = false;
	};

	/** Beyond the slide's ends the lips make up what is left, up to 20 cents, past which the resonance cannot
	play the note; or, `anyway`, the slide is put at its end and the note left out of tune. */
	Place placeFor(double hz, int resonance, bool anyway = false) const;
	/** The resonance's frequency at an extension, from the tables, on a straight line in its logarithm. */
	double resonanceAt(int resonance, double extension) const;
	/** What the tables say at an extension between positions, at a pressure. */
	BrassTablePoint tableAt(int resonance, double extension, double mouth) const;
	double pressureFor(int resonance, double extension, double centroidHz) const;
	double thresholdAt(int resonance, double extension) const;
	double secondsToMove(double from, double to) const;
	/** The least time the slide can move between two extensions: about 5 m/s on average. */
	double quickest(double from, double to) const;
	void decide(const Pending& p);
	/** THE ACTIONS THAT MAKE A NOTE'S JOIN, with these habits, into `into`, no earlier than `earliest`. */
	void joinActions(const Decision& d, double dynamic, const Habits& h, int64_t earliest, std::vector<Action>& into) const;
	/** The ways of making a join a rehearsal tries. */
	std::vector<Habits> candidates(Join join) const;
	/** WHAT A SITUATION IS, for remembering how it was best played: the join, the resonance, where the slide
	is to two centimetres, the dynamic's step, the note before — its resonance, or none where it had ended —
	its accent, and whether it is quick. */
	uint64_t situation(const Decision& d, double dynamic, bool sounding) const;
	static void schedule(Live& l, const Action& a);
	void apply(Live& l, const Action& a) const;
	double step(Live& l) const;

	Habits habits;
	/** The habits a start set by ear is played with (Note::rise). */
	Habits earHabits(const Note& n) const;
	/** The trombone's own breath noise and lips, for notes that do not set theirs. */
	double defaultAir = 0.0, defaultMass = 0.9, defaultQuality = 3.0, defaultGap = 0.0;
	bool rehearsing = false, fewer = false, evenLoudness = true;
	/** THE GAIN FOR LOUDNESS, gliding to each note's as it begins: the changes to come, at their samples. */
	struct GainChange {
		int64_t at = 0;
		double gain = 1.0;
	};
	std::deque<GainChange> gainChanges;
	double gainFrom = 1.0, gainTo = 1.0;
	int64_t gainAt = 0;
	/** The gain for a decided note at its marking, nought for ppp to seven for fff. */
	double loudnessGain(const Decision& d, double dynamic) const;
	double gainNow() const;
	size_t keptMost = (size_t) -1;
	Crew* crew = nullptr;
	/** The best way found for each situation: its join and the index of its habits in candidates(). */
	std::map<uint64_t, std::pair<Join, int>> memory;
	int rehearsedCount = 0, rememberedCount = 0, habitCount = 0;
	const BrassTable* table = nullptr;
	double rate = 48000.0;
	Live live;
	std::deque<Pending> upcoming;
	std::vector<Decision> decided;
	/** The notes of each resonance, by position, from the tables. */
	std::vector<std::vector<const BrassTableNote*>> byResonance;
	bool hasLast = false;
	Decision last;
};


} // namespace px
