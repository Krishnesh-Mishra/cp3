// env-injection (go entry) -- read one injected environment variable N times
// and fold its bytes into a checksum.
//
// Build:  go build -o env-injection.exe main.go
// Run:    env-injection.exe        (CP3_BENCH_ENV must be set)
//
// The suite runner injects CP3_BENCH_ENV into every child process for this
// task. Each iteration performs a fresh os.Getenv read (the lookup itself is
// the workload) and folds the value's length, byte sum, first byte and last
// byte into the checksum. The fixed value is never empty, so an unset variable
// cannot silently pass as a valid run.
//
// Same workload and same output as the cp3, C++, Rust, JavaScript and Python
// entries.

package main

import (
	"fmt"
	"os"
)

const (
	iterations = 500000
	key        = "CP3_BENCH_ENV"
	mod        = 1000003
)

func main() {
	checksum := int64(0)
	for i := 0; i < iterations; i++ {
		value := os.Getenv(key)
		if value == "" {
			fmt.Fprintln(os.Stderr, "CP3_BENCH_ENV is not set")
			os.Exit(1)
		}
		var bytes int64
		for _, b := range []byte(value) {
			bytes += int64(b)
		}
		checksum = (checksum*31 + int64(len(value))) % mod
		checksum = (checksum*31 + bytes) % mod
		checksum = (checksum*31 + int64(value[0])) % mod
		checksum = (checksum*31 + int64(value[len(value)-1])) % mod
	}
	fmt.Println(checksum)
}
