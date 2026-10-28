// json-roundtrip (java entry) -- 20,000 iterations of parse + stringify of one
// representative API record, folded into one integer.
//
// Java has no standard-library JSON surface, so this entry carries a small
// recursive-descent parser and a compact serializer for the fixed record. The
// payload is ASCII-only, and the serializer emits the same compact text as the
// other entries, so the folded output length is identical.
//
// The fold includes the length of the stringify result, so the serializer
// cannot be optimized away: a language that formats the text differently
// prints a different number.

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class Main {
    private static final String TEXT =
            "{\"id\":48271,\"name\":\"Ada Lovelace\",\"email\":\"ada.lovelace@example.com\","
            + "\"active\":true,\"score\":93.5,\"title\":\"Principal Engineer\","
            + "\"company\":\"Analytical Engines Ltd\",\"city\":\"London\",\"country\":\"GB\","
            + "\"tags\":[\"analyst\",\"engineer\",\"pioneer\",\"mathematician\",\"compiler\","
            + "\"runtime\"],\"samples\":[12,17,23,29,31,37,41,43,47,53]}";
    private static final int ITERATIONS = 20000;
    private static final long MOD = 1000003L;

    private static final class User {
        long id;
        String name;
        String email;
        boolean active;
        double score;
        String title;
        String company;
        String city;
        String country;
        String[] tags;
        long[] samples;
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
                        out.append((char) Integer.parseInt(text.substring(pos, pos + 4), 16));
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

    private static void appendJsonString(StringBuilder out, String value) {
        out.append('"');
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            switch (c) {
                case '"':
                    out.append("\\\"");
                    break;
                case '\\':
                    out.append("\\\\");
                    break;
                case '\b':
                    out.append("\\b");
                    break;
                case '\f':
                    out.append("\\f");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\r':
                    out.append("\\r");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    if (c < 0x20) {
                        out.append(String.format("\\u%04x", (int) c));
                    } else {
                        out.append(c);
                    }
                    break;
            }
        }
        out.append('"');
    }

    private static String stringify(User u) {
        StringBuilder out = new StringBuilder(310);
        out.append('{');
        out.append("\"id\":").append(u.id).append(',');
        out.append("\"name\":");
        appendJsonString(out, u.name);
        out.append(',');
        out.append("\"email\":");
        appendJsonString(out, u.email);
        out.append(',');
        out.append("\"active\":").append(u.active).append(',');
        out.append("\"score\":").append(u.score).append(',');
        out.append("\"title\":");
        appendJsonString(out, u.title);
        out.append(',');
        out.append("\"company\":");
        appendJsonString(out, u.company);
        out.append(',');
        out.append("\"city\":");
        appendJsonString(out, u.city);
        out.append(',');
        out.append("\"country\":");
        appendJsonString(out, u.country);
        out.append(',');
        out.append("\"tags\":[");
        for (int i = 0; i < u.tags.length; i++) {
            if (i > 0) {
                out.append(',');
            }
            appendJsonString(out, u.tags[i]);
        }
        out.append("],");
        out.append("\"samples\":[");
        for (int i = 0; i < u.samples.length; i++) {
            if (i > 0) {
                out.append(',');
            }
            out.append(u.samples[i]);
        }
        out.append("]}");
        return out.toString();
    }

    @SuppressWarnings("unchecked")
    private static User decode(String text) {
        Map<String, Object> object = (Map<String, Object>) new JsonParser(text).parse();
        User u = new User();
        u.id = ((Double) object.get("id")).longValue();
        u.name = (String) object.get("name");
        u.email = (String) object.get("email");
        u.active = (Boolean) object.get("active");
        u.score = (Double) object.get("score");
        u.title = (String) object.get("title");
        u.company = (String) object.get("company");
        u.city = (String) object.get("city");
        u.country = (String) object.get("country");

        List<Object> tagList = (List<Object>) object.get("tags");
        u.tags = new String[tagList.size()];
        for (int i = 0; i < tagList.size(); i++) {
            u.tags[i] = (String) tagList.get(i);
        }

        List<Object> sampleList = (List<Object>) object.get("samples");
        u.samples = new long[sampleList.size()];
        for (int i = 0; i < sampleList.size(); i++) {
            u.samples[i] = ((Double) sampleList.get(i)).longValue();
        }
        return u;
    }

    public static void main(String[] args) {
        long checksum = 0;
        for (int i = 0; i < ITERATIONS; i++) {
            User u = decode(TEXT);
            String out = stringify(u);

            long fieldValue = u.id + u.name.length() + u.email.length();
            if (u.active) {
                fieldValue = fieldValue + 1;
            }
            fieldValue = fieldValue + (long) (u.score * 10.0)
                    + u.title.length() + u.company.length();
            fieldValue = fieldValue + u.city.length() + u.country.length();
            fieldValue = fieldValue + u.tags.length + u.samples.length;
            for (String tag : u.tags) {
                fieldValue = fieldValue + tag.length();
            }
            for (long sample : u.samples) {
                fieldValue = fieldValue + sample;
            }

            checksum = (checksum * 31 + fieldValue + TEXT.length() + out.length()) % MOD;
        }
        System.out.println(checksum);
    }
}
