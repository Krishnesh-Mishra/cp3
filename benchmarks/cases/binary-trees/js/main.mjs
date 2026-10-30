// binary-trees (js entry) -- build and recursively check full binary trees:
// a depth-14 stretch tree, one long-lived depth-13 tree, then the classic
// depth 4..12 allocation loop. Leaves have null children; every node is a
// fresh heap object (no pooling).
//
// Run: node main.mjs
//
// Expected output: 1348958

class Node {
  constructor(left, right) {
    this.left = left;
    this.right = right;
  }
}

const MAX_DEPTH = 13;
const MIN_DEPTH = 4;

function makeTree(depth) {
  if (depth === 0) {
    return new Node(null, null);
  }
  return new Node(makeTree(depth - 1), makeTree(depth - 1));
}

function checkTree(node) {
  if (node.left === null) {
    return 1;
  }
  return 1 + checkTree(node.left) + checkTree(node.right);
}

const stretchDepth = MAX_DEPTH + 1;
let total = checkTree(makeTree(stretchDepth));

const longLived = makeTree(MAX_DEPTH);
const longLivedCheck = checkTree(longLived);

for (let depth = MIN_DEPTH; depth <= MAX_DEPTH; depth += 2) {
  const iterations = 1 << (MAX_DEPTH - depth + MIN_DEPTH);
  let check = 0;
  for (let i = 0; i < iterations; i++) {
    check += checkTree(makeTree(depth));
  }
  total += check;
}

total += longLivedCheck;
console.log(total);
