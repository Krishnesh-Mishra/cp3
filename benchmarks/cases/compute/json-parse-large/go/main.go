// json-parse-large (go entry) -- build one 204472-byte compact JSON record
// (50000 numbers and 1000 tags), parse it 20 times with encoding/json and fold
// the parsed fields into one integer.
//
// Build:  go build -o json-parse-large.exe main.go
// Run:    json-parse-large.exe
//
// Same text, same field fold and same output as the cp3, JS, Python and Java
// entries. Expected output: 613781. encoding/json is the standard library
// parser; no third-party package is used.

package main

import (
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
)

const (
	numbers    = 50000
	tags       = 1000
	iterations = 20
	mod        = 1000003
)

type Payload struct {
	ID      int      `json:"id"`
	Name    string   `json:"name"`
	Active  bool     `json:"active"`
	Score   float64  `json:"score"`
	Numbers []int    `json:"numbers"`
	Tags    []string `json:"tags"`
}

func buildText() []byte {
	var b strings.Builder
	b.WriteString(`{"id":48271,"name":"Ada Lovelace","active":true,"score":93.5,"numbers":[`)
	for i := 0; i < numbers; i++ {
		if i > 0 {
			b.WriteByte(',')
		}
		b.WriteString(strconv.Itoa((i*17 + 3) % 1000))
	}
	b.WriteString(`],"tags":[`)
	for i := 0; i < tags; i++ {
		if i > 0 {
			b.WriteByte(',')
		}
		b.WriteString(`"tag-`)
		b.WriteString(strconv.Itoa(i))
		b.WriteByte('"')
	}
	b.WriteString(`]}`)
	return []byte(b.String())
}

func main() {
	text := buildText()

	var checksum int64
	for iter := 0; iter < iterations; iter++ {
		var p Payload
		if err := json.Unmarshal(text, &p); err != nil {
			panic(err)
		}

		field := int64(p.ID) + int64(len(p.Name))
		if p.Active {
			field++
		}
		field += int64(p.Score * 10)
		field += int64(len(p.Numbers) + len(p.Tags))

		var numberSum int64
		for _, n := range p.Numbers {
			numberSum += int64(n)
		}
		field += numberSum

		for _, tag := range p.Tags {
			field += int64(len(tag))
		}

		checksum = (checksum*31 + field + int64(len(text))) % mod
	}

	fmt.Println(checksum)
}
