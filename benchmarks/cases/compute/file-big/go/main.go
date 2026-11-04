// file-big (go entry) -- write an 8 MiB deterministic payload to the OS temp
// directory, read it back, and fold length, byte sum, first byte and last byte
// for 8 iterations.
//
// Build:  go build -o file-big.exe main.go
// Run:    file-big.exe
//
// Same payload, same fold and same output in every language entry. Expected
// output: 779913. The payload is built once before the loop; each iteration
// pays the write, the read and the fold. The temp file is removed at the end.

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const (
	size       = 8388608
	iterations = 8
	mod        = 1000003
)

const seed = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

func main() {
	payload := []byte(strings.Repeat(seed, size/len(seed)))

	path := filepath.Join(os.TempDir(), "cp3-bench-file-big-go.bin")
	defer os.Remove(path)

	var checksum int64
	for iter := 0; iter < iterations; iter++ {
		if err := os.WriteFile(path, payload, 0o644); err != nil {
			panic(err)
		}
		data, err := os.ReadFile(path)
		if err != nil {
			panic(err)
		}

		var byteSum int64
		for _, b := range data {
			byteSum += int64(b)
		}

		checksum = (checksum*31 + int64(len(data))) % mod
		checksum = (checksum*31 + byteSum) % mod
		checksum = (checksum*31 + int64(data[0])) % mod
		checksum = (checksum*31 + int64(data[len(data)-1])) % mod
	}

	fmt.Println(checksum)
}
