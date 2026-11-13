package app

import "giant/src/lib/mid"

func A26(x int) int {
    return mid.M2(x) + mid.M79(x + 2)
}

