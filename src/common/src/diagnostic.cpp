#include "cp3/diagnostic.hpp"

namespace cp3 {

const char* severityName(Severity s) noexcept {
    switch (s) {
        case Severity::Note:    return "note";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
        case Severity::Fatal:   return "fatal";
    }
    return "error";
}

}  // namespace cp3
