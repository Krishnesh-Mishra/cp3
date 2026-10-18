// object-churn (go entry) -- create 5,000,000 small heap objects in a hot
// loop: each iteration's temporary dies in scope, and every second object
// escapes into a slice. One checksum at the end.
//
// Build:  go build -o object-churn.exe main.go
// Run:    object-churn.exe
//
// Same workload and same output in every language entry (expected value in the
// task README). Go's escape analysis may stack-allocate the temporary; the
// kept half has to be heap-allocated and becomes GC pressure, which is the
// honest split for this runtime.

package main

import "fmt"

type Cell struct {
	value int64
}

func main() {
	const objects int64 = 5000000
	const mod int64 = 1000000007

	kept := make([]*Cell, 0, objects/2)
	var acc int64 = 0
	for i := int64(0); i < objects; i++ {
		temp := &Cell{value: i}
		acc = (acc + temp.value) % mod
		if i%2 == 0 {
			kept = append(kept, &Cell{value: i})
		}
	}

	checksum := acc
	for _, cell := range kept {
		checksum = (checksum * 31 + cell.value) % mod
	}
	fmt.Println(checksum)
}
