package mid

import "giant/src/lib/base"

func M96(x int) int {
    return base.B32(x) + base.B72(x + 1)
}

