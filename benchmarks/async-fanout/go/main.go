// async-fanout (go entry) -- two phases folded into one integer.
//
// Build: go build -o async-fanout.exe main.go
// Run:   ./async-fanout.exe
//
// Phase 1: 10,000 goroutines, all started in one loop and joined at once; each
//          sends its index and the resolved values are summed (a fan-in).
// Phase 2: 10,000 sequential receives from a channel that was filled before
//          the loop, folding the received value at every step. This is Go's
//          "await an already-resolved value": the receive never parks the
//          goroutine, so it measures communication, not the scheduler.
//
// The output is identical to the cp3, JS and Python entries.
package main

import (
	"fmt"
	"sync"
)

const (
	fanout = 10000
	chain  = 10000
	mod    = 1000003
)

func main() {
	// Phase 1: 10,000 goroutines, buffered so none of them blocks.
	results := make(chan int, fanout)
	var group sync.WaitGroup
	group.Add(fanout)
	for i := 0; i < fanout; i++ {
		go func(n int) {
			defer group.Done()
			results <- n
		}(i)
	}
	group.Wait()
	close(results)

	fanSum := 0
	for value := range results {
		fanSum += value
	}
	checksum := fanSum % mod

	// Phase 2: ready values, one receive per await-equivalent step.
	ready := make(chan int, chain)
	for i := 0; i < chain; i++ {
		ready <- i
	}
	for i := 0; i < chain; i++ {
		value := <-ready
		checksum = (checksum*31 + value) % mod
	}

	fmt.Println(checksum)
}
