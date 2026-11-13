package mid

import "giant/src/lib/base"

func M32(x int) int {
    return base.B96(x) + base.B8(x + 1)
}

