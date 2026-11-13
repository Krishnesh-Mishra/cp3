package mid

import "giant/src/lib/base"

func M46(x int) int {
    return base.B10(x) + base.B50(x + 1)
}

