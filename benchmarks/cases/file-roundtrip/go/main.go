// file-roundtrip (go entry) -- write, read and stat one fixed 4 KiB file N
// times in the OS temp directory, then remove it.
//
// Build:  go build -o file-roundtrip.exe main.go
// Run:    file-roundtrip.exe
//
// Same workload and same output as the cp3, C++, Rust, JavaScript and Python
// entries. The checksum folds the bytes read (length, byte sum, first and last
// byte) and the stat result (size, isFile, isDirectory), so a language that
// wrote a different file or mis-stated the metadata would print a different
// number.

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const (
	iterations = 200
	fileBytes  = 4096
	mod        = 1000003
)

func payload() string {
	const seed = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
	var data strings.Builder
	for data.Len() < fileBytes {
		data.WriteString(seed)
	}
	return data.String()[:fileBytes]
}

func main() {
	data := payload()
	file := filepath.Join(os.TempDir(), "cp3-bench-file-roundtrip.txt")

	var checksum int64
	for i := 0; i < iterations; i++ {
		if err := os.WriteFile(file, []byte(data), 0o644); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		read, err := os.ReadFile(file)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		if len(read) != fileBytes {
			fmt.Fprintln(os.Stderr, "wrong file size")
			os.Exit(1)
		}
		info, err := os.Stat(file)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}

		var bytes int64
		for _, b := range read {
			bytes += int64(b)
		}
		checksum = (checksum*31 + int64(len(read))) % mod
		checksum = (checksum*31 + bytes) % mod
		checksum = (checksum*31 + int64(read[0])) % mod
		checksum = (checksum*31 + int64(read[len(read)-1])) % mod
		checksum = (checksum*31 + info.Size()) % mod
		if info.Mode().IsRegular() {
			checksum = (checksum*31 + 1) % mod
		} else {
			checksum = (checksum * 31) % mod
		}
		if info.IsDir() {
			checksum = (checksum*31 + 1) % mod
		} else {
			checksum = (checksum * 31) % mod
		}
	}

	if err := os.Remove(file); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Println(checksum)
}
