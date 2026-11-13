package mid

import "giant/src/lib/base"

func M15(x int) int {
    return base.B45(x) + base.B85(x + 1)
}

