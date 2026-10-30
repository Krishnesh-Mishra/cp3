// regex-scan (go entry) -- run the global pattern [0-9]+ over a 240000-byte
// generated ASCII text 8 times, counting the matches and summing their values.
//
// Build:  go build -o regex-scan.exe main.go
// Run:    regex-scan.exe
//
// Same text, same pattern and same output in every language entry. Expected
// output: 735703090. The full scan is repeated every round; matches are never
// hoisted out of the loop.

package main

import (
	"fmt"
	"regexp"
	"strings"
)

const (
	repeats = 5000
	rounds  = 8
	mod     = 1000000007
)

func main() {
	const unit = "alpha 123 beta 45 gamma 6789 delta 0 epsilon 42\n"

	text := strings.Repeat(unit, repeats)
	digits := regexp.MustCompile(`[0-9]+`)

	var checksum int64
	for r := 0; r < rounds; r++ {
		matches := digits.FindAllString(text, -1)

		var count, sum int64
		for _, match := range matches {
			count++
			var value int64
			for i := 0; i < len(match); i++ {
				value = value*10 + int64(match[i]-'0')
			}
			sum += value
		}

		checksum = (checksum*31 + count + sum) % mod
	}

	fmt.Println(checksum)
}
