package main

import "fmt"

func xs_next(x uint64) uint64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x }

func main() {
	N, steps := 5000, 50
	dt, g := float32(0.0625), float32(8.0)
	x := make([]float32, N); y := make([]float32, N); vx := make([]float32, N); vy := make([]float32, N)
	r := uint64(12345)
	for i := 0; i < N; i++ { r = xs_next(r); x[i] = float32(r%1600) * 0.0625; r = xs_next(r); y[i] = float32(r%1600) * 0.0625; r = xs_next(r); vx[i] = float32(int64(r%200)-100) * 0.0625; r = xs_next(r); vy[i] = float32(int64(r%200)-100) * 0.0625 }
	for s := 0; s < steps; s++ { for i := 0; i < N; i++ { x[i] += vx[i] * dt; y[i] += vy[i] * dt; vy[i] -= g * dt; if y[i] < 0 { y[i] = -y[i]; vy[i] = -vy[i] * 0.5 }; if x[i] >= 100.0 { x[i] -= 100.0 }; if x[i] < 0 { x[i] += 100.0 } } }
	var total int64
	for i := 0; i < N; i++ { total += int64(x[i]*100) + int64(y[i]*100) + int64(vx[i]*100) + int64(vy[i]*100) }
	total %= 1000000007; if total < 0 { total += 1000000007 }
	fmt.Println(N); fmt.Println(total)
}
