// =============================================================================
// src/std/fs/runtime.cpp - `std/fs` implementation (cp3/std/fs.hpp).
//
// OWNERSHIP: the std/fs family (src/std/fs/**). One TU, compiled and linked only
// when a program imports "std/fs" (src/std/fs/native.hpp, D68).
//
// Backend: the C++ standard library only (`std::filesystem` + `std::fstream`),
// so the family carries no vendored dependency and no extra link flags. Every
// failure surfaces as `Thrown::make(<plain message>)`: the sync functions throw
// it, the async functions are coroutines whose `unhandled_exception` turns the
// same throw into a rejection, and the streams report it through "error" (or
// throw from the pump when no listener is attached).
//
// The read pump (ReadStreamImpl) is genuinely chunked: one binary read per
// event-loop microtask, started at the first `on(...)` registration. The write
// side opens the destination lazily and writes synchronously, so `end()` is a
// flush + close.
// =============================================================================
#include "cp3/std/fs.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cp3/async/eventloop.hpp"

namespace cp3::runtime::fs {

namespace {

constexpr std::size_t kDefaultChunkSize = 64u * 1024u;

[[noreturn]] void fail(const std::string& message) { throw Thrown::make(message); }

/// The cp3 string is UTF-8; `std::filesystem::path` from a `u8string` keeps it
/// byte-exact on every platform (the plain narrow ctor would use the ANSI code
/// page on Windows).
std::filesystem::path toPath(const std::string& path) {
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

}  // namespace

// =============================================================================
// Stream state
// =============================================================================
namespace detail {

struct ReadStreamImpl {
    ReadStreamImpl(std::string p, std::size_t c)
        : path(std::move(p)), chunkSize(c == 0 ? kDefaultChunkSize : c) {}

    std::string path;
    std::size_t chunkSize;
    std::ifstream in;
    bool started = false;
    bool finished = false;
    bool endDelivered = false;
    bool failed = false;
    std::function<void(std::string)> onData;
    std::function<void()> onEnd;
    std::function<void(std::string)> onError;
};

struct WriteStreamImpl {
    explicit WriteStreamImpl(std::string p) : path(std::move(p)) {}

