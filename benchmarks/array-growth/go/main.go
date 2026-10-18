// array-growth (go entry) -- push 20,000,000 numbers into a growing slice
// while summing them, pop half of them back off, then print the sum.
//
// Build:  go build -o array-growth.exe main.go
// Run:    array-growth.exe
//
// Same workload and same output in every language entry (expected value in the
// task README). The slice starts empty so append doubling is part of the
// workload; popping is reslicing, Go's idiomatic way to shrink.

package main

import "fmt"

func main() {
	const pushes int64 = 20000000
	values := make([]int64, 0)
	var total int64 = 0
	for i := int64(1); i <= pushes; i++ {
		values = append(values, i)
		total += i
	}
	for k := int64(0); k < pushes/2; k++ {
		values = values[:len(values)-1]
	}
	fmt.Println(total)
}
