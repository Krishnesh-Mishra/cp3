package mid

import "giant/src/lib/base"

func M104(x int) int {
    return base.B56(x) + base.B96(x + 1)
}

