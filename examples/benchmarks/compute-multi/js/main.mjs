// compute-multi (js entry) -- the compute-single workload split across
// os.availableParallelism() worker_threads, each summing a contiguous chunk
// of 1..1,000,000; the partials are combined on the main thread.
//
// Run: node main.mjs
//
// Prints the same single integer as every other entry (single or multi), so
// the outputs cross-check each other. Worker threads keep separate V8
// isolates, so each one JIT-warms its own copy of the hot loop.

import os from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

const N = 1_000_000;

function collatzSteps(start) {
  let n = start;
  let steps = 0;
  while (n !== 1) {
    if (n % 2 === 0) {
      n = n / 2;
    } else {
      n = 3 * n + 1;
    }
    steps += 1;
  }
  return steps;
}

function sumRange(first, last) {
  let total = 0;
  for (let start = first; start <= last; start++) {
    total += collatzSteps(start);
  }
  return total;
}

if (!isMainThread) {
  parentPort.postMessage(sumRange(workerData.first, workerData.last));
} else {
  const workers = os.availableParallelism();
  const chunk = Math.ceil(N / workers);

  const pool = [];
  for (let w = 0; w < workers; w++) {
    const first = w * chunk + 1;
    const last = Math.min(N, (w + 1) * chunk);
    if (first > N) break;
    pool.push(new Worker(new URL(import.meta.url), { workerData: { first, last } }));
  }

  let total = 0;
  let finished = 0;
  for (const worker of pool) {
    worker.on("message", (partial) => {
      total += partial;
    });
    worker.on("exit", () => {
      finished += 1;
      if (finished === pool.length) {
        console.log(total);
      }
    });
  }
}
