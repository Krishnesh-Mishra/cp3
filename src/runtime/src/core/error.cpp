// =============================================================================
// cp3/src/core/error.cpp - thrown-value boxing and unhandled-rejection reporting.
// =============================================================================
#include "cp3/core/error.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace cp3::runtime {

Thrown Thrown::fromCurrentException() {
    try {
        throw;
    } catch (const Thrown& thrown) {
        return thrown;
    } catch (const std::exception& ex) {
        return Thrown::make(std::string(ex.what()));
    } catch (...) {
        return Thrown::make(std::string("unknown exception"));
    }
}

std::string Thrown::toString() const {
    if (isEmpty()) {
        return "undefined";
    }
    if (isNull()) {
        return "null";
    }
    if (render_ != nullptr && box_) {
        return render_(box_.get());
    }
    return "<value>";
}

void fatalUnhandledRejection(const Thrown& reason) {
    // C stdio, not std::cerr: see the print/println note in core/runtime.cpp.
    // stderr is unbuffered by default and is flushed explicitly anyway, which
    // is exactly std::cerr's contract.
    const std::string line =
        "cp3: error: unhandled promise rejection: " + reason.toString() + '\n';
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
    std::exit(kFailureExitCode);
}

}  // namespace cp3::runtime
