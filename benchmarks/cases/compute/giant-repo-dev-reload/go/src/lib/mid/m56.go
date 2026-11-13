package mid

import "giant/src/lib/base"

func M56(x int) int {
    return base.B40(x) + base.B80(x + 1)
}

