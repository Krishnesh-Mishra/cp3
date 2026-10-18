// hello-world (go entry) -- print one line and exit.
//
// Build:  go build -o hello-world.exe main.go
// Run:    hello-world.exe
//
// The suite times the whole process: spawn -> output -> exit. Same one line
// of output in every language entry.

package main

import "fmt"

func main() {
	fmt.Println("Hello, world!")
}
