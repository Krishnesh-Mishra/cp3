package mid

import "giant/src/lib/base"

func M105(x int) int {
    return base.B59(x) + base.B99(x + 1)
}

