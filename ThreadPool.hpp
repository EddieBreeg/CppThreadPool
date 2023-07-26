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
		std::unique_lock<std::mutex> lock(_mutex);
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
		std::unique_lock<std::mutex> lock(_mutex);
		if (_stopped || !_tasks_running) return;
		_cv.wait(lock, [this]() { return !this->_tasks_running; });
		// std::cout << "All tasks over\n";
	}
	template <typename Func>
	[[nodiscard]]
	std::future<T> run_task(Func &&f, Args... args) {
		std::future<T> r;
		{
			std::lock_guard<std::mutex> lock(_mutex);
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
#if __cplusplus < 201703L
	template <class Func, class _Tuple, size_t... I>
	inline decltype(auto) call(std::index_sequence<I...>, Func &&f,
							   _Tuple &&args) {
		return f(std::get<I>(std::forward<_Tuple>(args))...);
	}
	template <class Func, class _Tuple>
	decltype(auto) call(Func &&f, _Tuple &&args) {
		using indices_t =
			std::make_integer_sequence<size_t, std::tuple_size_v<_Tuple>>;
		return call(indices_t{}, std::forward<Func>(f),
					std::forward<_Tuple>(args));
	}
#endif
	template <class U, std::enable_if_t<std::is_void_v<U>, nullptr_t> = nullptr>
	void invoke_task(task &&t) {
#if __cplusplus < 201703L
		call(std::move(t._f), std::move(t._args));
#else
		std::apply(std::move(t._f), std::move(t._args));
#endif
		t._p.set_value();
	}

	template <class U,
			  std::enable_if_t<!std::is_void_v<U>, nullptr_t> = nullptr>
	void invoke_task(task &&t) {
#if __cplusplus < 201703L
		t._p.set_value(call(std::move(t._f), std::move(t._args)));
#else
		t._p.set_value(std::bind(std::move(t._f), std::move(t._args)));
#endif
	}

	void loop() {
		for (;;) {
			{
				std::unique_lock<std::mutex> lock(_mutex);
				_cv.wait(lock, [this]() {
					return this->_stopped || this->_tasks_running;
				});
				if (_stopped) return;
				if (_tasks.empty()) continue;
				// std::cout << "Picking up task\n";
				task t = std::move(_tasks.front());
				_tasks.pop();
				lock.unlock();
				invoke_task<T>(std::move(t));
			}
			std::unique_lock<std::mutex> lock(_mutex);
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
