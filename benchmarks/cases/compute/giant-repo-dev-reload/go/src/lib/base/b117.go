package base

func B117(x int) int {
    acc := x + 117
    acc = (acc * 31 + 819) % 1000033
    acc = (acc * 131 + x * 17 + 117) % 1000199
    return acc
}

