package app

import "giant/src/lib/mid"

func A15(x int) int {
    return mid.M75(x) + mid.M24(x + 2)
}

