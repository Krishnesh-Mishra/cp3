package base

func B114(x int) int {
    acc := x + 114
    acc = (acc * 31 + 798) % 1000033
    acc = (acc * 131 + x * 17 + 114) % 1000199
    return acc
}

