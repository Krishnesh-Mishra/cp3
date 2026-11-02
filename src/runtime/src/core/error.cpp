// =============================================================================
// cp3/src/core/error.cpp - thrown-value boxing and unhandled-rejection reporting.
// =============================================================================
#include "cp3/core/error.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace cp3::runtime {

Thrown Thrown::makeError(Ref<::cp3::Error> error) {
    if (!error) error = makeRef<::cp3::Error>();
    Thrown t;
    t.kind_ = Kind::Boxed;
    t.type_ = &typeid(Ref<::cp3::Error>);
    t.box_ = std::shared_ptr<void>(
        new Ref<::cp3::Error>(std::move(error)),
        [](void* p) { delete static_cast<Ref<::cp3::Error>*>(p); });
    t.render_ = [](const void* p) -> std::string {
        return (*static_cast<const Ref<::cp3::Error>*>(p))->toString();
    };
    // A thrown Error is a reference handle: copying the box copies the handle,
    // exactly like cp3's ordinary reference semantics. A worker boundary needs
    // a genuine copy, but an Error is immutable enough for the v0.2 surface;
    // sharing it keeps the dynamic subclass intact.
    t.copy_ = [](const void* p) -> std::shared_ptr<void> {
        const auto& value = *static_cast<const Ref<::cp3::Error>*>(p);
        return std::shared_ptr<void>(
            new Ref<::cp3::Error>(value),
            [](void* q) { delete static_cast<Ref<::cp3::Error>*>(q); });
    };
    return t;
}

Ref<::cp3::Error> Thrown::asError() const {
    if (const auto* boxed = get<Ref<::cp3::Error>>()) {
        if (*boxed) return *boxed;
    }
    // A native exception / legacy boxed value has no Error object yet. Give it
    // the closest honest representation instead of losing the message.
    std::string message = toString();
    if (message == "undefined" || message == "null" || message == "<value>") {
        message.clear();
    }
    return makeRef<::cp3::Error>(std::move(message));
}

Thrown Thrown::fromCurrentException() {
    try {
        throw;
    } catch (const Thrown& thrown) {
        return thrown;
    } catch (const ::cp3::Error& error) {
        return Thrown::makeError(makeRef<::cp3::Error>(error));
    } catch (const std::exception& ex) {
        return Thrown::makeError(makeRef<::cp3::Error>(std::string(ex.what())));
    } catch (...) {
        return Thrown::makeError(
            makeRef<::cp3::Error>(std::string("unknown exception")));
    }
}

std::string Thrown::toString() const {
    if (const auto* boxed = get<Ref<::cp3::Error>>()) {
        if (*boxed) return (*boxed)->toString();
    }
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
