package mid

import "giant/src/lib/base"

func M9(x int) int {
    return base.B27(x) + base.B67(x + 1)
}

