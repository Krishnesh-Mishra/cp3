package app

import "giant/src/lib/mid"

func A27(x int) int {
    return mid.M7(x) + mid.M84(x + 2)
}

