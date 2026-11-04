// utf8-text (cpp entry) -- build a 417,792-byte UTF-8 text by repeating a
// 102-byte multilingual unit 4,096 times, then decode it by hand from bytes
// for 32 rounds and fold the per-round statistics into one integer.
//
// Build:  g++ -std=c++20 -O3 -o utf8-text.exe main.cpp
// Run:    utf8-text.exe
//
// Same workload and same output in every language entry: 78156718.

#include <iostream>
#include <string>

static const long long kRepeats = 4096;
static const long long kRounds = 32;
static const long long kMod = 1000000007;

int main() {
    const std::string unit =
        "ASCII-0123456789|日本語テキスト|emoji-😀🚀|café naïve résumé|"
        "Ω≈ç√∫˜µ≤≥÷|";

    std::string text;
    text.reserve(unit.size() * (size_t)kRepeats);
    for (long long r = 0; r < kRepeats; ++r) {
        text += unit;
    }

    const unsigned char* bytes =
        reinterpret_cast<const unsigned char*>(text.data());
    const size_t byteLength = text.size();

    long long checksum = 0;
    for (long long round = 0; round < kRounds; ++round) {
        size_t i = 0;
        long long codepoints = 0;
        long long asciiCount = 0;
        long long twoByte = 0;
        long long threeByte = 0;
        long long fourByte = 0;
        long long codepointSum = 0;
        long long codepointHash = 0;

        while (i < byteLength) {
            const unsigned char b0 = bytes[i];
            long long cp;
            size_t width;

            if (b0 < 0x80) {
                cp = b0;
                width = 1;
                asciiCount = asciiCount + 1;
            } else if ((b0 & 0xE0) == 0xC0) {
                cp = ((b0 & 0x1F) << 6) | (bytes[i + 1] & 0x3F);
                width = 2;
                twoByte = twoByte + 1;
            } else if ((b0 & 0xF0) == 0xE0) {
                cp = ((b0 & 0x0F) << 12)
                   | ((bytes[i + 1] & 0x3F) << 6)
                   | (bytes[i + 2] & 0x3F);
                width = 3;
                threeByte = threeByte + 1;
            } else {
                cp = ((b0 & 0x07) << 18)
                   | ((bytes[i + 1] & 0x3F) << 12)
                   | ((bytes[i + 2] & 0x3F) << 6)
                   | (bytes[i + 3] & 0x3F);
                width = 4;
                fourByte = fourByte + 1;
            }

            codepointSum = (codepointSum + cp) % kMod;
            codepointHash = (codepointHash * 131 + cp) % kMod;
            codepoints = codepoints + 1;
            i = i + width;
        }

        checksum = (checksum * 31
                  + codepoints
                  + asciiCount * 3
                  + twoByte * 5
                  + threeByte * 7
                  + fourByte * 11
                  + codepointSum
                  + codepointHash) % kMod;
    }

    std::cout << checksum << "\n";
    return 0;
}
