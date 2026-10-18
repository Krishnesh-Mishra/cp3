// =============================================================================
// src/std/console/runtime_pump_read.cpp - the stdin pump's platform readers.
//
// OWNERSHIP: the std/console family (src/std/console/**). Split from
// runtime_pump.cpp so both stay under the source-size budget; the shared
// internals are the private pump.hpp. Nothing here touches the loop: every
// event is posted through the helpers runtime_pump.cpp owns.
//
// Two stdin shapes:
//   * a Windows console: `ReadConsoleInputW` key records; named keys come from
//     the virtual-key codes, literal characters from the UTF-16 unit.
//   * a pipe/file (or a POSIX terminal): raw bytes; lines split on CR/LF and
//     the ANSI escape sequences the arrow keys send are parsed back. A lone ESC
//     at the end of a chunk is only reported once more input arrives or at EOF.
//
// Raw mode is entered (and left) around the reads while a real terminal is
// attached; typed characters are echoed exactly while a line-oriented consumer
// (`input()` or a "line" listener) exists, like a terminal would.
// =============================================================================
#include "pump.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

namespace cp3::runtime::console::detail {
namespace {

bool echoLines(PumpState& st) {
    std::lock_guard<std::mutex> lock(st.mutex);
    // Only a real terminal echoes: a pipe/file carries no typed characters.
    if (!st.consoleInput) return false;
    return !st.pendingLines.empty() || !st.lineListeners.empty();
}

void echoText(const std::string& text) {
    std::cout << text;
    std::cout.flush();
}

void finishLine(PumpState* st) {
    std::string line = std::move(st->partialLine);
    st->partialLine.clear();
    postLine(st, std::move(line));
}

// A literal character: part of the line being assembled, a keypress event, and
// (while a line-oriented consumer exists) echoed back like a terminal.
void noteChar(PumpState* st, const std::string& ch) {
    st->partialLine += ch;
    postKey(st, ch);
    if (echoLines(*st)) echoText(ch);
}

// A named key: `Enter` closes the current line; `Backspace` edits it; every
// named key is a keypress event.
void noteNamed(PumpState* st, const std::string& name) {
    if (name == "Enter") {
        postKey(st, name);
        finishLine(st);
        return;
    }
    if (name == "Backspace" && !st->partialLine.empty()) {
        st->partialLine.pop_back();
        if (echoLines(*st)) echoText("\b \b");
    }
    postKey(st, name);
}

void consumeEscape(PumpState* st, char c);

// One raw input byte on the pipe/file path.
void consumeByte(PumpState* st, char c) {
    if (!st->escape.empty()) {
        consumeEscape(st, c);
        return;
    }
    if (c == '\x1b') {
        st->escape = "\x1b";
        st->lastWasCR = false;
        return;
    }
    if (c == '\r') {
        st->lastWasCR = true;
        finishLine(st);
        return;
    }
    if (c == '\n') {
        if (st->lastWasCR) {  // a CRLF pair is one terminator
            st->lastWasCR = false;
            return;
        }
        finishLine(st);
        return;
    }
    st->lastWasCR = false;
    if (c == '\x7f' || c == '\x08') {
        noteNamed(st, "Backspace");
        return;
    }
    if (c == '\t') {
        postKey(st, "Tab");
        return;
    }
    const unsigned char byte = static_cast<unsigned char>(c);
    if (byte >= 0x20) noteChar(st, std::string(1, c));
}

// The byte after ESC: `[`/`O` opens a CSI/SS3 sequence; anything else closes a
// lone ESC as the "Escape" key. A lone ESC at a chunk end waits for the next
// byte (or EOF): the pipe path has no timing information to decide earlier.
void consumeEscape(PumpState* st, char c) {
    if (st->escape.size() == 1) {
        if (c == '[' || c == 'O') {
            st->escape += c;
            return;
        }
        st->escape.clear();
        postKey(st, "Escape");
        consumeByte(st, c);
        return;
    }
    const char final = c;
    st->escape.clear();
    if (final == 'A') postKey(st, "ArrowUp");
    else if (final == 'B') postKey(st, "ArrowDown");
    else if (final == 'C') postKey(st, "ArrowRight");
    else if (final == 'D') postKey(st, "ArrowLeft");
    // Any other final byte (Home/End, function keys, ...) is outside the
    // documented key vocabulary: consumed and ignored.
}

void finishEof(PumpState* st) {
    if (!st->escape.empty()) {
        st->escape.clear();
        postKey(st, "Escape");
    }
    if (!st->partialLine.empty()) finishLine(st);  // trailing line, no newline
    postEnd(st);
}

#if defined(_WIN32)
// --- Windows console (key records) -------------------------------------------

std::string namedKey(WORD code) {
    switch (code) {
        case VK_UP:
            return "ArrowUp";
        case VK_DOWN:
            return "ArrowDown";
        case VK_LEFT:
            return "ArrowLeft";
        case VK_RIGHT:
            return "ArrowRight";
        case VK_RETURN:
            return "Enter";
        case VK_ESCAPE:
            return "Escape";
        case VK_TAB:
            return "Tab";
        case VK_BACK:
            return "Backspace";
        default:
            return std::string();
    }
}

std::string utf8FromUnit(wchar_t unit) {
    if (unit >= 1 && unit < 0x80) return std::string(1, static_cast<char>(unit));
    char buffer[8] = {0};
    const int written =
        WideCharToMultiByte(CP_UTF8, 0, &unit, 1, buffer,
                            static_cast<int>(sizeof(buffer)), nullptr, nullptr);
    if (written <= 0) return std::string();
    return std::string(buffer, static_cast<std::size_t>(written));
}

void readerConsole(PumpState* st) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    for (;;) {
        if (!waitForDemand(*st)) return;
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            enterRawMode(*st);
        }
        INPUT_RECORD record {};
        DWORD read = 0;
        if (in == nullptr || in == INVALID_HANDLE_VALUE ||
            !ReadConsoleInputW(in, &record, 1, &read) || read == 0) {
            postEnd(st);
            return;
        }
        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            continue;
        }
        const std::string named =
            namedKey(record.Event.KeyEvent.wVirtualKeyCode);
        if (!named.empty()) {
            noteNamed(st, named);
            continue;
        }
        const std::string ch =
            utf8FromUnit(record.Event.KeyEvent.uChar.UnicodeChar);
        if (!ch.empty()) noteChar(st, ch);
    }
}

