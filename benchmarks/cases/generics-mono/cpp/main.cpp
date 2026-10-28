// generics-mono (cpp entry) -- the same 2,000,000-iteration sum run twice: once
// through a generic template type plus a generic template function, once
// through a hand-written concrete type. Prints both sums on one line.
//
// Build:  g++ -std=c++20 -O2 -o generics-mono.exe main.cpp
// Run:    generics-mono.exe
//
// Same workload and same output in every language entry (expected line in the
// task README). For every language the intra-language ratio between the two
// loops is the signal, not the absolute time: it shows what monomorphization
// buys over the generic shape.

#include <iostream>

static const long long kIterations = 2000000;
static const long long kMod = 1000000007;

template <typename T>
struct Box {
    T value;
    explicit Box(T v) : value(v) {}
    T get() const { return value; }
};

template <typename T>
static T sumBox(const Box<T>& box) {
    return box.get();
}

struct NumberBox {
    long long value;
    explicit NumberBox(long long v) : value(v) {}
    long long get() const { return value; }
};

int main() {
    long long totalGeneric = 0;
    for (long long i = 0; i < kIterations; ++i) {
        Box<long long> box(i);
        totalGeneric = (totalGeneric + sumBox(box)) % kMod;
    }

    long long totalConcrete = 0;
    for (long long i = 0; i < kIterations; ++i) {
        NumberBox box(i);
        totalConcrete = (totalConcrete + box.get()) % kMod;
    }

    std::cout << totalGeneric << " " << totalConcrete << "\n";
    return 0;
}
