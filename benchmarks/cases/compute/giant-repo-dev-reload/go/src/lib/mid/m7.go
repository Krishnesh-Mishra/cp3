package mid

import "giant/src/lib/base"

func M7(x int) int {
    return base.B21(x) + base.B61(x + 1)
}

