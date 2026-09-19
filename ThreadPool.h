#pragma once
#include <thread>
#include <future>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <windows.h>

class ThreadPool {
public:
	explicit ThreadPool(size_t threads) : stop(false) {
		unsigned int hw_cores = std::thread::hardware_concurrency();
		if (hw_cores == 0) hw_cores = 4;
		for (size_t i = 0; i < threads; ++i) {
			workers.emplace_back([this] {
				for (;;) {
					std::function<void()> task;
					{
						std::unique_lock<std::mutex> lock(this->queue_mutex);
						this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
						if (this->stop && this->tasks.empty()) return;
						task = std::move(this->tasks.front());
						this->tasks.pop();
					}
					task();
				}
			});
			HANDLE thread_handle = (HANDLE)workers.back().native_handle();
			thread_handles.push_back(thread_handle);
			if (i < hw_cores) SetThreadAffinityMask(thread_handle, 1ULL << i);
			SetThreadPriority(thread_handle, THREAD_PRIORITY_HIGHEST);
		}
	}

	template<class F>
	auto enqueue(F&& f) -> std::future<decltype(f())> {
		using R = decltype(f());
		auto task_ptr = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
		std::future<R> res = task_ptr->get_future();
		{
			std::unique_lock<std::mutex> lock(queue_mutex);
			if (stop) throw std::runtime_error("Enqueue on stopped ThreadPool");
			tasks.emplace([task_ptr]() { (*task_ptr)(); });
		}
		condition.notify_one();
		return res;
	}

	void set_pool_priority(int priority_level) {
		std::unique_lock<std::mutex> lock(queue_mutex);
		for (HANDLE h : thread_handles) SetThreadPriority(h, priority_level);
	}

	~ThreadPool() {
		{ std::unique_lock<std::mutex> lock(queue_mutex); stop = true; }
		condition.notify_all();
		for (std::thread &worker : workers) worker.join();
	}

private:
	std::vector<std::thread> workers;
	std::vector<HANDLE> thread_handles;
	std::queue<std::function<void()>> tasks;
	std::mutex queue_mutex;
	std::condition_variable condition;
	bool stop;
};