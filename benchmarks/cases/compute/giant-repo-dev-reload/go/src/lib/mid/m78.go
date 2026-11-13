package mid

import "giant/src/lib/base"

func M78(x int) int {
    return base.B106(x) + base.B18(x + 1)
}

