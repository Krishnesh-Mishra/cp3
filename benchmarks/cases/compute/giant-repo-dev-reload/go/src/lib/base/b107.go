package base

func B107(x int) int {
    acc := x + 107
    acc = (acc * 31 + 749) % 1000033
    acc = (acc * 131 + x * 17 + 107) % 1000199
    return acc
}

