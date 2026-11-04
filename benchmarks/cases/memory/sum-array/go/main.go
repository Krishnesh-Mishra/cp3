package main

import "fmt"

func xs_next(x uint64) uint64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x }

func main() {
	const N = 4000000
	const PASSES = 8
	data := make([]int32, N)
	r := uint64(12345)
	for i := 0; i < N; i++ { r = xs_next(r); data[i] = int32(r % 1000) }
	var total int64
	for p := 0; p < PASSES; p++ {
		for _, x := range data { total += int64(x ^ int32(p)) }
	}
	fmt.Println(N)
	fmt.Println(total)
}
