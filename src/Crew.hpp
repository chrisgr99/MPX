#pragma once
/** A CREW: work shared out over a few threads, each piece on one of them, and back when all are done.

FOR TRIALS THAT DO NOT DEPEND ON EACH OTHER: the bowed player rehearses a dozen starts and more for a
note, each on its own copy of the instrument, and only then compares them. The comparing stays in
order on the thread that asked; only the trials are shared out, so what is chosen is exactly what one
thread would have chosen. See BowedEngine.hpp.

The thread that asks works too, so a crew of none is the work done in order on that thread. No Rack. */
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#if defined(__APPLE__)
#include <pthread.h>
#endif

namespace px {


/** BEHIND THE AUDIO: a thread doing work that can wait — a planner, its crew — asks to be run after
anything more urgent, so the audio thread is never kept waiting for it. */
inline void crewStandsBack() {
#if defined(__APPLE__)
	pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
}


struct Crew {
	/** `helpers` threads besides the one that asks. */
	explicit Crew(int helpers) {
		for (int i = 0; i < helpers; i++)
			threads.emplace_back([this]() { work(); });
	}

	~Crew() {
		{
			std::lock_guard<std::mutex> hold(lock);
			ending = true;
			round++;
		}
		wake.notify_all();
		for (std::thread& t : threads)
			t.join();
	}

	int size() const { return (int) threads.size() + 1; }

	/** f(0) to f(n - 1), each once, shared out; returns when all have run. Several threads may ask at
	once — the planners of several modules sharing one crew — and take turns. */
	void each(int n, const std::function<void(int)>& f) {
		if (n <= 0)
			return;
		std::lock_guard<std::mutex> turn(asking);
		if (threads.empty() || n == 1) {
			for (int i = 0; i < n; i++)
				f(i);
			return;
		}
		{
			std::lock_guard<std::mutex> hold(lock);
			job = &f;
			count = n;
			// The count of pieces finished first: a helper still in the last round may take a piece of
			// this one the moment `next` is reset, and its finishing must count.
			finished.store(0);
			next.store(0);
			round++;
		}
		wake.notify_all();
		take();
		std::unique_lock<std::mutex> hold(lock);
		done.wait(hold, [&]() { return finished.load() >= count; });
		job = nullptr;
	}

private:
	std::vector<std::thread> threads;
	/** Held by whoever has the crew, so that one job is shared out at a time. */
	std::mutex asking;
	std::mutex lock;
	std::condition_variable wake, done;
	const std::function<void(int)>* job = nullptr;
	int count = 0;
	uint64_t round = 0;
	bool ending = false;
	std::atomic<int> next{0}, finished{0};

	/** Pieces of the job in hand until there are none left. */
	void take() {
		for (;;) {
			const int i = next.fetch_add(1);
			if (i >= count)
				return;
			(*job)(i);
			if (finished.fetch_add(1) + 1 >= count) {
				std::lock_guard<std::mutex> hold(lock);
				done.notify_all();
			}
		}
	}

	void work() {
		crewStandsBack();
		uint64_t seen = 0;
		for (;;) {
			{
				std::unique_lock<std::mutex> hold(lock);
				wake.wait(hold, [&]() { return round != seen; });
				seen = round;
				if (ending)
					return;
				if (!job)
					continue;
			}
			take();
		}
	}
};


} // namespace px
