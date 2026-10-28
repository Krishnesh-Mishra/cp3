// os-probe (java entry) -- query platform, CPU count, home, temp and hostname
// in a tight loop, folding only machine-independent booleans.
//
// Same workload, same fold and same output in every language entry. The
// platform name is mapped to 1/2/3 and reduced to a "documented platform"
// boolean; the CPU count and the three path/name strings reduce to
// non-empty/positive booleans. No path, hostname or core count is folded, so
// the checksum cannot vary between machines.

import java.net.InetAddress;
import java.net.UnknownHostException;

public class Main {
    private static final int ITERATIONS = 50000;
    private static final long MOD = 1000003L;

    private static int platformCode() {
        String name = System.getProperty("os.name");
        if (name == null) {
            return 0;
        }
        if (name.contains("Windows")) {
            return 1;
        }
        if (name.contains("Mac")) {
            return 2;
        }
        if (name.contains("Linux")) {
            return 3;
        }
        return 0;
    }

    private static boolean hostnameOk() {
        try {
            String host = InetAddress.getLocalHost().getHostName();
            return host != null && !host.isEmpty();
        } catch (UnknownHostException e) {
            return false;
        }
    }

    private static long probeChecksum(int iterations) {
        long checksum = 0;
        for (int k = 0; k < iterations; k++) {
            int code = platformCode();
            int platformOk = code >= 1 && code <= 3 ? 1 : 0;
            int cpuOk = Runtime.getRuntime().availableProcessors() > 0 ? 1 : 0;

            String home = System.getProperty("user.home");
            int homeOk = home != null && !home.isEmpty() ? 1 : 0;

            String temp = System.getProperty("java.io.tmpdir");
            int tempOk = temp != null && !temp.isEmpty() ? 1 : 0;

            int hostOk = hostnameOk() ? 1 : 0;

            int term = platformOk * 16 + cpuOk * 8 + homeOk * 4 + tempOk * 2 + hostOk;
            checksum = (checksum * 31 + term) % MOD;
        }
        return checksum;
    }

    public static void main(String[] args) {
        System.out.println(probeChecksum(ITERATIONS));
    }
}
