// map-churn (go entry) -- fill a map[int64]int64 with 100000 keys, then run
// two rounds of delete every 10th key, reinsert it, update every 7th key and
// fold the whole map in ascending key order.
//
// Build:  go build -o map-churn.exe main.go
// Run:    map-churn.exe
//
// Same workload and same output in every language entry. Expected output:
// 942471769. The fold walks numeric key order, so map iteration order cannot
// affect the result.

package main

import "fmt"

const (
	keys   = 100000
	rounds = 2
	mod    = 1000000007
)

func main() {
	m := make(map[int64]int64, keys)
	for i := int64(0); i < keys; i++ {
		m[i] = i*3 + 1
	}

	var checksum int64
	for r := int64(0); r < rounds; r++ {
		for i := int64(0); i < keys; i += 10 {
			delete(m, i)
		}
		for i := int64(0); i < keys; i += 10 {
			m[i] = i*5 + r + 2
		}
		for i := int64(0); i < keys; i += 7 {
			m[i] = m[i] + 1
		}
		for i := int64(0); i < keys; i++ {
			checksum = (checksum*31 + m[i]) % mod
		}
	}

	fmt.Println(checksum)
}
