package main

import "fmt"

func xs_next(x uint64) uint64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x }
type TNode struct { v uint64; l, r *TNode }

func build(depth uint64, seed uint64) *TNode {
	s := xs_next(seed)
	if depth == 0 { return &TNode{v: s % 1000} }
	return &TNode{v: s % 1000, l: build(depth-1, s), r: build(depth-1, xs_next(s+1))}
}
func sumTree(n *TNode, p uint64) uint64 {
	if n == nil { return 0 }
	s := n.v ^ p
	s += sumTree(n.l, p); s += sumTree(n.r, p)
	return s
}
func main() {
	root := build(13, 12345)
	var total uint64
	for p := uint64(0); p < 8; p++ { total += sumTree(root, p) }
	fmt.Println(16383); fmt.Println(total)
}
