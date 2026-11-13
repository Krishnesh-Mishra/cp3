package mid

import "giant/src/lib/base"

func M102(x int) int {
    return base.B50(x) + base.B90(x + 1)
}

