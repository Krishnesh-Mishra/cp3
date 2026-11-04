// json-roundtrip (go entry) -- 20,000 iterations of json.Unmarshal +
// json.Marshal of one representative API record, folded into one integer.
//
// Build: go build -o json-roundtrip.exe main.go
// Run:   ./json-roundtrip.exe
//
// Same payload text, same field fold and same output as the cp3, JS and Python
// entries. `encoding/json` is the standard library decoder/encoder; no
// third-party package is used, matching the suite's dependency-free rule.
//
// Every length in the fold is a byte count of ASCII text, so Go's len() and the
// character counts used by JS, Python and cp3 agree exactly.
package main

import (
	"encoding/json"
	"fmt"
)

type User struct {
	ID      int       `json:"id"`
	Name    string    `json:"name"`
	Email   string    `json:"email"`
	Active  bool      `json:"active"`
	Score   float64   `json:"score"`
	Title   string    `json:"title"`
	Company string    `json:"company"`
	City    string    `json:"city"`
	Country string    `json:"country"`
	Tags    []string  `json:"tags"`
	Samples []int     `json:"samples"`
}

const text = `{"id":48271,"name":"Ada Lovelace","email":"ada.lovelace@example.com","active":true,"score":93.5,"title":"Principal Engineer","company":"Analytical Engines Ltd","city":"London","country":"GB","tags":["analyst","engineer","pioneer","mathematician","compiler","runtime"],"samples":[12,17,23,29,31,37,41,43,47,53]}`

const (
	iterations = 20000
	mod        = 1000003
)

func main() {
	checksum := 0
	for i := 0; i < iterations; i++ {
		var u User
		if err := json.Unmarshal([]byte(text), &u); err != nil {
			panic(err)
		}
		out, err := json.Marshal(u)
		if err != nil {
			panic(err)
		}

		fieldValue := float64(u.ID + len(u.Name) + len(u.Email))
		if u.Active {
			fieldValue++
		}
		fieldValue += u.Score*10 + float64(len(u.Title)+len(u.Company))
		fieldValue += float64(len(u.City) + len(u.Country))
		fieldValue += float64(len(u.Tags) + len(u.Samples))
		for _, tag := range u.Tags {
			fieldValue += float64(len(tag))
		}
		for _, sample := range u.Samples {
			fieldValue += float64(sample)
		}

		checksum = (checksum*31 + int(fieldValue) + len(text) + len(out)) % mod
	}

	fmt.Println(checksum)
}
