package main

import "fmt"

func xs_next(x uint64) uint64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x }

type E struct { kind int; id int64; a, b int64; w0 byte; wlen int64 }

func main() {
	log := make([]E, 0, 100000)
	r := uint64(12345)
	for i := 0; i < 100000; i++ {
		r = xs_next(r); k := r % 8; id := int64((r >> 3) % 100000)
		e := E{id: id}
		if k < 4 { e.kind = 1; e.a = int64((r>>20)%201) - 100; e.b = int64((r>>40)%201) - 100 } else if k < 6 { e.kind = 2; e.wlen = int64(4 + (r % 12)); w := xs_next(r); e.w0 = byte(w%26) + 'a' } else if k < 7 { e.kind = 3; e.a = int64((r >> 20) % 256) } else { e.kind = 0 }
		log = append(log, e)
	}
	var total int64
	for p := 0; p < 4; p++ { for _, e := range log { if e.kind == 0 { total += 1 } else if e.kind == 1 { total += e.id + e.a + e.b } else if e.kind == 2 { total += e.id + e.wlen + int64(e.w0) } else { total += e.id + e.a } } }
	fmt.Println(len(log)); fmt.Println(total)
}
