// =============================================================================
// src/lsp/src/framing.cpp - Content-Length framing from detail/framing.hpp.
//
// OWNERSHIP: LSP workstream (src/lsp/**). Implements detail/framing.hpp.
//
// Header parsing is deliberately forgiving (any header casing, extra headers,
// LF-only line endings) but a missing or malformed Content-Length is fatal:
// without a trustworthy frame boundary the remaining stream is unparseable.
// =============================================================================
#include "detail/framing.hpp"

#include <cstdint>
#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <fcntl.h>
#  include <io.h>
#  include <windows.h>
#endif

namespace cp3::lsp {
namespace {

// A header block this large means the client is not speaking LSP.
inline constexpr std::size_t kMaxHeaderBytes = 8u * 1024u;

void toLowerAscii(std::string& text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
}

std::string trimAscii(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// Reads bytes until the blank line that ends the header block.
bool readHeaderBlock(std::FILE* in, std::string& headers, std::string& err) {
    headers.clear();
    int c = 0;
    while ((c = std::fgetc(in)) != EOF) {
        headers.push_back(static_cast<char>(c));
        if (headers.size() > kMaxHeaderBytes) {
            err = "LSP header block exceeds the size cap";
            return false;
        }
        const std::size_t n = headers.size();
        if (n >= 4 && headers.compare(n - 4, 4, "\r\n\r\n") == 0) return true;
        if (n >= 2 && headers.compare(n - 2, 2, "\n\n") == 0) return true;
    }
    err = headers.empty() ? kEofReason : "EOF inside the LSP header block";
    return false;
}

// The first `Content-Length` header wins; the value must be decimal digits.
bool parseContentLength(const std::string& headers, std::size_t& out,
                        std::string& err) {
    std::size_t pos = 0;
    while (pos < headers.size()) {
        std::size_t end = headers.find('\n', pos);
        if (end == std::string::npos) end = headers.size();
        std::string line = headers.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        toLowerAscii(name);
        if (name != "content-length") continue;
        const std::string value = trimAscii(line.substr(colon + 1));
        if (value.empty() || value.size() > 10) {
            err = "malformed Content-Length";
            return false;
        }
        std::size_t length = 0;
        for (const char digit : value) {
            if (digit < '0' || digit > '9') {
                err = "malformed Content-Length";
                return false;
            }
            length = length * 10 + static_cast<std::size_t>(digit - '0');
        }
        out = length;
        return true;
    }
    err = "missing Content-Length";
    return false;
}

}  // namespace

bool readFrame(std::FILE* in, std::string& body, std::string& err) {
    std::string headers;
    if (!readHeaderBlock(in, headers, err)) return false;
    std::size_t length = 0;
    if (!parseContentLength(headers, length, err)) return false;
    if (length > kMaxFrameBytes) {
        err = "LSP frame exceeds the size cap";
        return false;
    }
    body.assign(length, '\0');
    std::size_t got = 0;
    while (got < length) {
        const std::size_t n = std::fread(&body[got], 1, length - got, in);
        if (n == 0) {
            err = "EOF inside the LSP frame body";
            return false;
        }
        got += n;
    }
    return true;
}

bool writeFrame(std::FILE* out, std::string_view body) {
    char header[48] = {};
    const int written = std::snprintf(header, sizeof(header),
                                      "Content-Length: %zu\r\n\r\n", body.size());
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(header)) {
        return false;
    }
    const std::size_t headerSize = static_cast<std::size_t>(written);
    if (std::fwrite(header, 1, headerSize, out) != headerSize) return false;
    if (!body.empty() && std::fwrite(body.data(), 1, body.size(), out) != body.size()) {
        return false;
    }
    return std::fflush(out) == 0;
}

void setBinaryStdio() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    // Unbuffered stdin: the coalescing window asks the OS how many bytes are
    // still waiting, and that answer is only accurate while nothing sits in
    // the C runtime's own read-ahead buffer. Header bytes (tens per frame) are
    // then read one at a time; bodies are still read in one fread per chunk.
    std::setvbuf(stdin, nullptr, _IONBF, 0);
}

bool stdinHasData() {
#if defined(_WIN32)
    const intptr_t raw = _get_osfhandle(_fileno(stdin));
    if (raw == -1) return false;
    const HANDLE handle = reinterpret_cast<HANDLE>(raw);
    if (GetFileType(handle) != FILE_TYPE_PIPE) return false;
    DWORD available = 0;
    if (PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) == 0) {
        return false;
    }
    return available > 0;
#else
    return false;
#endif
}

}  // namespace cp3::lsp
