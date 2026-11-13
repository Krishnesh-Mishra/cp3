package mid

import "giant/src/lib/base"

func M8(x int) int {
    return base.B24(x) + base.B64(x + 1)
}

