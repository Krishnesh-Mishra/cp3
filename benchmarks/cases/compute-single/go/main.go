// compute-single (go entry) -- total Collatz steps for every start value in
// 1..1,000,000, printed as one integer.
//
// Build:  go build -o compute-single.exe main.go
// Run:    compute-single.exe
//
// Same workload and same output in every language entry.

package main

import "fmt"

func collatzSteps(start int64) int64 {
	n := start
	var steps int64 = 0
	for n != 1 {
		if n%2 == 0 {
			n /= 2
		} else {
			n = 3*n + 1
		}
		steps++
	}
	return steps
}

func main() {
	const n int64 = 1000000
	var total int64 = 0
	for start := int64(1); start <= n; start++ {
		total += collatzSteps(start)
	}
	fmt.Println(total)
}
