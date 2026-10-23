// typed-array-sum (go entry) -- fill a fixed Float64Array and Uint8Array,
// sum the floats for 32 rounds, then fold the float and byte totals into one
// integer checksum.
//
// Build:  go build -o typed-array-sum.exe main.go
// Run:    typed-array-sum.exe
//
// Same workload and same output in every language entry. The float pattern is
// a multiple of 0.25 and every partial sum stays far below 2^53, so the
// addition is exact in IEEE-754 doubles.

package main

import "fmt"

const (
	size   = 1000000
	rounds = 32
	mod    = int64(1000000007)
)

func main() {
	values := make([]float64, size)
	bytes := make([]byte, size)
	for i := 0; i < size; i++ {
		values[i] = float64(i%97)*0.5 + float64(i%13)*0.25
		bytes[i] = byte((i*31 + 7) % 256)
	}

	total := 0.0
	for round := 0; round < rounds; round++ {
		acc := 0.0
		for _, value := range values {
			acc += value
		}
		total += acc
	}

	var byteTotal int64
	for _, value := range bytes {
		byteTotal += int64(value)
	}

	scaled := int64(total * 4.0)
	checksum := (scaled + byteTotal) % mod
	checksum = (checksum*31 + size + rounds) % mod
	fmt.Println(checksum)
}
