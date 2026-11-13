package mid

import "giant/src/lib/base"

func M14(x int) int {
    return base.B42(x) + base.B82(x + 1)
}

