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

template <class Task> class ThreadPool;

template <class T, typename... Args> class ThreadPool<T(Args...)> {
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
	class result {
		friend class ThreadPool;
		_result_impl *_res = new _result_impl;

	public:
		result() = default;
		result(const result &) = delete;
		result(result &&other) : _res(other._res) { other._res = nullptr; }
		bool ready() const {
			unique_lock lock(_res->_m);
			return !_res->_cancelled && (bool)_res->_val;
		}
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

	explicit ThreadPool(size_t n) {
		for (size_t i = 0; i < n; i++) {
			_threads.emplace_back(&ThreadPool::thread_loop, this);
		}
	}
	ThreadPool(ThreadPool &) = delete;
	void restart() {
		unique_lock lock(_mutex);
		if (!_stopped) return;
		_stopped = false;
		for (auto &t : _threads) {
			t = std::thread(&ThreadPool::thread_loop, this);
		}
	}
	template <class F>
	[[nodiscard]]
	result run_task(F &&f, Args &&...args) {
		result r;
		std::tuple<Args &&...> A{ std::forward<Args>(args)... };
		unique_lock lock(_mutex);
		_tasks.emplace(task{ r._res, f, std::move(A) });
		_running_task = true;
		lock.unlock();
		_cv.notify_one();
		return r;
	}
	void stop(bool cancel = false) {
		if (_stopped) return;
		if (!cancel) {
			wait();
			{
				unique_lock lock(_mutex);
				_stopped = true;
				_cv.notify_all();
			}
			join_threads();
			return;
		}
		unique_lock lock(_mutex);
		_stopped = true;
		_cv.notify_all();
		while (!_tasks.empty()) {
			task &&t = std::move(_tasks.front());
			_tasks.pop();
			unique_lock lock(t._res->_m);
			t._res->_cancelled = true;
			lock.unlock();
			t._res->_cv.notify_one();
		}
		lock.unlock();
		join_threads();
	}
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
					unique_lock lock{ t._res->_m };
					t._res->_val = std::move(val);
				}
			} else {
				std::apply(t._f, std::move(t._args));
				unique_lock lock{ t._res->_m };
				t._res->_val = NoneType{};
			}
			t._res->_cv.notify_one();
			{
				unique_lock lock(_waitMutex);
				if (_running_task && _tasks.empty()) {
					_running_task = false;
					lock.unlock();
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
