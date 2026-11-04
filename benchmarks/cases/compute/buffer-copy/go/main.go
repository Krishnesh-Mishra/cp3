// buffer-copy (go entry) -- build a 64 KiB byte-pattern buffer, then reshape
// it ITERATIONS times: rotate it by a changing offset, overwrite the new first
// byte with a cycling marker, and fold a 96-byte window into a checksum.
//
// Build:  go build -o buffer-copy.exe main.go
// Run:    buffer-copy.exe
//
// The other five entries run the same reshape with their own buffer types
// (cp3 Buffer, std::vector<uint8_t>, Vec<u8>, Node Buffer, bytearray). Every
// intermediate buffer is exactly SIZE bytes, and every entry folds the same two
// marker positions, so all six languages must print the same number.

package main

import (
	"bytes"
	"fmt"
)

const (
	size       = 65536
	iterations = 5000
	window     = 96
	seed       = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!?"
)

func main() {
	buf := bytes.Repeat([]byte(seed), size/len(seed))

	checksum := int64(0)
	for k := 0; k < iterations; k++ {
		offset := (k*4093 + 17) % size
		marker := seed[k%len(seed)]

		// Rotate left by offset and overwrite the new first byte with marker.
		next := make([]byte, 0, size)
		next = append(next, marker)
		next = append(next, buf[offset+1:]...)
		next = append(next, buf[:offset]...)
		buf = next

		firstA := bytes.IndexByte(buf[:window], 'a')
		first7 := bytes.IndexByte(buf[:window], '7')
		checksum = (checksum*131 + int64(len(buf)) + int64(firstA+1) + int64(first7+1)) % 1000003
	}
	fmt.Println(checksum)
}
