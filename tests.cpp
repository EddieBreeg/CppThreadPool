#include <ThreadPool.hpp>
#include <iostream>

void f(int n) {
	std::this_thread::sleep_for(std::chrono::milliseconds(n));
	std::cout << n << '\n';
}

int main(int argc, char const *argv[]) {
	ThreadPool<decltype(f)> tp(2);
	auto res1 = tp.run_task(f, 1000);
	auto res2 = tp.run_task(f, 2000);
	auto res3 = tp.run_task(f, 3000);
	return 0;
}
