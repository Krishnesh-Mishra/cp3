package mid

import "giant/src/lib/base"

func M58(x int) int {
    return base.B46(x) + base.B86(x + 1)
}

