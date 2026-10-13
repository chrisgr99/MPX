/** See BowedEngine.hpp. */
#include "BowedEngine.hpp"

#include <chrono>
#include <mutex>

namespace px {


/** HOW LONG A STRETCH THE PLANNER RUNS BEFORE LOOKING FOR NEW NOTES, seconds. */
static const double STRETCH = 0.01;


/** THE ONE CREW, made when the first engine wants it and gone when the last is. */
static std::shared_ptr<Crew> sharedCrew() {
	static std::mutex guard;
	static std::weak_ptr<Crew> shared;
	std::lock_guard<std::mutex> hold(guard);
	std::shared_ptr<Crew> crew = shared.lock();
	if (!crew) {
		const int cores = (int) std::thread::hardware_concurrency();
		crew = std::make_shared<Crew>(std::max(0, std::min(6, cores / 2 - 1)));
		shared = crew;
	}
	return crew;
}


BowedEngine::BowedEngine() {}


BowedEngine::~BowedEngine() {
	stop();
}


void BowedEngine::stop() {
	stopping.store(true);
	if (worker.joinable())
		worker.join();
	stopping.store(false);
	delete planner;
	planner = nullptr;
	BowedPlayer::Plan* p = nullptr;
	while (plans.pop(p))
		delete p;
	while (used.pop(p))
		delete p;
	Message m;
	while (toPlanner.pop(m))
		delete m.from;
}


void BowedEngine::setup(const Setup& s) {
	stop();
	playing = BowedPlayer();
	playing.setHabits(s.habits);
	playing.setRehearsing(s.rehearsing);
	playing.setInstrument(s.design, s.engineRate, s.economy ? 0.5f * s.highestHz : s.highestHz);
	playing.setSympathetic(s.sympathetic);
	playing.setQuietStringsRest(s.economy);
	// LIVE: no planner. The player decides every note itself, without rehearsing.
	planned = s.rehearsing != BowedPlayer::LIVE;
	playing.setMode(planned ? BowedPlayer::PLAYING : BowedPlayer::SOLO, planned ? this : nullptr);
	factor = (int) std::lround(playing.stringRate() / (double) s.engineRate);
	// EVERY NOTE THE PLANNER DECIDES ABOUT HAS BEEN ANNOUNCED TO IT: it decides PREPARE before a note,
	// looks LOOK_AHEAD past the note for the stroke after (BowedPlayer::plan), and whether a note's end
	// joins the next looks a little past that (BowedPlayer, JOINED), so it keeps all of that and a
	// little more inside what has been announced.
	horizon = (int64_t) (std::fmax(0.0, s.announcedAhead - BowedPlayer::PREPARE - BowedPlayer::LOOK_AHEAD - 0.06)
		* playing.stringRate());
	playingClock.store(0);
	restartCount = 0;
	if (!planned)
		return;
	if (!crew)
		crew = sharedCrew();
	planner = new BowedPlayer(playing);
	planner->setMode(BowedPlayer::PLANNER, this);
	planner->setCrew(crew.get());
	plannerGeneration.store(planner->generation);
	plannerDecisions.store(planner->decisions);
	restartCount = 0;
	worker = std::thread([this]() { run(); });
}


void BowedEngine::announce(const BowedPlayer::Note& n, int64_t ahead) {
	const int64_t start = playing.clock() + std::max<int64_t>(0, ahead) * factor;
	playing.addAt(n, start);
	Message m;
	m.kind = Message::NOTE;
	m.note = n;
	m.start = start;
	// A planner that cannot be told about a note will not decide it, and the playing copy then does.
	if (planned)
		toPlanner.push(m);
}


void BowedEngine::playNow(const BowedPlayer::Note& n) {
	BowedPlayer::Note now = n;
	now.unannounced = true;
	playing.addAt(now, playing.clock());
}


void BowedEngine::endNow() {
	playing.endNow();
}


void BowedEngine::withdraw() {
	playing.withdraw();
}


float BowedEngine::process() {
	const float sound = playing.process();
	playingClock.store(playing.clock(), std::memory_order_release);
	if (planned && playing.lost)
		restart();
	return sound;
}


void BowedEngine::restart() {
	playing.lost = false;
	playing.generation++;
	// A COPY OF THE PLAYING ONE, made here because only this thread may read it. It is the one place
	// the audio thread allocates, and it happens only when something nobody planned has been played.
	Message m;
	m.kind = Message::RESTART;
	m.from = new BowedPlayer(playing);
	m.from->setMode(BowedPlayer::PLANNER, this);
	if (!toPlanner.push(m))
		delete m.from;
	restartCount++;
}


BowedPlayer::Plan* BowedEngine::planFor(uint32_t generation, int64_t serial) {
	const auto began = std::chrono::steady_clock::now();
	for (;;) {
		// Read before looking, so that a plan handed over in between is still found below.
		const bool passed = plannerGeneration.load() == generation && plannerDecisions.load() > serial;
		BowedPlayer::Plan* p = nullptr;
		while (plans.peek(p)) {
			// Decided before the planner last started again, or for a decision already made: of no use.
			if (p->generation != generation || p->serial < serial) {
				plans.pop(p);
				spent(p);
				continue;
			}
			if (p->serial == serial) {
				plans.pop(p);
				return p;
			}
			break;
		}
		if (!patient.load())
			return nullptr;
		// WAITING, on a bench: while the planner, as it now is, has still to reach this decision — and
		// not for ever, since a note nobody announced is one it will never decide.
		if (passed || !worker.joinable()
				|| std::chrono::steady_clock::now() - began > std::chrono::seconds(5))
			return nullptr;
		std::this_thread::sleep_for(std::chrono::microseconds(50));
	}
}


void BowedEngine::spent(BowedPlayer::Plan* p) {
	if (!used.push(p))
		delete p;
}


void BowedEngine::decided(BowedPlayer::Plan* p) {
	if (!plans.push(p))
		delete p;
}


void BowedEngine::run() {
	crewStandsBack();
	const int64_t stretch = (int64_t) (STRETCH * planner->stringRate());
	while (!stopping.load()) {
		Message m;
		while (toPlanner.pop(m)) {
			if (m.kind == Message::NOTE)
				planner->addAt(m.note, m.start);
			else {
				delete planner;
				planner = m.from;
				planner->setCrew(crew.get());
				plannerGeneration.store(planner->generation);
				plannerDecisions.store(planner->decisions);
			}
		}
		BowedPlayer::Plan* p = nullptr;
		while (used.pop(p))
			delete p;
		const int64_t limit = playingClock.load(std::memory_order_acquire) + horizon;
		if (planner->clock() >= limit) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
		const int64_t until = std::min(limit, planner->clock() + stretch);
		while (planner->clock() < until && !stopping.load())
			planner->process();
		plannerDecisions.store(planner->decisions);
	}
}


} // namespace px
