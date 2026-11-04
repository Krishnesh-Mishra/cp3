// binary-trees (go entry) -- build and check the classic full binary trees:
// a depth-14 stretch tree, a depth-13 long-lived tree, then the per-depth
// batches from depth 4 through 12, all as ordinary heap-allocated nodes.
//
// Build:  go build -o binary-trees.exe main.go
// Run:    binary-trees.exe
//
// Same workload and same output in every language entry. Expected output:
// 1348958. Nodes are never reused or pooled: the recursive allocation and the
// pointer-chasing check are the workload.

package main

import "fmt"

const (
	maxDepth = 13
	minDepth = 4
)

type Node struct {
	left  *Node
	right *Node
}

func makeTree(depth int) *Node {
	if depth == 0 {
		return &Node{}
	}
	return &Node{left: makeTree(depth - 1), right: makeTree(depth - 1)}
}

func checkTree(n *Node) int64 {
	if n.left == nil {
		return 1
	}
	return 1 + checkTree(n.left) + checkTree(n.right)
}

func main() {
	stretchDepth := maxDepth + 1
	total := checkTree(makeTree(stretchDepth))

	longLived := makeTree(maxDepth)
	longLivedCheck := checkTree(longLived)

	for depth := minDepth; depth <= maxDepth; depth += 2 {
		iterations := int64(1) << (maxDepth - depth + minDepth)
		var check int64
		for i := int64(0); i < iterations; i++ {
			check += checkTree(makeTree(depth))
		}
		total += check
	}

	total += longLivedCheck
	fmt.Println(total)
}
