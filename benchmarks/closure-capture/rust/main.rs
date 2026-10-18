// closure-capture (rust entry) -- two closure shapes back to back over a
// 10,000,000-element source:
//   A: map / filter / reduce with closures that capture offset and cap
//   B: 1000 closures, each capturing its own key, stored in a vector and
//      invoked after the creating scope has ended (escaping closures)
// Prints both sums on one line.
//
// Build:  rustc -O main.rs -o closure-capture.exe
// Run:    closure-capture.exe
//
// Same workload and same output in every language entry (expected line in the
// task README). The iterator chain is lazy and captured by value; the escaping
// phase boxes the closures because each one has a different captured state.

fn main() {
    const SOURCE: i64 = 10_000_000;
    const OFFSET: i64 = 7;
    const CAP: i64 = 3;

    let total_map_reduce: i64 = (0..SOURCE)
        .map(|x| x + OFFSET)
        .filter(|x| x % CAP == 0)
        .sum();

    let mut closures: Vec<Box<dyn Fn(i64) -> i64>> = Vec::with_capacity(1000);
    for key in 0..1000i64 {
        closures.push(Box::new(move |x| x * key + OFFSET));
    }

    let mut total_escaping: i64 = 0;
    for (index, closure) in closures.iter().enumerate() {
        for _ in 0..3000 {
            total_escaping += closure(index as i64);
        }
    }

    println!("{} {}", total_map_reduce, total_escaping);
}
