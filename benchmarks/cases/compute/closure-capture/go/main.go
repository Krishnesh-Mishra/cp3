// closure-capture (go entry) -- two closure shapes back to back over a
// 10,000,000-element source:
//   A: map / filter / reduce with closures that capture offset and cap
//   B: 1000 closures, each capturing its own key, stored in a slice and
//      invoked after the creating scope has ended (escaping closures)
// Prints both sums on one line.
//
// Build:  go build -o closure-capture.exe main.go
// Run:    closure-capture.exe
//
// Same workload and same output in every language entry (expected line in the
// task README). Go's stdlib has no map/filter helpers, so phase A invokes the
// closures directly; every closure escapes to the heap either way, and that
// capture cost is what the task measures.

package main

import "fmt"

func main() {
	const source int64 = 10000000
	const offset int64 = 7
	const cap int64 = 3

	apply := func(x int64) int64 { return x + offset }
	keep := func(x int64) bool { return x%cap == 0 }
	add := func(acc int64, x int64) int64 { return acc + x }

	var totalMapReduce int64 = 0
	for x := int64(0); x < source; x++ {
		if value := apply(x); keep(value) {
			totalMapReduce = add(totalMapReduce, value)
		}
	}

	closures := make([]func(int64) int64, 0, 1000)
	for key := int64(0); key < 1000; key++ {
		key := key
		closures = append(closures, func(x int64) int64 { return x*key + offset })
	}

	var totalEscaping int64 = 0
	for index := 0; index < len(closures); index++ {
		for call := 0; call < 3000; call++ {
			totalEscaping += closures[index](int64(index))
		}
	}

	fmt.Println(totalMapReduce, totalEscaping)
}
