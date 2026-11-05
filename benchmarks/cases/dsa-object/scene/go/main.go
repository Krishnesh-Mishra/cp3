package main

import "fmt"

func xs_next(x uint64) uint64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x }

type Node struct { id int64; tx, ty, tz, rot int64; kids []*Node }

func build(depth uint64, seed uint64, count *int64) *Node {
	r := xs_next(seed); n := &Node{id: *count, tx: int64(r % 9), ty: int64((r >> 16) % 9), tz: int64((r >> 24) % 9), rot: int64((r >> 12) % 4)}
	*count++
	if depth == 0 { return n }
	k := 1 + int((r >> 40) % 4)
	for i := uint64(0); i < uint64(k); i++ { n.kids = append(n.kids, build(depth-1, xs_next(r+i), count)) }
	return n
}
func animate(n *Node, phase int64) { if n.id%7 == phase { n.rot = (n.rot + 1) % 4; n.tx = (n.tx + 1) % 9 }; for _, c := range n.kids { animate(c, phase) } }
func gather(n *Node) int64 { t := n.tx + n.ty + n.tz; for _, c := range n.kids { t += gather(c) }; return t }
func main() { var count int64; root := build(4, 12345, &count); var total int64; for f := int64(0); f < 16; f++ { animate(root, f%7); total = (total + gather(root)) % 1000000007 }; fmt.Println(count); fmt.Println(total) }
