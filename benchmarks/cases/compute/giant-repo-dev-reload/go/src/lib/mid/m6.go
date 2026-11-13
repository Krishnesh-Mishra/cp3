package mid

import "giant/src/lib/base"

func M6(x int) int {
    return base.B18(x) + base.B58(x + 1)
}

