#pragma once
/** THE BOWED PLAYER ON TWO THREADS: what a module plays. Phase 8 of docs/bowed-strings.md.

THE PLAYER REHEARSES, AND REHEARSING TAKES TIME. Before a note from the string it tries a dozen and
more starts on copies of the instrument, and before a bow change nine ways of changing; on the Fauré
that came to about a third of a second of computing a note. A sample is twenty microseconds. So the
deciding is done on a thread of its own, by a PLANNER copy of the player running ahead through the
notes the cable announces, and the audio thread runs a PLAYING copy that carries out what the
planner decided (see BowedPlayer.hpp, "ON TWO THREADS IN A MODULE").

EXACT, NOT APPROXIMATE. The planner's copy of the instrument is exactly what the playing one will be
when a note starts: the same arithmetic on the same state, every decision before it carried out in
both. So what was rehearsed is what is played, and a part played through this is the same, sample
for sample, as the player playing it alone at leisure — which a bench checks (make bowplayer
ARGS="engine ...").

WHAT NOBODY ANNOUNCED is played at once by the playing copy without rehearsing, and so is anything
the planner did not decide in time; the planner then starts again from the playing copy as it stands.
The same happens when the playing copy does something no planner could foresee: a note let go from a
keyboard, or everything announced withdrawn.

NO RACK IN HERE, so a bench can drive it. */
#include "BowedPlayer.hpp"
#include "Crew.hpp"

#include <atomic>
#include <memory>
#include <thread>

namespace px {


/** A RING BETWEEN TWO THREADS, one writing and one reading, of a fixed size. */
template <typename T, int N>
struct HandOver {
	T items[N];
	std::atomic<uint32_t> written{0}, read{0};
	bool push(const T& x) {
		const uint32_t w = written.load(std::memory_order_relaxed);
		if (w - read.load(std::memory_order_acquire) >= (uint32_t) N)
			return false;
		items[w % N] = x;
		written.store(w + 1, std::memory_order_release);
		return true;
	}
	bool peek(T& x) const {
		const uint32_t r = read.load(std::memory_order_relaxed);
		if (r == written.load(std::memory_order_acquire))
			return false;
		x = items[r % N];
		return true;
	}
	bool pop(T& x) {
		if (!peek(x))
			return false;
		read.store(read.load(std::memory_order_relaxed) + 1, std::memory_order_release);
		return true;
	}
};


struct BowedEngine : BowedPlayer::Planning {
	BowedEngine();
	~BowedEngine() override;

	/** WHAT AN ENGINE IS SET UP FOR, assembled field by field: the instrument and its player; the rates,
	the engine's and the highest note the part will reach, which sets how many times faster the strings
	run; how far ahead notes are announced, seconds, the cable's MPX_ANNOUNCE_AHEAD; whether the other
	strings ring in sympathy; economy; and how much each start is rehearsed.

	ECONOMY, for less computing and less fidelity: the strings run half as many times faster than the
	engine as the highest note would have them — the strings made ready for the note an octave down —
	and a string nobody is playing stops once it has rung down (BowedInstrument::setQuietStringsRest).

	REHEARSING: see BowedPlayer::Rehearsing. Live by default: the least computing, and by ear good for
	everyday playing; more is rehearsed at the higher levels for a performance. LIVE has no planner:
	the playing copy decides every note itself as it comes, so no thread is started and nothing need be
	announced. */
	struct Setup {
		BowedInstrument::Design design;
		BowedPlayer::Habits habits;
		float engineRate = 48000.f, highestHz = 1280.f;
		double announcedAhead = 3.0;
		bool sympathetic = true, economy = false;
		BowedPlayer::Rehearsing rehearsing = BowedPlayer::LIVE;
	};
	/** Everything starts again from silence and the planning thread is begun afresh. Not on the audio
	thread. */
	void setup(const Setup& s);

	/** A NOTE ANNOUNCED, to start `ahead` engine samples from now. Audio thread. */
	void announce(const BowedPlayer::Note& n, int64_t ahead);
	/** A NOTE STARTING NOW that nobody announced. Audio thread. */
	void playNow(const BowedPlayer::Note& n);
	/** THE BOW LIFTED NOW: a note from a keyboard let go. Audio thread. */
	void endNow();
	/** EVERYTHING ANNOUNCED AND NOT BEGUN, VOID. Audio thread. */
	void withdraw();
	/** ONE SAMPLE AT THE ENGINE'S RATE: the sound a metre away, pascals. Audio thread. */
	float process();
	/** The vibrato's width, a share of what the player decides. Audio thread. */
	void setVibrato(float k) { playing.setVibratoScale(k); }

	/** FOR A BENCH: wait for the planner to decide rather than deciding without it, so a part played
	faster than real time is still played exactly. */
	void setPatient(bool on) { patient.store(on); }
	/** WHETHER THE PLAYING COPY COUNTS EACH NOTE'S SLIPS, for a bench's report. Off in a module. Before
	anything plays. */
	void setWatching(bool on) { playing.setWatching(on); }
	/** How many times the planner had to start again. */
	int restarts() const { return restartCount; }
	/** The notes played with nothing rehearsed for them, though a planner was rehearsing: see
	BowedPlayer::missed. */
	int missed() const { return playing.missed; }
	/** The count back to nought. Audio thread. */
	void clearMissed() { playing.missed = 0; }
	/** THE BOW TAKEN AFRESH for the music played again from its start: see BowedPlayer::retakeBow.
	Audio thread; the planner starts again from it. */
	void retakeBow() { playing.retakeBow(); }
	double missedAt(int i) const { return (double) playing.missedAt[i] / playing.stringRate(); }
	const BowedPlayer& player() const { return playing; }

	BowedPlayer::Plan* planFor(uint32_t generation, int64_t serial) override;
	void spent(BowedPlayer::Plan* p) override;
	void decided(BowedPlayer::Plan* p) override;

private:
	/** WHAT THE AUDIO THREAD TELLS THE PLANNING THREAD. */
	struct Message {
		enum Kind : uint8_t { NOTE, RESTART } kind = NOTE;
		BowedPlayer::Note note;
		int64_t start = 0;
		BowedPlayer* from = nullptr;
	};

	BowedPlayer playing;
	/** The planning thread's own; null before setup. */
	BowedPlayer* planner = nullptr;
	/** THE PLANNER'S HELPERS, rehearsing a note's starts side by side: one crew for every bowed module
	in the patch, about half the machine's cores, the rest left to Rack. A crew each was four threads a
	module, and three modules' fifteen busy threads kept Rack's own waiting. */
	std::shared_ptr<Crew> crew;
	std::thread worker;
	std::atomic<bool> stopping{false};
	std::atomic<bool> patient{false};
	HandOver<Message, 512> toPlanner;
	HandOver<BowedPlayer::Plan*, 1024> plans, used;
	/** Where the playing copy has got to, for how far ahead the planner may run. */
	std::atomic<int64_t> playingClock{0};
	/** The planner's generation and decisions made, for a bench that waits on it. */
	std::atomic<uint32_t> plannerGeneration{0};
	std::atomic<int64_t> plannerDecisions{0};
	/** How far ahead of the playing copy the planner may run, in the strings' samples: less than
	notes are announced ahead by, less the time a decision is made before its note, so that every
	note it decides about has been announced to it. */
	int64_t horizon = 0;
	int factor = 1;
	int restartCount = 0;
	/** Whether there is a planner: not at LIVE. */
	bool planned = true;

	void run();
	void stop();
	/** The planner made again from the playing copy as it now stands. Audio thread. */
	void restart();
};


} // namespace px
