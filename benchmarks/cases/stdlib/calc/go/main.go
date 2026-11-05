package main

import "fmt"

func xs_next(x uint64) uint64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x }

const mod = 1000003

func genExpr(seed uint64, corrupt bool) string {
	r := seed
	ops := []byte{'+', '-', '*', '/'} 
	r = xs_next(r)
	terms := 3 + int(r%4)
	var s []byte
	for i := 0; i < terms; i++ {
		if i > 0 { r = xs_next(r); s = append(s, ops[r%4]) }
		r = xs_next(r)
		n := int(r%99) + 1
		if n >= 10 { s = append(s, byte('0'+n/10), byte('0'+n%10)) } else { s = append(s, byte('0'+n)) }
	}
	if corrupt && len(s) > 4 { s[len(s)/2] = 'x' }
	return string(s)
}

type P struct { s []byte; i int; err bool }

func (p *P) expr() int64 {
	v := p.term()
	for !p.err && p.i < len(p.s) && (p.s[p.i] == '+' || p.s[p.i] == '-') { op := p.s[p.i]; p.i++; t := p.term(); if op == '+' { v += t } else { v -= t } }
	return v
}
func (p *P) term() int64 {
	v := p.factor()
	for !p.err && p.i < len(p.s) && (p.s[p.i] == '*' || p.s[p.i] == '/') { op := p.s[p.i]; p.i++; f := p.factor(); if op == '*' { v *= f } else { if f == 0 { p.err = true; break }; v /= f } }
	return v
}
func (p *P) factor() int64 {
	if p.i >= len(p.s) { p.err = true; return 0 }
	if p.s[p.i] == '(' { p.i++; v := p.expr(); if p.i >= len(p.s) || p.s[p.i] != ')' { p.err = true; return 0 }; p.i++; return v }
	if p.s[p.i] == '-' { p.i++; return -p.factor() }
	if p.s[p.i] >= '0' && p.s[p.i] <= '9' { var v int64; for p.i < len(p.s) && p.s[p.i] >= '0' && p.s[p.i] <= '9' { v = v*10 + int64(p.s[p.i]-'0'); p.i++ }; return v }
	p.err = true; return 0
}
func main() {
	var ok, errn, total int64
	for i := uint64(0); i < 5000; i++ { s := genExpr(12345+i, i%4==3); p := &P{s: []byte(s)}; v := p.expr(); if p.err || p.i != len(p.s) { errn++; continue }; ok++; total = (total + v) % mod; if total < 0 { total += mod } }
	fmt.Println(ok); fmt.Println(errn); fmt.Println(total)
}
