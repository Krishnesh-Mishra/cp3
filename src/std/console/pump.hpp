// =============================================================================
// src/std/console/pump.hpp - PRIVATE: the stdin pump shared internals (D71/D75).
//
// OWNERSHIP: the std/console family (src/std/console/**). Internal to the two
// pump translation units - runtime_pump.cpp (state, delivery, the public
// members) and runtime_pump_read.cpp (the platform readers) - and deliberately
// kept out of the public ABI header cp3/std/console.hpp. The source-size budget
// is why the pair exists; nothing here is generated-code ABI.
// =============================================================================
#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "cp3/async/eventloop.hpp"
#include "cp3/async/promise.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <termios.h>
#endif

namespace cp3::runtime::console::detail {

// The process-lifetime pump state (the loop is process-lifetime too): the
// detached reader thread may still be inside a blocking read when the process
// ends, so this is deliberately never destroyed.
struct PumpState {
    std::mutex mutex;              // guards every field below
    EventLoop* loop = nullptr;     // the requester's loop (set once)
    bool started = false;          // the reader thread has been started
    bool eof = false;              // stdin reached EOF; "close" delivered
    bool retained = false;         // loop->retainExternal is held
    bool rawMode = false;          // the terminal sits in raw mode
    bool consoleInput = false;     // stdin is a real console (echo enabled)
#if defined(_WIN32)
    DWORD savedMode = 0;           // console mode before raw mode
#else
    termios savedTermios {};
#endif
    std::deque<Promise<std::string>> pendingLines;
    std::deque<Promise<std::string>> pendingKeys;
    std::deque<std::string> bufferedLines;
    std::deque<std::string> bufferedKeys;
    struct Listener {              // one registered `on` listener (D75)
        const void* typeTag = nullptr;  // its identity for `off`
        std::function<void(std::string)> fn;
    };
    std::vector<Listener> lineListeners;
    std::vector<Listener> keyListeners;
    std::vector<Listener> closeListeners;
    // Reader-thread-local parse state (no lock needed).
    std::string partialLine;
    std::string escape;            // pending ESC sequence, pipe path
    bool lastWasCR = false;
};

PumpState& pumpState();

// Raw mode only matters when stdin really is a terminal; on a pipe/file
// `GetConsoleMode` / `isatty` fails and these are no-ops.
void enterRawMode(PumpState& st);
void leaveRawMode(PumpState& st);

// Keep the loop alive (and the terminal raw) exactly while there is demand.
void refreshDemand(PumpState& st);

// The reader thread body (runtime_pump_read.cpp).
void readerMain(PumpState* st);

// Post one event to the loop from the reader thread.
void postLine(PumpState* st, std::string line);
void postKey(PumpState* st, std::string key);
void postEnd(PumpState* st);

// Block the reader exactly while there is no demand (and no EOF).
bool waitForDemand(PumpState& st);

}  // namespace cp3::runtime::console::detail
