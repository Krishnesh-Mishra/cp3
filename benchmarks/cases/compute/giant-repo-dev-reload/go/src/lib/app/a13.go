package app

import "giant/src/lib/mid"

func A13(x int) int {
    return mid.M65(x) + mid.M14(x + 2)
}

