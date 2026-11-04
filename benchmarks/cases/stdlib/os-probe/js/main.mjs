// os-probe (js entry) -- query platform, CPU count, home, temp and hostname
// in a tight loop, folding only machine-independent booleans.
//
// Run: node main.mjs
//
// Same workload, same fold and same output in every language entry. The
// platform name is mapped to 1/2/3 and reduced to a "documented platform"
// boolean; the CPU count and the three path/name strings reduce to
// non-empty/positive booleans. No path, hostname or core count is folded, so
// the checksum cannot vary between machines.

import os from "node:os";

const ITERATIONS = 50000;
const MOD = 1000003;

function platformCode(name) {
  if (name === "win32" || name === "windows") return 1;
  if (name === "darwin" || name === "macos") return 2;
  if (name === "linux") return 3;
  return 0;
}

let checksum = 0;

for (let k = 0; k < ITERATIONS; k++) {
  const code = platformCode(os.platform());
  const platformOk = code >= 1 && code <= 3 ? 1 : 0;
  const cpuOk = os.availableParallelism() > 0 ? 1 : 0;
  const homeOk = os.homedir().length > 0 ? 1 : 0;
  const tempOk = os.tmpdir().length > 0 ? 1 : 0;
  const hostOk = os.hostname().length > 0 ? 1 : 0;

  const term = platformOk * 16 + cpuOk * 8 + homeOk * 4 + tempOk * 2 + hostOk;
  checksum = (checksum * 31 + term) % MOD;
}

console.log(checksum);
