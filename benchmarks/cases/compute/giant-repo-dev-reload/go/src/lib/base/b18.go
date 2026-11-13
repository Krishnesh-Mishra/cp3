package base

func B18(x int) int {
    acc := x + 18
    acc = (acc * 31 + 126) % 1000033
    acc = (acc * 131 + x * 17 + 18) % 1000199
    return acc
}

