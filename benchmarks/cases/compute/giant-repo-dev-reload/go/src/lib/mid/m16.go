package mid

import "giant/src/lib/base"

func M16(x int) int {
    return base.B48(x) + base.B88(x + 1)
}

