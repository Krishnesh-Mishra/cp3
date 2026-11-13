package mid

import "giant/src/lib/base"

func M48(x int) int {
    return base.B16(x) + base.B56(x + 1)
}

