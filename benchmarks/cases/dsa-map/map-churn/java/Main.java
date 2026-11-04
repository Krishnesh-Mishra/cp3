// map-churn (java entry) -- 100,000 integer keys in a HashMap, then two rounds
// of delete/reinsert/update churn, folding every key in ascending order after
// each round. The standard hash map is the only data structure used.
//
// Expected output: 942471769.

import java.util.HashMap;

public class Main {
    private static final int KEYS = 100_000;
    private static final int ROUNDS = 2;
    private static final long MOD = 1_000_000_007L;

    public static void main(String[] args) {
        HashMap<Long, Long> map = new HashMap<>();
        for (int i = 0; i < KEYS; i++) {
            map.put((long) i, (long) i * 3 + 1);
        }

        long checksum = 0;
        for (int round = 0; round < ROUNDS; round++) {
            for (int i = 0; i < KEYS; i += 10) {
                map.remove((long) i);
            }
            for (int i = 0; i < KEYS; i += 10) {
                map.put((long) i, (long) i * 5 + round + 2);
            }
            for (int i = 0; i < KEYS; i += 7) {
                map.put((long) i, map.get((long) i) + 1);
            }
            for (int i = 0; i < KEYS; i++) {
                checksum = (checksum * 31 + map.get((long) i)) % MOD;
            }
        }

        System.out.println(checksum);
    }
}
