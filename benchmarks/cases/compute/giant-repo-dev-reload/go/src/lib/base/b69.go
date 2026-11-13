package base

func B69(x int) int {
    acc := x + 69
    acc = (acc * 31 + 483) % 1000033
    acc = (acc * 131 + x * 17 + 69) % 1000199
    return acc
}

