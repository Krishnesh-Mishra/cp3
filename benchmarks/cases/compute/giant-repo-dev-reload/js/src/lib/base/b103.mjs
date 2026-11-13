export function b103(x) {
    let acc = x + 103;
    acc = (acc * 31 + 721) % 1000033;
    acc = (acc * 131 + x * 17 + 103) % 1000199;
    return acc;
}
