// compute-multi (go entry) -- the compute-single workload split across
// runtime.NumCPU() goroutines, each summing a contiguous chunk of
// 1..1,000,000; the partials are combined on the main goroutine.
//
// Build:  go build -o compute-multi.exe main.go
// Run:    compute-multi.exe
//
// Prints the same single integer as every other entry (single or multi), so
// the outputs cross-check each other.

package main

import (
	"fmt"
	"runtime"
	"sync"
)

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

func sumRange(first int64, last int64) int64 {
	var total int64 = 0
	for start := first; start <= last; start++ {
		total += collatzSteps(start)
	}
	return total
}

func main() {
	const n int64 = 1000000
	workers := runtime.NumCPU()
	chunk := (n + int64(workers) - 1) / int64(workers)

	partial := make([]int64, workers)
	var waitGroup sync.WaitGroup
	for w := 0; w < workers; w++ {
		first := int64(w)*chunk + 1
		last := int64(w+1) * chunk
		if last > n {
			last = n
		}
		waitGroup.Add(1)
		go func(index int, first int64, last int64) {
			defer waitGroup.Done()
			partial[index] = sumRange(first, last)
		}(w, first, last)
	}
	waitGroup.Wait()

	var total int64 = 0
	for _, value := range partial {
		total += value
	}
	fmt.Println(total)
}
