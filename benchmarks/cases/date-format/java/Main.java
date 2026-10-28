// date-format (java entry) -- construct UTC dates from one fixed epoch
// millisecond value, format the components manually, and fold a fixed diff.
//
// Same workload, same fold and same output in every runnable language entry.
// The formatted spelling is built by hand from the UTC fields, so every
// language emits the same 19 ASCII characters: YYYY-MM-DDTHH:MM:SS.
//
// java.time.Instant plus ZoneOffset.UTC is the standard-library calendar
// surface; no locale-aware formatter is used.

import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;

public class Main {
    private static final long BASE_MS = 1700000000000L; // 2023-11-14T22:13:20Z
    private static final long LATER_MS = 1700000123456L; // +123,456 ms
    private static final int ITERATIONS = 50000;
    private static final long MOD = 1000003L;
    private static final int HOUR_SECONDS = 3600;

    // '0'..'9' -> 0..9, '-' -> 10, 'T' -> 11, ':' -> 12.
    private static int charValue(char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch == '-') {
            return 10;
        }
        if (ch == 'T') {
            return 11;
        }
        return 12;
    }

    private static void pad2(StringBuilder out, int value) {
        if (value < 10) {
            out.append('0');
        }
        out.append(value);
    }

    private static void pad4(StringBuilder out, int value) {
        if (value < 10) {
            out.append("000");
        } else if (value < 100) {
            out.append("00");
        } else if (value < 1000) {
            out.append('0');
        }
        out.append(value);
    }

    private static long checksum(int iterations) {
        Instant base = Instant.ofEpochMilli(BASE_MS);
        long diffMs = LATER_MS - BASE_MS;
        long total = 0;

        for (int k = 0; k < iterations; k++) {
            Instant moment = base.plusSeconds(k % HOUR_SECONDS);
            LocalDateTime date = LocalDateTime.ofInstant(moment, ZoneOffset.UTC);

            StringBuilder text = new StringBuilder(19);
            pad4(text, date.getYear());
            text.append('-');
            pad2(text, date.getMonthValue());
            text.append('-');
            pad2(text, date.getDayOfMonth());
            text.append('T');
            pad2(text, date.getHour());
            text.append(':');
            pad2(text, date.getMinute());
            text.append(':');
            pad2(text, date.getSecond());

            int charSum = 0;
            for (int i = 0; i < text.length(); i++) {
                charSum = charSum + charValue(text.charAt(i));
            }

            total = (total * 31 + charSum + text.length() + diffMs) % MOD;
        }
        return total;
    }

    public static void main(String[] args) {
        System.out.println(checksum(ITERATIONS));
    }
}
