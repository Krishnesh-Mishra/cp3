// sha256-churn (go entry) -- hash one fixed 1 KiB buffer ITERATIONS times and
// fold the first four digest bytes into a checksum.
//
// Build:  go build -o sha256-churn.exe main.go
// Run:    sha256-churn.exe
//
// Same workload and same output as the cp3, JavaScript and Python entries; the
// C++ and Rust entries are omitted because neither standard library ships
// SHA-256 (see TASK_OMIT_LANGUAGES in runner/run_suite.py).
//
// The buffer is the seed repeated to exactly 1024 bytes and never changes, so
// every hash is over identical bytes; what the task measures is the hash
// implementation plus the per-call boundary. sha256.Sum256 returns the digest
// as 32 bytes; the first four are read as one big-endian 32-bit integer.

package main

import (
	"crypto/sha256"
	"encoding/binary"
	"fmt"
)

const (
	bufferSize = 1024
	iterations = 60000
	seed       = "0123456789abcdef"
)

func main() {
	data := make([]byte, bufferSize)
	for i := range data {
		data[i] = seed[i%len(seed)]
	}

	checksum := int64(0)
	for k := 0; k < iterations; k++ {
		sum := sha256.Sum256(data)
		firstFourBytes := binary.BigEndian.Uint32(sum[:4])
		checksum = (checksum*31 + int64(firstFourBytes)) % 1000003
	}
	fmt.Println(checksum)
}
