export function b76(x) {
    let acc = x + 76;
    acc = (acc * 31 + 532) % 1000033;
    acc = (acc * 131 + x * 17 + 76) % 1000199;
    return acc;
}
