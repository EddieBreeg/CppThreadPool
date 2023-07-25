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
#include <optional>
#include <queue>
#include <thread>
#include <tuple>
#include <vector>

/* Represents a pool of threads you can assign tasks to */
template <class Task>
class ThreadPool;

template <class T, typename... Args>
class ThreadPool<T(Args...)> {
public:
	struct NoneType {};
	using value_t = std::conditional_t<std::is_void_v<T>, NoneType, T>;

private:
	using unique_lock = std::unique_lock<std::mutex>;
	struct _result_impl {
		std::optional<value_t> _val;
		std::mutex _m;
		std::condition_variable _cv;
		bool _cancelled = false;
	};

public:
	/* Represents a task result, which you can use to retrieve the return value
	once the task is done executing
	*/
	class result {
		friend class ThreadPool;
		_result_impl *_res = new _result_impl;
		result() = default;

	public:
		result(const result &) = delete;
		result(result &&other) : _res(other._res) { other._res = nullptr; }
		/* Queries the state of the corresponding task
		@return true if the task finished executing correctly, false otherwise
		@throw std::runtime_error if the result object is invalid
		*/
		bool ready() const {
#ifndef NDEBUG
			if (!_res) throw std::runtime_error("Invalid result object");
#endif
			unique_lock lock(_res->_m);
			return !_res->_cancelled && (bool)_res->_val;
		}
		/* Waits until the task is either done, or cancelled
		@return true if the task finished and a result is available, false
		otherwise
		@throw std::runtime_error if the result object is invalid
		 */
		bool wait() {
#ifndef NDEBUG
			if (!_res) throw std::runtime_error("Invalid result object");
#endif
			unique_lock lock(_res->_m);
			if (_res->_val) return true;
			if (_res->_cancelled) return false;
			_res->_cv.wait(lock, [this]() {
				return (bool)_res->_val || _res->_cancelled;
			});
			return (bool)_res->_val;
		}
		/* Waits for the task to finish or be cancelled, and returns the result
		@return An object which contains the return value if the task finished
		succesfully, or nothing if the task was cancelled. If the underlying
		callable object had void as a return type, the result will be
		ThreadPool::NoneType
		@throw std::runtime_error if the result object is invalid
		 */
		std::optional<value_t> get() {
#ifndef NDEBUG
			if (!_res) throw std::runtime_error("Invalid result object");
#endif
			wait();
			return std::move(_res->_val);
		}
		~result() {
			if (!_res) return;
			wait();
			delete _res;
		}
	};

	/* Constructor
	@param n: The number of threads to create
	 */
	explicit ThreadPool(size_t n) {
		for (size_t i = 0; i < n; i++) {
			_threads.emplace_back(&ThreadPool::thread_loop, this);
		}
	}
	ThreadPool(ThreadPool &) = delete;
	/* Restarts the thread pool if it had previously been stopped. Otherwise,
	 * does nothing */
	void restart() {
		std::lock_guard lock(_mutex);
		if (!_stopped) return;
		_stopped = false;
		for (auto &t : _threads) {
			t = std::thread(&ThreadPool::thread_loop, this);
		}
	}
	/* Enqueues a new task
	@param f: The callable object to invoke
	@param args: The arguments to forward to f
	@return A result object you can use to wait to the task to end, and retrieve
	the result
	@warning If the return value is discarded, the current thread will block
	until the newly created task is over
	 */
	template <class F>
	[[nodiscard]]
	result run_task(F &&f, Args &&...args) {
		result r;
		std::tuple<Args &&...> A{ std::forward<Args>(args)... };
		{
			std::lock_guard lock(_mutex);
			_tasks.emplace(task{ r._res, f, std::move(A) });
			_running_task = true;
		}
		_cv.notify_one();
		return r;
	}
	/* Stops the threads.
	If cancel is set to false, the current thread will block
	until all tasks in the queue have been processed. Otherwise, the tasks
	still awaiting for execution will get cancelled. Either way, the tasks
	currently being executing will finish normally
	@param cancel: Whether to cancel the tasks still in the queue
	 */
	void stop(bool cancel = false) {
		if (_stopped) return;
		if (!cancel) {
			wait();
			{
				std::lock_guard lock(_mutex);
				_stopped = true;
			}
			_cv.notify_all();
			join_threads();
			return;
		}
		{
			std::lock_guard lock(_mutex);
			_stopped = true;
			_cv.notify_all();
			while (!_tasks.empty()) {
				task &&t = std::move(_tasks.front());
				_tasks.pop();
				{
					std::lock_guard lock(t._res->_m);
					t._res->_cancelled = true;
				}
				t._res->_cv.notify_one();
			}
		}
		join_threads();
	}
	/* Wait until all tasks in the queue have been processed */
	void wait() {
		unique_lock lock(_waitMutex);
		if (!_running_task || _stopped) return;
		_cv.wait(lock, [this]() { return !this->_running_task; });
	}
	~ThreadPool() { stop(); }

private:
	inline void join_threads() {
		for (auto &t : _threads) {
			t.join();
		}
	}
	struct task {
		_result_impl *_res;
		std::function<T(Args...)> _f;
		std::tuple<Args &&...> _args;
	};
	void thread_loop() {
		for (;;) {
			unique_lock lock(_mutex);
			_cv.wait(lock, [this]() {
				return this->_stopped || this->_running_task;
			});
			if (_stopped) return;
			if (_tasks.empty()) continue;
			task t = std::move(_tasks.front());
			_tasks.pop();
			lock.unlock();
			if constexpr (!std::is_void_v<T>) {
				auto val = std::apply(t._f, std::move(t._args));
				{
					std::lock_guard lock{ t._res->_m };
					t._res->_val = std::move(val);
				}
			} else {
				std::apply(t._f, std::move(t._args));
				std::lock_guard lock{ t._res->_m };
				t._res->_val = NoneType{};
			}
			t._res->_cv.notify_one();
			{
				std::lock_guard lock(_waitMutex);
				if (_running_task && _tasks.empty()) {
					_running_task = false;
					_cv.notify_one();
				}
			}
		}
	}
	std::vector<std::thread> _threads;
	std::mutex _mutex, _waitMutex;
	std::condition_variable _cv;
	bool _stopped = false;
	bool _running_task = false;
	std::queue<task> _tasks;
};

#endif // THREAD_POOL_H
