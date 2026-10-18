// generics-mono (go entry) -- the same 2,000,000-iteration sum run twice:
// once through a generic wrapper type plus a generic function, once through a
// hand-written concrete type. Prints both sums on one line.
//
// Build:  go build -o generics-mono.exe main.go
// Run:    generics-mono.exe
//
// Same workload and same output in every language entry (expected line in the
// task README). Go generics are reified at compile time per instantiation, so
// the generic loop and the concrete loop compile to comparable direct code;
// the small gap that remains is the honest result.

package main

import "fmt"

type Box[T any] struct {
	value T
}

func (b Box[T]) Get() T { return b.value }

func sumBox[T any](b Box[T]) T { return b.Get() }

type NumberBox struct {
	value int64
}

func (b NumberBox) Get() int64 { return b.value }

func main() {
	const iterations int64 = 2000000
	const mod int64 = 1000000007

	var totalGeneric int64 = 0
	for i := int64(0); i < iterations; i++ {
		boxed := Box[int64]{value: i}
		totalGeneric = (totalGeneric + sumBox(boxed)) % mod
	}

	var totalConcrete int64 = 0
	for i := int64(0); i < iterations; i++ {
		boxed := NumberBox{value: i}
		totalConcrete = (totalConcrete + boxed.Get()) % mod
	}

	fmt.Println(totalGeneric, totalConcrete)
}
