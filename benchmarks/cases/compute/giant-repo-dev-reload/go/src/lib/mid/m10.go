package mid

import "giant/src/lib/base"

func M10(x int) int {
    return base.B30(x) + base.B70(x + 1)
}

