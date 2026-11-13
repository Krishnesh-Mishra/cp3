package base

func B119(x int) int {
    acc := x + 119
    acc = (acc * 31 + 833) % 1000033
    acc = (acc * 131 + x * 17 + 119) % 1000199
    return acc
}

