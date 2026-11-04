// deep-recursion (go entry) -- 200 rounds of a fresh recursive sumTo(5000)
// call, each result folded into one checksum.
//
// Build:  go build -o deep-recursion.exe main.go
// Run:    deep-recursion.exe
//
// Same workload and same output in every language entry. Expected output:
// 292249866. sumTo is plain recursion: the addition happens after the
// recursive call, and there is no memoization, precomputed value or loop
// replacement. Go grows goroutine stacks as needed.

package main

import "fmt"

const (
	depth  = 5000
	rounds = 200
	mod    = 1000000007
)

func sumTo(n int64) int64 {
	if n == 0 {
		return 0
	}
	return n + sumTo(n-1)
}

func main() {
	var checksum int64
	for r := 0; r < rounds; r++ {
		checksum = (checksum*31 + sumTo(depth)) % mod
	}

	fmt.Println(checksum)
}
