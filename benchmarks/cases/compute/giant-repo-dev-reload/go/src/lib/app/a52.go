package app

import "giant/src/lib/mid"

func A52(x int) int {
    return mid.M4(x) + mid.M81(x + 2)
}

