package app

import "giant/src/lib/mid"

func A30(x int) int {
    return mid.M22(x) + mid.M99(x + 2)
}

