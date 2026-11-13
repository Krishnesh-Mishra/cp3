package base

func B89(x int) int {
    acc := x + 89
    acc = (acc * 31 + 623) % 1000033
    acc = (acc * 131 + x * 17 + 89) % 1000199
    return acc
}

