"""os-probe (python entry) -- query platform, CPU count, home, temp and
hostname in a tight loop, folding only machine-independent booleans.

Run:   python main.py

Same workload, same fold and same output in every language entry. The platform
name is mapped to 1/2/3 and reduced to a "documented platform" boolean; the CPU
count and the three path/name strings reduce to non-empty/positive booleans.
No path, hostname or core count is folded, so the checksum cannot vary between
machines.
"""

import os
import socket
import sys
import tempfile

ITERATIONS = 50000
MOD = 1000003

PLATFORM_CODES = {"win32": 1, "darwin": 2, "linux": 3}


def main():
    checksum = 0

    for _ in range(ITERATIONS):
        code = PLATFORM_CODES.get(sys.platform, 0)
        platform_ok = 1 if 1 <= code <= 3 else 0
        cpu_ok = 1 if (os.cpu_count() or 0) > 0 else 0
        home_ok = 1 if os.path.expanduser("~") else 0
        temp_ok = 1 if tempfile.gettempdir() else 0
        host_ok = 1 if socket.gethostname() else 0

        term = (platform_ok * 16 + cpu_ok * 8 + home_ok * 4 +
                temp_ok * 2 + host_ok)
        checksum = (checksum * 31 + term) % MOD

    print(checksum)


if __name__ == "__main__":
    main()
