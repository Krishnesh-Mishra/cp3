// =============================================================================
// cp3/std/readline.hpp - `std/readline` line reader (ledger L).
//
// OWNERSHIP: the std/readline family (src/std/readline/**). Part of the
// generated-code ABI.
//
// ---------------------------------------------------------------------------
// CODEGEN CONTRACT
// ---------------------------------------------------------------------------
//   readline.readLine()    -> ::cp3::runtime::readline::readLine()
//                             (Promise<string>; "" on EOF)
//   readline.readLines()   -> ::cp3::runtime::readline::readLines()
//                             (Promise<string[]>; all lines to EOF)
//   readline.readAll()     -> ::cp3::runtime::readline::readAll()
//                             (Promise<string>; all bytes to EOF)
//   readline.createReader()-> ::cp3::runtime::readline::createReader()
//                             (LineReader; see below)
//   reader.readLine()      -> LineReader::readLine()   (Promise<string>)
//   reader.readLines()     -> LineReader::readLines()  (Promise<string[]>)
//   reader.on("line"|"close", cb) -> LineReader::on(event, cb)
//   reader.close()         -> LineReader::close()
//
// ---------------------------------------------------------------------------
// SEMANTICS
// ---------------------------------------------------------------------------
//   * All reads delegate to the process family's stdin pump
//     (cp3/std/process.hpp, `process.stdin`): one sibling reader thread,
//     one buffered line queue shared with `process.stdin`. Lines are split
//     on '\n' with a trailing '\r' stripped, so "\r\n" is one terminator.
//   * readLine resolves with the next line, terminator removed; "" on EOF
//     (never rejects). Successive calls never lose bytes.
//   * readLines / readAll resolve only at EOF. The loop is retained while
//     the pump is alive, so awaiting them keeps the program running.
//   * LineReader::on("line", cb) is an observation tap (never consumes a
//     line a pending readLine also sees); on("close", cb) fires once at
//     EOF with "". A listener may take the payload or none.
//   * LineReader::close() detaches that reader's listeners and makes its
//     later readLine calls resolve "" immediately; it does not close stdin
//     for other consumers.
//   * Mixing readline with console.input()/readKey() in one program races:
//     each family owns its own stdin pump (see Limitations in the ledger
//     report). Use one stdin consumer per program.
// =============================================================================
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cp3/async/promise.hpp"
#include "cp3/core/value.hpp"
#include "cp3/std/process.hpp"

namespace cp3::runtime::readline {

// Module-level one-shot reads (inline: they are the process stdin reads).
inline Promise<std::string> readLine() { return process::stdinReadLine(); }

inline Promise<Array<std::string>> readLines() {
    return process::stdinReadLines();
}

inline Promise<std::string> readAll() { return process::stdinReadAll(); }

// One line reader over the shared stdin pump. Copyable (the closed flag is
// shared); pimpl-free: the state is a single shared flag.
class LineReader {
public:
    LineReader() : closed_(std::make_shared<bool>(false)) {}

    Promise<std::string> readLine() {
        if (*closed_) return Promise<std::string>::resolve(std::string());
        return process::stdinReadLine();
    }

    Promise<Array<std::string>> readLines() {
        if (*closed_) {
            return Promise<Array<std::string>>::resolve(
                makeArray<std::string>(std::vector<std::string>{}));
        }
        return process::stdinReadLines();
    }

    /// `reader.on("line" | "close", cb)`: the underlying process listener
    /// is filtered by this reader's closed flag.
    template <class F>
    void on(const std::string& event, F&& listener) {
        auto closed = closed_;
        auto fn = process::detail::adaptStringListener(std::forward<F>(listener));
        if (event == "close") {
            process::stdinAddCloseListener(
                [closed, fn](std::string value) mutable {
                    if (!*closed) fn(std::move(value));
                });
        } else {
            process::stdinAddLineListener(
                [closed, fn](std::string value) mutable {
                    if (!*closed) fn(std::move(value));
                });
        }
    }

    void close() { *closed_ = true; }
    bool closed() const { return *closed_; }

private:
    std::shared_ptr<bool> closed_;
};

inline LineReader createReader() { return LineReader(); }

}  // namespace cp3::runtime::readline
