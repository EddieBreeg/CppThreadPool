#include <ThreadPool.hpp>
#include <iostream>

class Foo {
	int _x;

public:
	Foo() = delete;
	Foo(int x) : _x(x) {}
	Foo(const Foo &) = delete;
	Foo(Foo &&other) : _x(other._x) {}
	operator int() const { return _x; }
};

Foo f(Foo x) {
	std::this_thread::sleep_for(std::chrono::milliseconds(int(x)));
	std::cout << (int)x << '\n';
	return x;
}
void g(int x) {
	std::this_thread::sleep_for(std::chrono::milliseconds(x));
	std::cout << x << '\n';
}

int main(int argc, char const *argv[]) {
	ThreadPool<Foo(Foo)> tp(2);
	for (int x : { 500, 1000, 1500 }) {
		tp.run_task(f, Foo{ x });
	}

	tp.wait();
	std::cout << "Finished first batch\n";

	ThreadPool<void(int)> tp2(3);
	for (int x : { 500, 1000, 1500 }) {
		tp2.run_task(g, x);
	}
	tp2.wait();
	return 0;
}
