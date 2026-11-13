export function b25(x) {
    let acc = x + 25;
    acc = (acc * 31 + 175) % 1000033;
    acc = (acc * 131 + x * 17 + 25) % 1000199;
    return acc;
}
