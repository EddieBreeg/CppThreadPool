/*
Copyright © 2023 EddieBreeg

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#if !defined(THREAD_POOL_H)
#define THREAD_POOL_H

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <optional>
#include <queue>
#include <thread>
#include <tuple>
#include <vector>

struct operation_cancelled_exception : public std::exception {
	inline const char *what() const { return "Operation was cancelled"; }
};

template <class Func>
class ThreadPool;

template <class T, typename... Args>
class ThreadPool<T(Args...)> {
	using Tuple = std::tuple<Args...>;
	struct task {
		std::function<T(Args...)> _f;
		Tuple _args;
		std::promise<T> _p;
	};

public:
	ThreadPool(size_t n) {
		for (size_t i = 0; i < n; i++) {
			_threads.emplace_back(&ThreadPool::loop, this);
		}
	}
	void stop(bool cancel = false) {
		std::unique_lock lock(_mutex);
		if (_stopped) return;
		if (cancel) {
			// empty the task queue
			while (!_tasks.empty()) {
				task &&t = std::move(_tasks.front());
				_tasks.pop();
				t._p.set_exception(
					std::make_exception_ptr(operation_cancelled_exception{}));
			}
		}
		_stopped = true;
		lock.unlock();
		_cv.notify_all();
		join();
	}
	void wait() {
		std::unique_lock lock(_mutex);
		if (_stopped || !_tasks_running) return;
		_cv.wait(lock, [this]() { return !this->_tasks_running; });
		// std::cout << "All tasks over\n";
	}
	template <typename Func>
	[[nodiscard]]
	std::future<T> run_task(Func &&f, Args... args) {
		std::future<T> r;
		{
			std::lock_guard lock(_mutex);
			if (_stopped) return r;
			std::promise<T> p;
			r = p.get_future();

			// std::cout << "Enqueuing task\n";
			Tuple A{ std::forward<Args>(args)... };
			_tasks.emplace(task{ f, std::move(A), std::move(p) });
			++_tasks_running;
		}
		_cv.notify_one();
		return r;
	}
	~ThreadPool() { stop(); }

private:
	void join() {
		for (auto &t : _threads)
			t.join();
	}
	void loop() {
		for (;;) {
			{
				std::unique_lock lock(_mutex);
				_cv.wait(lock, [this]() {
					return this->_stopped || this->_tasks_running;
				});
				if (_stopped) return;
				if (_tasks.empty()) continue;
				// std::cout << "Picking up task\n";
				task t = std::move(_tasks.front());
				_tasks.pop();
				lock.unlock();

				if constexpr (std::is_void_v<T>) {
					std::apply(t._f, t._args);
					t._p.set_value();
				} else {
					T val = std::apply(t._f, std::move(t._args));
					t._p.set_value(std::move(val));
				}
			}
			std::unique_lock lock(_mutex);
			if (!--_tasks_running) {
				lock.unlock();
				_cv.notify_one();
			}
		}
	}
	std::condition_variable _cv;
	std::mutex _mutex;
	std::vector<std::thread> _threads;
	std::queue<task> _tasks;
	bool _stopped = false;
	int _tasks_running = 0;
};
#endif // THREAD_POOL_H
