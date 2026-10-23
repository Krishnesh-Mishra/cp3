// math-kernel (go entry) -- evaluate sqrt, floor, abs, pow and clamp over
// 5,000,000 points, quantize each result to an integer and fold one checksum.
//
// Build:  go build -o math-kernel.exe main.go
// Run:    math-kernel.exe
//
// Same workload and same output in every language entry. sqrt is correctly
// rounded, Pow uses an integer exponent, and every term is quantized to an
// integer before folding, so the checksum is exact across libms.

package main

import (
	"fmt"
	"math"
)

const (
	count = 5000000
	mod   = int64(1000000007)
)

func main() {
	var checksum int64
	sqrtTotal := 0.0
	for i := 1; i <= count; i++ {
		x := float64(i) * 0.000001
		s := math.Sqrt(float64(i))
		sqrtTotal += s
		f := math.Floor(x + 0.5)
		a := math.Abs(x - 2.5)
		p := int64(math.Pow(float64(i), 2))
		c := math.Min(math.Max(x*1000.0, 0.0), 1000.0)
		mixed := s + f + a + c
		quantum := int64(mixed * 1000000.0)
		checksum = (checksum*31 + quantum + p) % mod
	}
	sqrtScaled := int64(sqrtTotal * 1000.0)
	checksum = (checksum*31 + sqrtScaled) % mod
	fmt.Println(checksum)
}
