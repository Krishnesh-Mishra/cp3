// =============================================================================
// src/cli/src/toolchain/toolchain_menu.cpp - terminal single-select input.
//
// OWNERSHIP: cli module (src/cli/**). Implements the menu half of
// detail/toolchain_select.hpp.
//
// A TTY gets an in-place redrawn list: arrow keys move, Enter selects, q or Esc
// cancels. A pipe gets a numbered prompt instead, so scripts and tests can
// drive the same flow (`echo 1 | cp3c toolchain`). No external dependency and
// no shell: Windows uses _getch, POSIX uses termios raw mode with a guaranteed
// restore in the destructor.
// =============================================================================
#include "detail/toolchain_select.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "detail/util.hpp"

#if defined(_WIN32)
#  include <conio.h>
#else
#  include <sys/select.h>
#  include <termios.h>
#  include <unistd.h>
#endif

namespace cp3::cli {

namespace {

constexpr int kKeyUp = -2;
constexpr int kKeyDown = -3;
constexpr int kKeyEnter = -4;
constexpr int kKeyCancel = -5;

std::string trimCopy(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string lowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(c)));
    return s;
}

// Restores the terminal even when the menu is left through early returns.
class RawTerminal {
public:
    RawTerminal() {
#if !defined(_WIN32)
        if (tcgetattr(STDIN_FILENO, &saved_) == 0) {
            termios raw = saved_;
            raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            active_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
        }
#endif
    }
    ~RawTerminal() {
#if !defined(_WIN32)
        if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
#endif
    }
    RawTerminal(const RawTerminal&) = delete;
    RawTerminal& operator=(const RawTerminal&) = delete;

private:
#if !defined(_WIN32)
    termios saved_{};
    bool active_ = false;
#endif
};

#if defined(_WIN32)
int readKey() {
    const int first = _getch();
    if (first == 0 || first == 224) {
        const int second = _getch();
        if (second == 72) return kKeyUp;
        if (second == 80) return kKeyDown;
        return -1;
    }
    if (first == '\r' || first == '\n') return kKeyEnter;
    if (first == 27 || first == 'q' || first == 'Q') return kKeyCancel;
    return first;
}
#else
int readKey() {
    const int first = std::fgetc(stdin);
    if (first == EOF) return kKeyCancel;
    if (first == 27) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(STDIN_FILENO, &readable);
        timeval timeout{};
        timeout.tv_usec = 50000;
        if (select(STDIN_FILENO + 1, &readable, nullptr, nullptr, &timeout) <=
            0) {
            return kKeyCancel;
        }
        const int second = std::fgetc(stdin);
        if (second != '[') return kKeyCancel;
        const int third = std::fgetc(stdin);
        if (third == 'A') return kKeyUp;
        if (third == 'B') return kKeyDown;
        return -1;
    }
    if (first == '\r' || first == '\n') return kKeyEnter;
    if (first == 'q' || first == 'Q') return kKeyCancel;
    return first;
}
#endif

void drawMenu(const std::string& title, const std::vector<std::string>& rows,
              int selected) {
    std::printf("%s\n", title.c_str());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const bool active = static_cast<int>(i) == selected;
        std::printf("  %s %2zu. %s\n", active ? ">" : " ", i + 1,
                    rows[i].c_str());
    }
    std::printf("  (up/down arrows, Enter to select, q to cancel)\n");
    std::fflush(stdout);
}

int menuNumbered(const std::string& title, const std::vector<std::string>& rows,
                 const std::vector<std::string>& keys, bool& cancelled) {
    std::printf("%s\n", title.c_str());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        std::printf("  %2zu. %s\n", i + 1, rows[i].c_str());
    }
    std::printf("choice: ");
    std::fflush(stdout);
    std::string line;
    if (!std::getline(std::cin, line)) {
        std::printf("\n");
        cancelled = true;
        return -1;
    }
    std::printf("\n");
    const std::string answer = trimCopy(line);
    if (answer.empty()) {
        cancelled = true;
        return -1;
    }
    if (answer.find_first_not_of("0123456789") == std::string::npos) {
        char* end = nullptr;
        const long number = std::strtol(answer.c_str(), &end, 10);
        if (end != answer.c_str() && *end == '\0' && number >= 1 &&
            number <= static_cast<long>(rows.size())) {
            cancelled = false;
            return static_cast<int>(number - 1);
        }
    }
    for (std::size_t i = 0; i < keys.size() && i < rows.size(); ++i) {
        if (lowerCopy(keys[i]) == lowerCopy(answer)) {
            cancelled = false;
            return static_cast<int>(i);
        }
    }
    std::fprintf(stderr, "cp3c: invalid choice '%s'\n", answer.c_str());
    cancelled = false;
    return -1;
}

}  // namespace

int runMenu(const std::string& title, const std::vector<std::string>& rows,
            const std::vector<std::string>& keys, int initial,
            bool& cancelled) {
    cancelled = false;
    if (rows.empty()) {
        cancelled = true;
        return -1;
    }
    if (!isTty(stdin)) return menuNumbered(title, rows, keys, cancelled);

    RawTerminal raw;
    int selected = initial;
    if (selected < 0 || selected >= static_cast<int>(rows.size())) {
        selected = 0;
    }
    int drawn = 0;
    for (;;) {
        if (drawn > 0) {
            std::printf("\x1b[%dA\x1b[0J", drawn);
        }
        drawMenu(title, rows, selected);
        drawn = static_cast<int>(rows.size()) + 2;
        const int key = readKey();
        if (key == kKeyEnter) {
            std::printf("\n");
            return selected;
        }
        if (key == kKeyCancel) {
            std::printf("\n");
            cancelled = true;
            return -1;
        }
        if (key == kKeyUp) {
            selected = (selected + static_cast<int>(rows.size()) - 1) %
                       static_cast<int>(rows.size());
        } else if (key == kKeyDown) {
            selected = (selected + 1) % static_cast<int>(rows.size());
        }
    }
}

bool runYesNo(const std::string& question) {
    bool cancelled = false;
    const int choice = runMenu(question, {"Yes", "No"}, {"yes", "no", "y", "n"},
                               0, cancelled);
    return choice == 0;
}

}  // namespace cp3::cli
