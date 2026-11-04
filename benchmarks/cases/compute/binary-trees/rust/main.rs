// binary-trees (rust entry) -- build full binary trees from heap-allocated
// Option<Box<Node>> nodes, check them recursively, and discard the short-lived
// trees each iteration while one long-lived tree stays alive.
//
// Build:  rustc -C opt-level=3 main.rs -o binary-trees.exe
// Run:    binary-trees.exe
//
// Expected output: 1348958

struct Node {
    left: Option<Box<Node>>,
    right: Option<Box<Node>>,
}

// A leaf is None; an internal node owns exactly two children.
fn make_tree(depth: i64) -> Option<Box<Node>> {
    if depth == 0 {
        None
    } else {
        Some(Box::new(Node {
            left: make_tree(depth - 1),
            right: make_tree(depth - 1),
        }))
    }
}

fn check_tree(node: &Option<Box<Node>>) -> i64 {
    match node {
        None => 1,
        Some(n) => 1 + check_tree(&n.left) + check_tree(&n.right),
    }
}

fn main() {
    const MAX_DEPTH: i64 = 13;
    const MIN_DEPTH: i64 = 4;

    let stretch_depth = MAX_DEPTH + 1;
    let stretch_check = check_tree(&make_tree(stretch_depth));

    let long_lived = make_tree(MAX_DEPTH);
    let long_lived_check = check_tree(&long_lived);

    let mut total = stretch_check;
    let mut depth = MIN_DEPTH;
    while depth <= MAX_DEPTH {
        let iterations = 1i64 << (MAX_DEPTH - depth + MIN_DEPTH);
        let mut check = 0i64;
        for _ in 0..iterations {
            check += check_tree(&make_tree(depth));
        }
        total += check;
        depth += 2;
    }

    total += long_lived_check;
    println!("{}", total);
}
