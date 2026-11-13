package base

func B68(x int) int {
    acc := x + 68
    acc = (acc * 31 + 476) % 1000033
    acc = (acc * 131 + x * 17 + 68) % 1000199
    return acc
}

