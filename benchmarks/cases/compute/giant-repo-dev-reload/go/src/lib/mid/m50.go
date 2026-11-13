package mid

import "giant/src/lib/base"

func M50(x int) int {
    return base.B22(x) + base.B62(x + 1)
}

