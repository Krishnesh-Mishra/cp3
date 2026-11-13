export function b10(x) {
    let acc = x + 10;
    acc = (acc * 31 + 70) % 1000033;
    acc = (acc * 131 + x * 17 + 10) % 1000199;
    return acc;
}
