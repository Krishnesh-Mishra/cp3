// os-probe (go entry) -- query platform, CPU count, home, temp and hostname
// in a tight loop, folding only machine-independent booleans.
//
// Build:  go build -o os-probe.exe main.go
// Run:    os-probe.exe
//
// Same workload, same fold and same output in every language entry. The
// platform name is mapped to 1/2/3 and reduced to a "documented platform"
// boolean; the CPU count and the three path/name strings reduce to
// non-empty/positive booleans. No path, hostname or core count is folded, so
// the checksum cannot vary between machines.
package main

import (
	"fmt"
	"os"
	"runtime"
)

const (
	iterations int64 = 50000
	mod        int64 = 1000003
)

func platformCode() int {
	switch runtime.GOOS {
	case "windows":
		return 1
	case "darwin":
		return 2
	case "linux":
		return 3
	default:
		return 0
	}
}

func main() {
	var checksum int64 = 0

	for k := int64(0); k < iterations; k++ {
		code := platformCode()
		platformOk := 0
		if code >= 1 && code <= 3 {
			platformOk = 1
		}

		cpuOk := 0
		if runtime.NumCPU() > 0 {
			cpuOk = 1
		}

		home, _ := os.UserHomeDir()
		homeOk := 0
		if home != "" {
			homeOk = 1
		}

		tempOk := 0
		if os.TempDir() != "" {
			tempOk = 1
		}

		host, _ := os.Hostname()
		hostOk := 0
		if host != "" {
			hostOk = 1
		}

		term := platformOk*16 + cpuOk*8 + homeOk*4 + tempOk*2 + hostOk
		checksum = (checksum*31 + int64(term)) % mod
	}

	fmt.Println(checksum)
}
