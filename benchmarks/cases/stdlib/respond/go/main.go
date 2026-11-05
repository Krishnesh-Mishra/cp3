package main

import (
	"fmt"
	"strconv"
	"strings"
)

func xsNext(x uint64) uint64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x }

func main() {
	var out strings.Builder
	r := uint64(12345)
	for i := 0; i < 10000; i++ {
		r = xsNext(r)
		id := r % 100000
		r = xsNext(r)
		length := 4 + int(r%9)

		var name strings.Builder
		for j := 0; j < length; j++ {
			r = xsNext(r)
			if j%8 == 7 {
				if r%2 == 0 {
					name.WriteByte('"')
				} else {
					name.WriteByte('\\')
				}
			} else {
				name.WriteByte(byte('a' + r%26))
			}
		}

		r = xsNext(r)
		score := r % 1000
		active := (r>>8)%2 == 0

		out.WriteString("{\"id\":")
		out.WriteString(strconv.FormatUint(id, 10))
		out.WriteString(",\"name\":\"")
		raw := name.String()
		for k := 0; k < len(raw); k++ {
			c := raw[k]
			if c == '"' || c == '\\' {
				out.WriteByte('\\')
			}
			out.WriteByte(c)
		}
		out.WriteString("\",\"score\":")
		out.WriteString(strconv.FormatUint(score, 10))
		out.WriteString(",\"active\":")
		if active {
			out.WriteString("true")
		} else {
			out.WriteString("false")
		}
		out.WriteString("},")
	}

	text := out.String()
	h := uint64(1469598103934665603)
	for i := 0; i < len(text); i++ {
		h = (h ^ uint64(text[i])) * 1099511628211
	}
	fmt.Println(len(text))
	fmt.Println(h)
}
