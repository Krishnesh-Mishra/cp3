package mid

import "giant/src/lib/base"

func M114(x int) int {
    return base.B86(x) + base.B126(x + 1)
}

