package mid

import "giant/src/lib/base"

func M63(x int) int {
    return base.B61(x) + base.B101(x + 1)
}

