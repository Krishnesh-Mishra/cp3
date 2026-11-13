export function b61(x) {
    let acc = x + 61;
    acc = (acc * 31 + 427) % 1000033;
    acc = (acc * 131 + x * 17 + 61) % 1000199;
    return acc;
}
