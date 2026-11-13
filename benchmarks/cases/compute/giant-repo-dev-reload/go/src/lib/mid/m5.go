package mid

import "giant/src/lib/base"

func M5(x int) int {
    return base.B15(x) + base.B55(x + 1)
}

