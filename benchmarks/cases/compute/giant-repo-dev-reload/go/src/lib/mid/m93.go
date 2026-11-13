package mid

import "giant/src/lib/base"

func M93(x int) int {
    return base.B23(x) + base.B63(x + 1)
}

