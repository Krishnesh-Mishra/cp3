"""buffer-copy (python entry) -- build a 64 KiB byte-pattern buffer, then
reshape it ITERATIONS times: rotate it by a changing offset, overwrite the new
first byte with a cycling marker, and fold a 96-byte window into a checksum.

Run:    python main.py

The other five entries run the same reshape with their own buffer types
(cp3 Buffer, std::vector<uint8_t>, Vec<u8>, []byte, Node Buffer). Every
intermediate buffer is exactly SIZE bytes, and every entry folds the same two
marker positions, so all six languages must print the same number.
"""

SIZE = 65536
ITERATIONS = 5000
WINDOW = 96
SEED = b"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!?"


def buffer_copy_checksum(iterations):
    buf = bytearray(SEED * (SIZE // len(SEED)))
    checksum = 0

    for k in range(iterations):
        offset = (k * 4093 + 17) % SIZE
        marker = SEED[k % len(SEED)]

        # Rotate left by offset and overwrite the new first byte with marker.
        buf = bytearray((marker,)) + buf[offset + 1:] + buf[:offset]

        first_a = buf.find(b"a", 0, WINDOW) + 1
        first_7 = buf.find(b"7", 0, WINDOW) + 1
        checksum = (checksum * 131 + len(buf) + first_a + first_7) % 1000003

    return checksum


print(buffer_copy_checksum(ITERATIONS))
