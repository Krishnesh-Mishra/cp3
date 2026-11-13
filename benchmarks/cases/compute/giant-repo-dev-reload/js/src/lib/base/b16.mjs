export function b16(x) {
    let acc = x + 16;
    acc = (acc * 31 + 112) % 1000033;
    acc = (acc * 131 + x * 17 + 16) % 1000199;
    return acc;
}
