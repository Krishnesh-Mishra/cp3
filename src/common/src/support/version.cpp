#include "cp3/meta/version.hpp"

namespace cp3 {

std::string versionString() {
    std::string s = kCompilerName;
    s += ' ';
    s += kVersion;
    if (kVersionStage[0] != '\0') {
        s += '-';
        s += kVersionStage;
    }
    return s;
}

}  // namespace cp3
