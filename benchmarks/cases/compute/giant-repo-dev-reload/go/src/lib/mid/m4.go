package mid

import "giant/src/lib/base"

func M4(x int) int {
    return base.B12(x) + base.B52(x + 1)
}

