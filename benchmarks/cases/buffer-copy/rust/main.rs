// buffer-copy (rust entry) -- build a 64 KiB byte-pattern buffer, then reshape
// it ITERATIONS times: rotate it by a changing offset, overwrite the new first
// byte with a cycling marker, and fold a 96-byte window into a checksum.
//
// Build:  rustc -O main.rs -o buffer-copy.exe
// Run:    buffer-copy.exe
//
// The other five entries run the same reshape with their own buffer types
// (cp3 Buffer, std::vector<uint8_t>, []byte, Node Buffer, bytearray). Every
// intermediate buffer is exactly SIZE bytes, and every entry folds the same
// two marker positions, so all six languages must print the same number.

const SIZE: usize = 65536;
const ITERATIONS: usize = 5000;
const WINDOW: usize = 96;
const SEED: &[u8] = b"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!?";

fn main() {
    let mut buf: Vec<u8> = (0..SIZE).map(|i| SEED[i % SEED.len()]).collect();
    let mut checksum: i64 = 0;

    for k in 0..ITERATIONS {
        let offset = (k * 4093 + 17) % SIZE;
        let marker = SEED[k % SEED.len()];

        // Rotate left by offset and overwrite the new first byte with marker.
        let mut next = Vec::with_capacity(SIZE);
        next.push(marker);
        next.extend_from_slice(&buf[offset + 1..]);
        next.extend_from_slice(&buf[..offset]);
        buf = next;

        let first_a = buf[..WINDOW]
            .iter()
            .position(|&b| b == b'a')
            .map(|p| p as i64)
            .unwrap_or(-1);
        let first_7 = buf[..WINDOW]
            .iter()
            .position(|&b| b == b'7')
            .map(|p| p as i64)
            .unwrap_or(-1);
        checksum = (checksum * 131 + buf.len() as i64 + (first_a + 1) + (first_7 + 1)) % 1000003;
    }

    println!("{}", checksum);
}
