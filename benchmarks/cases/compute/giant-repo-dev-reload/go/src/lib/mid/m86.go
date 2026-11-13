package mid

import "giant/src/lib/base"

func M86(x int) int {
    return base.B2(x) + base.B42(x + 1)
}

