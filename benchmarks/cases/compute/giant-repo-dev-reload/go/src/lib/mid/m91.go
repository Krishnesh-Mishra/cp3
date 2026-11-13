package mid

import "giant/src/lib/base"

func M91(x int) int {
    return base.B17(x) + base.B57(x + 1)
}

