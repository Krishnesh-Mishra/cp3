// utf8-text (go entry) -- repeat one 102-byte multilingual unit 4096 times
// (417792 bytes), then manually decode the UTF-8 bytes 32 times and fold the
// per-round counters into one integer.
//
// Build:  go build -o utf8-text.exe main.go
// Run:    utf8-text.exe
//
// Same byte sequence, same manual decode and same output in every language
// entry. Expected output: 78156718. No library UTF-8 decoder is used in the
// timed loop.

package main

import (
	"fmt"
	"strings"
)

const unit = `ASCII-0123456789|日本語テキスト|emoji-😀🚀|café naïve résumé|Ω≈ç√∫˜µ≤≥÷|`

const (
	repeats = 4096
	rounds  = 32
	mod     = 1000000007
)

func main() {
	data := []byte(strings.Repeat(unit, repeats))
	n := len(data)

	var checksum int64
	for r := 0; r < rounds; r++ {
		i := 0
		var codepoints, asciiCount, twoByte, threeByte, fourByte int64
		var codepointSum, codepointHash int64

		for i < n {
			b0 := data[i]
			var cp int64
			var width int

			switch {
			case b0 < 0x80:
				cp = int64(b0)
				width = 1
				asciiCount++
			case b0&0xE0 == 0xC0:
				cp = int64(b0&0x1F)<<6 | int64(data[i+1]&0x3F)
				width = 2
				twoByte++
			case b0&0xF0 == 0xE0:
				cp = int64(b0&0x0F)<<12 |
					int64(data[i+1]&0x3F)<<6 |
					int64(data[i+2]&0x3F)
				width = 3
				threeByte++
			default:
				cp = int64(b0&0x07)<<18 |
					int64(data[i+1]&0x3F)<<12 |
					int64(data[i+2]&0x3F)<<6 |
					int64(data[i+3]&0x3F)
				width = 4
				fourByte++
			}

			codepointSum = (codepointSum + cp) % mod
			codepointHash = (codepointHash*131 + cp) % mod
			codepoints++
			i += width
		}

		checksum = (checksum*31 +
			codepoints +
			asciiCount*3 +
			twoByte*5 +
			threeByte*7 +
			fourByte*11 +
			codepointSum +
			codepointHash) % mod
	}

	fmt.Println(checksum)
}
