// date-format (go entry) -- construct UTC dates from one fixed epoch
// millisecond value, format the components manually, and fold a fixed diff.
//
// Build:  go build -o date-format.exe main.go
// Run:    date-format.exe
//
// Same workload, same fold and same output in every runnable language entry.
// The formatted spelling is built by hand, so every language emits the same 19
// ASCII characters: YYYY-MM-DDTHH:MM:SS.
package main

import (
	"fmt"
	"time"
)

const (
	baseMS      int64 = 1700000000000 // 2023-11-14T22:13:20Z
	laterMS     int64 = 1700000123456 // +123,456 ms
	iterations  int64 = 50000
	mod         int64 = 1000003
	hourSeconds int64 = 3600
)

// '0'..'9' -> 0..9, '-' -> 10, 'T' -> 11, ':' -> 12. The same weights are used
// by every entry, so the character fold cannot drift between languages.
func charValue(ch byte) int64 {
	if ch >= '0' && ch <= '9' {
		return int64(ch - '0')
	}
	if ch == '-' {
		return 10
	}
	if ch == 'T' {
		return 11
	}
	return 12
}

func main() {
	diffMS := laterMS - baseMS
	var checksum int64 = 0

	for k := int64(0); k < iterations; k++ {
		instant := time.UnixMilli(baseMS + (k%hourSeconds)*1000).UTC()
		text := fmt.Sprintf("%04d-%02d-%02dT%02d:%02d:%02d",
			instant.Year(), int(instant.Month()), instant.Day(),
			instant.Hour(), instant.Minute(), instant.Second())

		var charSum int64 = 0
		for i := 0; i < len(text); i++ {
			charSum += charValue(text[i])
		}

		checksum = (checksum*31 + charSum + int64(len(text)) + diffMS) % mod
	}

	fmt.Println(checksum)
}
