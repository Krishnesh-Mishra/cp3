// interface-dispatch (go entry) -- fill a slice with 2,000,000 shapes of three
// concrete types behind an interface, sum every Area() through the interface,
// then repeat the hot loop as direct calls on a concrete type and print both
// sums on one line.
//
// Build:  go build -o interface-dispatch.exe main.go
// Run:    interface-dispatch.exe
//
// Same workload and same output in every language entry (expected line in the
// task README). Interface method calls go through itab lookup; the concrete
// receiver in the control loop compiles to a direct call.

package main

import "fmt"

type Shape interface {
	Area() int64
}

type Circle struct {
	radius int64
}

func (c Circle) Area() int64 { return 3 * c.radius * c.radius }

type Square struct {
	side int64
}

func (s Square) Area() int64 { return s.side * s.side }

type Rect struct {
	width  int64
	height int64
}

func (r Rect) Area() int64 { return r.width * r.height }

func main() {
	const count int64 = 2000000

	shapes := make([]Shape, 0, count)
	for i := int64(0); i < count; i++ {
		switch i % 3 {
		case 0:
			shapes = append(shapes, Circle{radius: i%5 + 1})
		case 1:
			shapes = append(shapes, Square{side: i%4 + 1})
		default:
			shapes = append(shapes, Rect{width: i%3 + 1, height: i%6 + 2})
		}
	}

	var totalDispatch int64 = 0
	for _, shape := range shapes {
		totalDispatch += shape.Area()
	}

	circles := make([]Circle, 0, count)
	for i := int64(0); i < count; i++ {
		circles = append(circles, Circle{radius: i%5 + 1})
	}

	var totalControl int64 = 0
	for _, circle := range circles {
		totalControl += circle.Area() // concrete receiver: direct call
	}

	fmt.Println(totalDispatch, totalControl)
}
