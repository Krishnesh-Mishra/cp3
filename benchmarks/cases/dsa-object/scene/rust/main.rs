fn xs_next(mut x: u64) -> u64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; x }
struct Node { id: i64, tx: i64, ty: i64, tz: i64, rot: i64, kids: Vec<Node> }
fn build(depth: u64, seed: u64, count: &mut i64) -> Node {
    let r = xs_next(seed); let id = *count; *count += 1;
    let mut n = Node { id, tx: (r % 9) as i64, ty: ((r >> 16) % 9) as i64, tz: ((r >> 24) % 9) as i64, rot: ((r >> 12) % 4) as i64, kids: Vec::new() };
    if depth == 0 { return n; }
    let k = 1 + ((r >> 40) % 4); for i in 0..k { n.kids.push(build(depth - 1, xs_next(r + i), count)); }
    n
}
fn animate(n: &mut Node, phase: i64) { if n.id % 7 == phase { n.rot = (n.rot + 1) % 4; n.tx = (n.tx + 1) % 9; } for c in &mut n.kids { animate(c, phase); } }
fn gather(n: &Node) -> i64 { let mut t = n.tx + n.ty + n.tz; for c in &n.kids { t += gather(c); } t }
fn main() { let mut count: i64 = 0; let mut root = build(4, 12345, &mut count); let mut total: i64 = 0; for f in 0..16i64 { animate(&mut root, f % 7); total = (total + gather(&root)) % 1000000007; } println!("{}", count); println!("{}", total); }