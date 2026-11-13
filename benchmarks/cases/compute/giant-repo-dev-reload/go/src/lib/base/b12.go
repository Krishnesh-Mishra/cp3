package base

func B12(x int) int {
    acc := x + 12
    acc = (acc * 31 + 84) % 1000033
    acc = (acc * 131 + x * 17 + 12) % 1000199
    return acc
}

