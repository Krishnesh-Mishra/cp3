package mid

import "giant/src/lib/base"

func M53(x int) int {
    return base.B31(x) + base.B71(x + 1)
}

