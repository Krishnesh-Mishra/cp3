// interface-dispatch (cpp entry) -- fill an array with 2,000,000 shapes of
// three concrete types behind a virtual base, sum every area() through the
// base pointer, then repeat the hot loop as direct calls on a concrete type
// and print both sums on one line.
//
// Build:  g++ -std=c++20 -O3 -o interface-dispatch.exe main.cpp
// Run:    interface-dispatch.exe
//
// Same workload and same output in every language entry (expected line in the
// task README). The gap between the two sums' timings is the virtual dispatch
// cost; the control loop is why the task prints both.

#include <iostream>
#include <memory>
#include <vector>

static const long long kCount = 2000000;

struct Shape {
    virtual ~Shape() = default;
    virtual long long area() const = 0;
};

struct Circle : Shape {
    long long radius;
    explicit Circle(long long r) : radius(r) {}
    long long area() const override { return 3 * radius * radius; }
};

struct Square : Shape {
    long long side;
    explicit Square(long long s) : side(s) {}
    long long area() const override { return side * side; }
};

struct Rect : Shape {
    long long width;
    long long height;
    Rect(long long w, long long h) : width(w), height(h) {}
    long long area() const override { return width * height; }
};

int main() {
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.reserve((size_t)kCount);
    for (long long i = 0; i < kCount; ++i) {
        if (i % 3 == 0) {
            shapes.push_back(std::make_unique<Circle>(i % 5 + 1));
        } else if (i % 3 == 1) {
            shapes.push_back(std::make_unique<Square>(i % 4 + 1));
        } else {
            shapes.push_back(std::make_unique<Rect>(i % 3 + 1, i % 6 + 2));
        }
    }

    long long totalDispatch = 0;
    for (const auto& shape : shapes) {
        totalDispatch += shape->area();
    }

    std::vector<Circle> circles;
    circles.reserve((size_t)kCount);
    for (long long i = 0; i < kCount; ++i) {
        circles.emplace_back(i % 5 + 1);
    }

    long long totalControl = 0;
    for (const Circle& circle : circles) {
        totalControl += circle.area();  // non-virtual: the type is known here
    }

    std::cout << totalDispatch << " " << totalControl << "\n";
    return 0;
}
