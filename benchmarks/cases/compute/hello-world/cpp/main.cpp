// hello-world (cpp entry) -- print one line and exit.
//
// Build:  g++ -std=c++20 -O3 -o hello-world.exe main.cpp
// Run:    hello-world.exe
//
// The suite times the whole process: spawn -> output -> exit. Same one line
// of output in every language entry.

#include <iostream>

int main() {
    std::cout << "Hello, world!\n";
    return 0;
}
