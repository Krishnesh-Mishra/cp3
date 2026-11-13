package mid

import "giant/src/lib/base"

func M12(x int) int {
    return base.B36(x) + base.B76(x + 1)
}

