// json-parse-large (java entry) -- build one compact 204,472-byte JSON record
// with 50,000 numbers and 1,000 tags, then parse it 20 times with a small
// hand-rolled recursive-descent parser and fold the parsed fields into one
// integer. No third-party JSON dependency is used.
//
// Expected output: 613781.

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class Main {
    private static final int NUMBERS = 50_000;
    private static final int TAGS = 1_000;
    private static final int ITERATIONS = 20;
    private static final long MOD = 1_000_003L;
    private static final String TEXT = buildText();

    private static final class Payload {
        long id;
        String name;
        boolean active;
        double score;
        int[] numbers;
        String[] tags;
    }

    private static String buildText() {
        StringBuilder out = new StringBuilder(204_472);
        out.append("{\"id\":48271,\"name\":\"Ada Lovelace\",\"active\":true,");
        out.append("\"score\":93.5,\"numbers\":[");
        for (int i = 0; i < NUMBERS; i++) {
            if (i > 0) {
                out.append(',');
            }
            out.append((i * 17 + 3) % 1000);
        }
        out.append("],\"tags\":[");
        for (int i = 0; i < TAGS; i++) {
            if (i > 0) {
                out.append(',');
            }
            out.append("\"tag-").append(i).append('"');
        }
        out.append("]}");
        return out.toString();
    }

    private static final class JsonParser {
        private final String text;
        private int pos;

        JsonParser(String text) {
            this.text = text;
        }

        Object parse() {
            Object value = parseValue();
            skipWhitespace();
            if (pos != text.length()) {
                throw new IllegalArgumentException("trailing JSON data");
            }
            return value;
        }

        private Object parseValue() {
            skipWhitespace();
            char c = text.charAt(pos);
            switch (c) {
                case '{':
                    return parseObject();
                case '[':
                    return parseArray();
                case '"':
                    return parseString();
                case 't':
                    expect("true");
                    return Boolean.TRUE;
                case 'f':
                    expect("false");
                    return Boolean.FALSE;
                case 'n':
                    expect("null");
                    return null;
                default:
                    return parseNumber();
            }
        }

        private Map<String, Object> parseObject() {
            Map<String, Object> object = new LinkedHashMap<>();
            pos++; // '{'
            skipWhitespace();
            if (text.charAt(pos) == '}') {
                pos++;
                return object;
            }
            while (true) {
                skipWhitespace();
                String key = parseString();
                skipWhitespace();
                if (text.charAt(pos) != ':') {
                    throw new IllegalArgumentException("expected ':'");
                }
                pos++;
                object.put(key, parseValue());
                skipWhitespace();
                char next = text.charAt(pos++);
                if (next == '}') {
                    return object;
                }
                if (next != ',') {
                    throw new IllegalArgumentException("expected ',' or '}'");
                }
            }
        }

        private List<Object> parseArray() {
            List<Object> array = new ArrayList<>();
            pos++; // '['
            skipWhitespace();
            if (text.charAt(pos) == ']') {
                pos++;
                return array;
            }
            while (true) {
                array.add(parseValue());
                skipWhitespace();
                char next = text.charAt(pos++);
                if (next == ']') {
                    return array;
                }
                if (next != ',') {
                    throw new IllegalArgumentException("expected ',' or ']'");
                }
            }
        }

        private String parseString() {
            if (text.charAt(pos) != '"') {
                throw new IllegalArgumentException("expected string");
            }
            pos++;
            StringBuilder out = new StringBuilder();
            while (true) {
                char c = text.charAt(pos++);
                if (c == '"') {
                    return out.toString();
                }
                if (c != '\\') {
                    out.append(c);
                    continue;
                }
                char escape = text.charAt(pos++);
                switch (escape) {
                    case '"':
                        out.append('"');
                        break;
                    case '\\':
                        out.append('\\');
                        break;
                    case '/':
                        out.append('/');
                        break;
                    case 'b':
                        out.append('\b');
                        break;
                    case 'f':
                        out.append('\f');
                        break;
                    case 'n':
                        out.append('\n');
                        break;
                    case 'r':
                        out.append('\r');
                        break;
                    case 't':
                        out.append('\t');
                        break;
                    case 'u':
                        out.append((char) Integer.parseInt(
                                text.substring(pos, pos + 4), 16));
                        pos += 4;
                        break;
                    default:
                        throw new IllegalArgumentException("bad escape");
                }
            }
        }

        private Double parseNumber() {
            int start = pos;
            while (pos < text.length()) {
                char c = text.charAt(pos);
                if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.'
                        || c == 'e' || c == 'E') {
                    pos++;
                } else {
                    break;
                }
            }
            return Double.valueOf(text.substring(start, pos));
        }

        private void expect(String literal) {
            if (!text.startsWith(literal, pos)) {
                throw new IllegalArgumentException("expected " + literal);
            }
            pos += literal.length();
        }

        private void skipWhitespace() {
            while (pos < text.length()) {
                char c = text.charAt(pos);
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    pos++;
                } else {
                    break;
                }
            }
        }
    }

    @SuppressWarnings("unchecked")
    private static Payload decode(String text) {
        Map<String, Object> object = (Map<String, Object>) new JsonParser(text).parse();
        Payload payload = new Payload();
        payload.id = ((Double) object.get("id")).longValue();
        payload.name = (String) object.get("name");
        payload.active = (Boolean) object.get("active");
        payload.score = (Double) object.get("score");

        List<Object> numberList = (List<Object>) object.get("numbers");
        payload.numbers = new int[numberList.size()];
        for (int i = 0; i < numberList.size(); i++) {
            payload.numbers[i] = ((Double) numberList.get(i)).intValue();
        }

        List<Object> tagList = (List<Object>) object.get("tags");
        payload.tags = new String[tagList.size()];
        for (int i = 0; i < tagList.size(); i++) {
            payload.tags[i] = (String) tagList.get(i);
        }
        return payload;
    }

    public static void main(String[] args) {
        long checksum = 0;
        for (int iteration = 0; iteration < ITERATIONS; iteration++) {
            Payload payload = decode(TEXT);

            long fieldValue = payload.id
                    + payload.name.length()
                    + (payload.active ? 1 : 0)
                    + (long) (payload.score * 10.0)
                    + payload.numbers.length
                    + payload.tags.length;
            for (int number : payload.numbers) {
                fieldValue = fieldValue + number;
            }
            for (String tag : payload.tags) {
                fieldValue = fieldValue + tag.length();
            }

            checksum = (checksum * 31 + fieldValue + TEXT.length()) % MOD;
        }

        System.out.println(checksum);
    }
}