    std::string path;
    std::ofstream out;
    bool opened = false;
    bool closed = false;
    bool failed = false;
    std::function<void(std::string)> onError;
};

}  // namespace detail

// =============================================================================
// ReadStream internals
// =============================================================================
namespace {

/// Reports a read failure: an "error" listener receives the message, otherwise
/// the `Thrown` escapes (from the pump microtask, so it aborts the program the
/// way an uncaught Node 'error' event would).
void deliverReadError(const std::shared_ptr<detail::ReadStreamImpl>& impl,
                      const std::string& message) {
    impl->failed = true;
    if (impl->onError) {
        impl->onError(message);
        return;
    }
    throw Thrown::make(message);
}

void deliverEnd(const std::shared_ptr<detail::ReadStreamImpl>& impl) {
    if (!impl->finished || impl->endDelivered || !impl->onEnd) return;
    impl->endDelivered = true;
    impl->onEnd();
}

/// One chunk: read, hand "data" the bytes, then either deliver "end" (EOF) or
/// post the next pump turn so the loop is never blocked on file I/O.
void pumpRead(const std::shared_ptr<detail::ReadStreamImpl>& impl) {
    if (impl->failed || impl->finished) return;
    std::string chunk(impl->chunkSize, '\0');
    impl->in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const std::streamsize got = impl->in.gcount();
    if (impl->in.bad()) {
        deliverReadError(impl, "EIO: read failed, '" + impl->path + "'");
        return;
    }
    if (got <= 0) {
        impl->finished = true;
        // Release the handle before "end" listeners run: Node has closed the fd
        // by then, and on Windows an end handler that unlinks the file or
        // reopens it for writing would otherwise hit a sharing violation.
        if (impl->in.is_open()) impl->in.close();
        deliverEnd(impl);
        return;
    }
    chunk.resize(static_cast<std::size_t>(got));
    if (impl->onData) impl->onData(chunk);
    currentLoop().post([impl]() { pumpRead(impl); });
}

/// Opens the file on the first `on(...)` registration and posts the first pump
/// turn. The open failure is posted too, so a listener attached later in the
/// same synchronous block (the documented pattern) still observes it.
void startRead(const std::shared_ptr<detail::ReadStreamImpl>& impl) {
    if (impl->started) return;
    impl->started = true;
    impl->in.open(toPath(impl->path), std::ios::binary);
    if (!impl->in) {
        const std::string message = "ENOENT: no such file, open '" + impl->path + "'";
        currentLoop().post([impl, message]() { deliverReadError(impl, message); });
        return;
    }
    currentLoop().post([impl]() { pumpRead(impl); });
}

}  // namespace

ReadStream::ReadStream(std::shared_ptr<detail::ReadStreamImpl> impl)
    : impl_(std::move(impl)) {}

void ReadStream::on(const std::string& event,
                    std::function<void(Chunk)> handler) const {
    if (event == "data") {
        impl_->onData = std::move(handler);
    } else if (event == "error") {
        impl_->onError = std::move(handler);
    } else if (event == "end") {
        impl_->onEnd = [handler = std::move(handler)]() { handler(std::string()); };
    } else {
        fail("readFileStream: unknown event '" + event + "'");
    }
    startRead(impl_);
    if (event == "end") deliverEnd(impl_);
}

void ReadStream::on(const std::string& event, std::function<void()> handler) const {
    if (event == "data") {
        impl_->onData = [handler = std::move(handler)](std::string) { handler(); };
    } else if (event == "error") {
        impl_->onError = [handler = std::move(handler)](std::string) { handler(); };
    } else if (event == "end") {
        impl_->onEnd = std::move(handler);
    } else {
        fail("readFileStream: unknown event '" + event + "'");
    }
    startRead(impl_);
    if (event == "end") deliverEnd(impl_);
}

ReadStream readFileStream(const std::string& path, double chunkSize) {
    std::size_t size = 0;
    if (chunkSize > 0.0) size = static_cast<std::size_t>(chunkSize);
    return ReadStream(std::make_shared<detail::ReadStreamImpl>(path, size));
}

// =============================================================================
// WriteStream internals
// =============================================================================
namespace {

void deliverWriteError(const std::shared_ptr<detail::WriteStreamImpl>& impl,
                       const std::string& message) {
    impl->failed = true;
    if (impl->onError) {
        impl->onError(message);
        return;
    }
    throw Thrown::make(message);
}

/// Lazily open (truncate) the destination at the first write/end.
void ensureWriteOpen(const std::shared_ptr<detail::WriteStreamImpl>& impl) {
    if (impl->opened || impl->failed) return;
    impl->opened = true;
    impl->out.open(toPath(impl->path), std::ios::binary | std::ios::trunc);
    if (!impl->out) {
        deliverWriteError(impl,
                          "ENOENT: no such file or directory, open '" + impl->path + "'");
    }
}

void writeChunk(const std::shared_ptr<detail::WriteStreamImpl>& impl,
                const std::string& chunk) {
    ensureWriteOpen(impl);
    if (impl->failed) return;
    if (impl->closed) {
        deliverWriteError(impl, "ERR_STREAM_WRITE_AFTER_END: '" + impl->path + "'");
        return;
    }
    impl->out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    impl->out.flush();
    if (!impl->out) {
        deliverWriteError(impl, "EIO: write failed, '" + impl->path + "'");
    }
}

void endWrite(const std::shared_ptr<detail::WriteStreamImpl>& impl) {
    ensureWriteOpen(impl);
    if (impl->failed || impl->closed) return;
    impl->closed = true;
    impl->out.close();
    if (!impl->out) {
        deliverWriteError(impl, "EIO: close failed, '" + impl->path + "'");
    }
}

}  // namespace

WriteStream::WriteStream(std::shared_ptr<detail::WriteStreamImpl> impl)
    : impl_(std::move(impl)) {}

void WriteStream::write(const std::string& chunk) const { writeChunk(impl_, chunk); }

void WriteStream::end() const { endWrite(impl_); }

void WriteStream::on(const std::string& event,
                     std::function<void(std::string)> handler) const {
    if (event != "error") fail("writeFileStream: unknown event '" + event + "'");
    impl_->onError = std::move(handler);
}

void WriteStream::on(const std::string& event, std::function<void()> handler) const {
    if (event != "error") fail("writeFileStream: unknown event '" + event + "'");
    impl_->onError = [handler = std::move(handler)](std::string) { handler(); };
}

WriteStream writeFileStream(const std::string& path) {
    return WriteStream(std::make_shared<detail::WriteStreamImpl>(path));
}

// =============================================================================
// Sync surface
// =============================================================================
std::string readFileSync(const std::string& path, std::string encoding) {
    (void)encoding;  // SEAM (encoding): utf8 only; see cp3/std/fs.hpp.
    std::ifstream in(toPath(path), std::ios::binary);
    if (!in) fail("ENOENT: no such file, open '" + path + "'");
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (in.bad()) fail("EIO: read failed, '" + path + "'");
    return data;
}

void writeFileSync(const std::string& path, const std::string& data) {
    std::ofstream out(toPath(path), std::ios::binary | std::ios::trunc);
    if (!out) fail("ENOENT: no such file or directory, open '" + path + "'");
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) fail("EIO: write failed, '" + path + "'");
}

void appendFileSync(const std::string& path, const std::string& data) {
    std::ofstream out(toPath(path), std::ios::binary | std::ios::app);
    if (!out) fail("ENOENT: no such file or directory, open '" + path + "'");
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) fail("EIO: write failed, '" + path + "'");
}

