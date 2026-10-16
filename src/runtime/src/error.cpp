// =============================================================================
// cp3/src/error.cpp — thrown-value boxing and unhandled-rejection reporting.
// =============================================================================
#include "cp3/error.hpp"

#include <cstdlib>
#include <iostream>
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
    std::cerr << "cp3: error: unhandled promise rejection: " << reason.toString() << '\n';
    std::cerr.flush();
    std::exit(kFailureExitCode);
}

}  // namespace cp3::runtime
