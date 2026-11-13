package app

import "giant/src/lib/mid"

func A36(x int) int {
    return mid.M52(x) + mid.M1(x + 2)
}

