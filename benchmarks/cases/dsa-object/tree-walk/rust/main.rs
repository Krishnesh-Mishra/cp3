fn xs_next(mut x: u64) -> u64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; x }
struct TNode { v: u64, l: Option<Box<TNode>>, r: Option<Box<TNode>> }
fn build(depth: u64, seed: u64) -> TNode {
    let s = xs_next(seed);
    if depth == 0 { return TNode { v: s % 1000, l: None, r: None }; }
    TNode { v: s % 1000, l: Some(Box::new(build(depth-1, s))), r: Some(Box::new(build(depth-1, xs_next(s+1)))) }
}
fn sum_tree(n: &TNode, p: u64) -> u64 {
    let mut s = n.v ^ p;
    if let Some(l) = &n.l { s += sum_tree(l, p); }
    if let Some(r) = &n.r { s += sum_tree(r, p); }
    s
}
fn main() {
    let depth: u64 = 13;
    let root = build(depth, 12345);
    let count: u64 = 16383;
    let mut total: u64 = 0;
    for p in 0..8 { total += sum_tree(&root, p); }
    println!("{}", count); println!("{}", total);
}
