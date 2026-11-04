// regex-scan (java entry) -- repeat a 48-byte ASCII unit 5,000 times, then run
// the global pattern [0-9]+ over the 240,000-byte text eight times, counting
// matches and summing their numeric values each round.
//
// Expected output: 735703090.

import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class Main {
    private static final int REPEATS = 5000;
    private static final int ROUNDS = 8;
    private static final long MOD = 1_000_000_007L;
    private static final String UNIT =
            "alpha 123 beta 45 gamma 6789 delta 0 epsilon 42\n";
    private static final Pattern PATTERN = Pattern.compile("[0-9]+");

    public static void main(String[] args) {
        StringBuilder builder = new StringBuilder(UNIT.length() * REPEATS);
        for (int r = 0; r < REPEATS; r++) {
            builder.append(UNIT);
        }
        String text = builder.toString();

        long checksum = 0;
        for (int round = 0; round < ROUNDS; round++) {
            Matcher matcher = PATTERN.matcher(text);
            long count = 0;
            long sum = 0;
            while (matcher.find()) {
                count = count + 1;
                sum = sum + Long.parseLong(matcher.group());
            }
            checksum = (checksum * 31 + count + sum) % MOD;
        }

        System.out.println(checksum);
    }
}