// --- Windows pipe/file (raw bytes) -------------------------------------------

void readerPipe(PumpState* st) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (in == nullptr || in == INVALID_HANDLE_VALUE) {
        postEnd(st);
        return;
    }
    char buffer[256];
    for (;;) {
        if (!waitForDemand(*st)) return;
        DWORD read = 0;
        if (!ReadFile(in, buffer, static_cast<DWORD>(sizeof(buffer)), &read,
                      nullptr) ||
            read == 0) {
            finishEof(st);
            return;
        }
        for (DWORD i = 0; i < read; ++i) consumeByte(st, buffer[i]);
    }
}

bool stdinIsConsole() {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    return in != nullptr && in != INVALID_HANDLE_VALUE &&
           GetConsoleMode(in, &mode) != 0;
}
#else
// --- POSIX terminal / pipe ---------------------------------------------------

void readerPosix(PumpState* st) {
    char buffer[256];
    for (;;) {
        if (!waitForDemand(*st)) return;
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            enterRawMode(*st);
        }
        const ssize_t read = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (read <= 0) {
            finishEof(st);
            return;
        }
        for (ssize_t i = 0; i < read; ++i) consumeByte(st, buffer[i]);
    }
}

bool stdinIsConsole() { return isatty(STDIN_FILENO) != 0; }
#endif

}  // namespace

void readerMain(PumpState* st) {
#if defined(_WIN32)
    st->consoleInput = stdinIsConsole();
    if (st->consoleInput) readerConsole(st); else readerPipe(st);
#else
    st->consoleInput = stdinIsConsole();
    readerPosix(st);
#endif
}

}  // namespace cp3::runtime::console::detail
