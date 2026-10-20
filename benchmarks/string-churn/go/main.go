// string-churn (go entry) -- build a 4 KiB string, then repeatedly rebuild
// strings from it: uppercase, slice, lowercase + re-concatenate, re-slice.
//
// Build:  go build -o string-churn.exe main.go
// Run:    string-churn.exe [iterations]        (default 50000)
//
// Same workload and same output in every language entry. Every round allocates
// new strings, which is what makes this task exercise allocators and GCs. The
// per-iteration fold (checksum * 31 + first 'c' position + length) depends on
// the exact string contents, so all six languages must print the same number.
//
// Fairness note: the alphabet is pinned ASCII, so the case helpers convert
// bytes directly instead of using strings.ToUpper/ToLower, whose generic path
// writes through a strings.Builder per changed rune and is ~20x slower on this
// toolchain. That would slow the Go entry for a reason that has nothing to do
// with the task; same output, same workload, no sandbagging.

package main

import (
	"fmt"
	"os"
	"strconv"
	"strings"
)

const defaultIterations int64 = 50000

func toUpperASCII(s string) string {
	out := []byte(s)
	for i := range out {
		if 'a' <= out[i] && out[i] <= 'z' {
			out[i] -= 'a' - 'A'
		}
	}
	return string(out)
}

func toLowerASCII(s string) string {
	out := []byte(s)
	for i := range out {
		if 'A' <= out[i] && out[i] <= 'Z' {
			out[i] += 'a' - 'A'
		}
	}
	return string(out)
}

func main() {
	iterations := defaultIterations
	if len(os.Args) > 1 {
		if parsed, err := strconv.ParseInt(os.Args[1], 10, 64); err == nil {
			iterations = parsed
		}
	}

	const seed = "abcXYZ0123456789"
	var built strings.Builder
	for built.Len() < 4096 {
		built.WriteString(seed)
	}
	data := built.String()

	var checksum int64 = 0
	for k := int64(0); k < iterations; k++ {
		s := data
		for round := 0; round < 8; round++ {
			upper := toUpperASCII(s)
			// Owned slices: this row measures string churn, so every entry pays
			// the same allocation for the intermediate copies (Go substring
			// headers would otherwise share the backing array for free).
			sliced := strings.Clone(upper[3 : len(upper)-1])
			rebuilt := toLowerASCII(sliced) + sliced[:8]
			s = strings.Clone(rebuilt[2:])
		}
		checksum = (checksum*31 + int64(strings.Index(s, "c")) + int64(len(s))) % 1000003
	}
	fmt.Println(checksum)
}
