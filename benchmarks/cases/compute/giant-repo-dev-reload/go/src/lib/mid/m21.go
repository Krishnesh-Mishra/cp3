package mid

import "giant/src/lib/base"

func M21(x int) int {
    return base.B63(x) + base.B103(x + 1)
}

