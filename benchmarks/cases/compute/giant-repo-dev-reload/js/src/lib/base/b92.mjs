export function b92(x) {
    let acc = x + 92;
    acc = (acc * 31 + 644) % 1000033;
    acc = (acc * 131 + x * 17 + 92) % 1000199;
    return acc;
}
