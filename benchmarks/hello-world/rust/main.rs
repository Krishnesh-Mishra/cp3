// hello-world (rust entry) -- print one line and exit.
//
// Build:  rustc -O main.rs -o hello-world.exe
// Run:    hello-world.exe
//
// The suite times the whole process: spawn -> output -> exit. Same one line
// of output in every language entry.

fn main() {
    println!("Hello, world!");
}
