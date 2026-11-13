package mid

import "giant/src/lib/base"

func M37(x int) int {
    return base.B111(x) + base.B23(x + 1)
}

