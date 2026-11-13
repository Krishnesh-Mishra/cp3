package mid

import "giant/src/lib/base"

func M112(x int) int {
    return base.B80(x) + base.B120(x + 1)
}