bool existsSync(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(toPath(path), ec);
}

void mkdirSync(const std::string& path, bool recursive) {
    std::error_code ec;
    if (recursive) {
        std::filesystem::create_directories(toPath(path), ec);
        if (ec) fail("EACCES: mkdir failed, '" + path + "': " + ec.message());
        return;
    }
    const bool created = std::filesystem::create_directory(toPath(path), ec);
    if (ec) fail("EACCES: mkdir failed, '" + path + "': " + ec.message());
    if (!created) fail("EEXIST: file already exists, mkdir '" + path + "'");
}

Array<std::string> readdirSync(const std::string& path) {
    std::error_code ec;
    std::filesystem::directory_iterator it(toPath(path), ec);
    if (ec) fail("ENOENT: no such file or directory, scandir '" + path + "'");
    std::vector<std::string> names;
    for (const std::filesystem::directory_entry& entry : it) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());  // docs leave the order unspecified
    return makeArray(std::move(names));
}

FileStats statSync(const std::string& path) {
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::status(toPath(path), ec);
    if (ec || !std::filesystem::exists(st)) {
        fail("ENOENT: no such file or directory, stat '" + path + "'");
    }
    FileStats out;
    out.isFile = std::filesystem::is_regular_file(st);
    out.isDirectory = std::filesystem::is_directory(st);
    if (out.isFile) {
        std::error_code sizeEc;
        const std::uintmax_t size = std::filesystem::file_size(toPath(path), sizeEc);
        if (!sizeEc) out.size = static_cast<double>(size);
    }
    return out;
}

void unlinkSync(const std::string& path) {
    std::error_code ec;
    // `std::filesystem::remove` also removes empty directories; `unlink` must
    // not (Node raises EISDIR/EPERM), so directories are rejected up front.
    if (std::filesystem::is_directory(toPath(path), ec)) {
        fail("EPERM: operation not permitted, unlink '" + path + "'");
    }
    ec.clear();
    const bool removed = std::filesystem::remove(toPath(path), ec);
    if (ec) fail("EACCES: unlink failed, '" + path + "': " + ec.message());
    if (!removed) fail("ENOENT: no such file or directory, unlink '" + path + "'");
}

// =============================================================================
// Async surface: the sync bodies in coroutines, so a throw rejects
// =============================================================================
Promise<std::string> readFile(const std::string& path, std::string encoding) {
    co_return readFileSync(path, std::move(encoding));
}

Promise<void> writeFile(const std::string& path, const std::string& data) {
    writeFileSync(path, data);
    co_return;
}

Promise<void> appendFile(const std::string& path, const std::string& data) {
    appendFileSync(path, data);
    co_return;
}

Promise<void> mkdir(const std::string& path, bool recursive) {
    mkdirSync(path, recursive);
    co_return;
}

Promise<Array<std::string>> readdir(const std::string& path) {
    co_return readdirSync(path);
}

Promise<FileStats> stat(const std::string& path) { co_return statSync(path); }

Promise<void> unlink(const std::string& path) {
    unlinkSync(path);
    co_return;
}

}  // namespace cp3::runtime::fs
