"""async-fanout (python entry) -- two phases folded into one integer.

Run:   python main.py

Phase 1: 10,000 zero-delay tasks, all created in one step and joined at once
         with asyncio.gather; each task resolves with its index, and the
         resolved values are summed.
Phase 2: 10,000 sequential awaits of an already-resolved yield, folding the
         loop index at every step (the task/coroutine churn probe).

The output is identical to the cp3, JS and Go entries.

Fairness note: asyncio.sleep(0) is a bare event-loop yield, not a kernel timer,
so Python does not pay timer-wheel costs in phase 1 the way cp3's sleep(0) and
Node's setTimeout(0) do. That is the idiomatic zero-delay primitive in each
runtime; the task README states the asymmetry.
"""

import asyncio

FANOUT = 10000
CHAIN = 10000
MOD = 1000003


async def tick(n):
    await asyncio.sleep(0)
    return n


async def main():
    values = await asyncio.gather(*(tick(i) for i in range(FANOUT)))
    checksum = sum(values) % MOD

    for i in range(CHAIN):
        value = await asyncio.sleep(0, result=i)
        checksum = (checksum * 31 + value) % MOD

    print(checksum)


if __name__ == "__main__":
    asyncio.run(main())
