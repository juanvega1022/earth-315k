// A small pool of worker threads for splitting a loop across CPU cores.
// Results never depend on how the work is split, so runs stay the same for a given seed.
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace earth
{

class WorkerPool
{
public:
	// Threads = 0 picks one less than the number of cores (the calling thread works too).
	explicit WorkerPool(int Threads = 0)
	{
		if (Threads <= 0)
		{
			Threads = std::max(0, static_cast<int>(std::thread::hardware_concurrency()) - 1);
		}
		for (int i = 0; i < Threads; ++i)
		{
			Workers.emplace_back([this] { WorkLoop(); });
		}
	}

	~WorkerPool()
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Stopping = true;
		}
		Wake.notify_all();
		for (std::thread& T : Workers)
		{
			T.join();
		}
	}

	WorkerPool(const WorkerPool&) = delete;
	WorkerPool& operator=(const WorkerPool&) = delete;

	int ThreadCount() const { return static_cast<int>(Workers.size()) + 1; }

	// Calls Body(i) for every i in [0, Count), spread over the pool and the calling thread.
	void For(size_t Count, const std::function<void(size_t)>& Body)
	{
		if (Workers.empty() || Count < 64)
		{
			for (size_t i = 0; i < Count; ++i)
			{
				Body(i);
			}
			return;
		}
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Job = &Body;
			JobCount = Count;
			Next.store(0);
			Busy = Workers.size();
			++Generation;
		}
		Wake.notify_all();
		RunChunks();
		std::unique_lock<std::mutex> Lock(Mutex);
		Done.wait(Lock, [this] { return Busy == 0; });
		Job = nullptr;
	}

private:
	static constexpr size_t kChunk = 32;

	void RunChunks()
	{
		for (;;)
		{
			const size_t Start = Next.fetch_add(kChunk);
			if (Start >= JobCount)
			{
				return;
			}
			const size_t End = std::min(JobCount, Start + kChunk);
			for (size_t i = Start; i < End; ++i)
			{
				(*Job)(i);
			}
		}
	}

	void WorkLoop()
	{
		size_t Seen = 0;
		for (;;)
		{
			{
				std::unique_lock<std::mutex> Lock(Mutex);
				Wake.wait(Lock, [&] { return Stopping || Generation != Seen; });
				if (Stopping)
				{
					return;
				}
				Seen = Generation;
			}
			RunChunks();
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				--Busy;
			}
			Done.notify_one();
		}
	}

	std::vector<std::thread> Workers;
	std::mutex Mutex;
	std::condition_variable Wake;
	std::condition_variable Done;
	const std::function<void(size_t)>* Job = nullptr;
	size_t JobCount = 0;
	std::atomic<size_t> Next{0};
	size_t Busy = 0;
	size_t Generation = 0;
	bool Stopping = false;
};

} // namespace earth
