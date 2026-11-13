package base

func B49(x int) int {
    acc := x + 49
    acc = (acc * 31 + 343) % 1000033
    acc = (acc * 131 + x * 17 + 49) % 1000199
    return acc
}

