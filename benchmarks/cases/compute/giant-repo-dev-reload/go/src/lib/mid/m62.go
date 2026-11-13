package mid

import "giant/src/lib/base"

func M62(x int) int {
    return base.B58(x) + base.B98(x + 1)
}

