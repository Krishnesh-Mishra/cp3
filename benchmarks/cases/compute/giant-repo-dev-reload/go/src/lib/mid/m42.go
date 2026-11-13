package mid

import "giant/src/lib/base"

func M42(x int) int {
    return base.B126(x) + base.B38(x + 1)
}

