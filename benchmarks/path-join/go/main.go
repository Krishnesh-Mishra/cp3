// path-join (go entry) -- join, normalize, basename and extname one fixed
// path-segment list N times and fold stable string lengths and characters.
//
// Build:  go build -o path-join.exe main.go
// Run:    path-join.exe
//
// Same workload and same output as the cp3, C++, Rust, JavaScript and Python
// entries. Only lengths and the first character of each result are folded, so
// the checksum is identical on Windows and POSIX (the separator is one
// character in every language's path surface).

package main

import (
	"fmt"
	"path/filepath"
)

const (
	iterations = 170000
	mod        = 1000003
)

var parts = []string{"var", "log", "cp3", "service", "events-2026-10-23.log"}

func main() {
	var checksum int64
	for i := 0; i < iterations; i++ {
		joined := filepath.Join(parts...)
		normalized := filepath.Clean(joined)
		base := filepath.Base(joined)
		ext := filepath.Ext(joined)

		checksum = (checksum*31 + int64(len(joined))) % mod
		checksum = (checksum*31 + int64(len(normalized))) % mod
		checksum = (checksum*31 + int64(len(base))) % mod
		checksum = (checksum*31 + int64(len(ext))) % mod
		checksum = (checksum*31 + int64(joined[0])) % mod
		checksum = (checksum*31 + int64(base[0])) % mod
		checksum = (checksum*31 + int64(ext[0])) % mod
	}
	fmt.Println(checksum)
}
