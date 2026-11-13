package base

func B97(x int) int {
    acc := x + 97
    acc = (acc * 31 + 679) % 1000033
    acc = (acc * 131 + x * 17 + 97) % 1000199
    return acc
}

