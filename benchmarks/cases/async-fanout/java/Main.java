// async-fanout (java entry) -- two phases folded into one integer.
//
// Phase 1: 10,000 zero-delay tasks, all created before any of them runs and
//          joined at once; each task resolves with its index and the resolved
//          values are summed (a fan-in).
// Phase 2: 10,000 sequential joins of an already-completed future, folding the
//          loop index at every step (the coroutine / microtask churn probe).
//
// The output is identical to the cp3, Go, JavaScript and Python entries.
// Java 21 virtual threads provide the lightweight task fan-out; a start gate
// keeps every task created before the first one resolves, matching the
// "schedule all, then join" shape of the timer and goroutine entries.

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

public class Main {
    private static final int FANOUT = 10000;
    private static final int CHAIN = 10000;
    private static final long MOD = 1000003L;

    public static void main(String[] args) throws Exception {
        ExecutorService executor = Executors.newVirtualThreadPerTaskExecutor();
        CountDownLatch gate = new CountDownLatch(1);
        List<Future<Integer>> futures = new ArrayList<>(FANOUT);
        try {
            for (int i = 0; i < FANOUT; i++) {
                final int n = i;
                futures.add(executor.submit(() -> {
                    gate.await();
                    return n;
                }));
            }
            gate.countDown();

            long fanSum = 0;
            for (Future<Integer> future : futures) {
                fanSum = fanSum + future.get();
            }
            long checksum = fanSum % MOD;

            for (int i = 0; i < CHAIN; i++) {
                int value = CompletableFuture.completedFuture(i).join();
                checksum = (checksum * 31 + value) % MOD;
            }
            System.out.println(checksum);
        } finally {
            executor.shutdown();
        }
    }
}
