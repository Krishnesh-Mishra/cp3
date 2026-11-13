package mid

import "giant/src/lib/base"

func M87(x int) int {
    return base.B5(x) + base.B45(x + 1)
}

