package mid

import "giant/src/lib/base"

func M80(x int) int {
    return base.B112(x) + base.B24(x + 1)
}

