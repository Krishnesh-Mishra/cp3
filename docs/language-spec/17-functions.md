# 17. Functions

```cp3
function add(a: number, b: number): number {
  return a + b;
}

function createPerson(): Person {
  return new Person();
}
```

No C++-style declaration complexity; the compiler determines the efficient underlying representation. Function overloading (multiple functions with the same name, different parameter types) is supported directly - C++ already handles this natively.

