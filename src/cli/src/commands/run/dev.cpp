// =============================================================================
// src/cli/src/commands/run/dev.cpp - `cp3c dev` (watch + build + restart).
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdDev.
//
// Watches every file in the module graph - the entry, every local import,
// every injected prelude and the project's package.json - using OS file
// events (ReadDirectoryChangesW on Windows, inotify on Linux, kqueue on
// macOS) with a portable stat-polling fallback. A rebuild is scheduled once
// per edit burst (25 ms quiet window) and confirmed by a content hash, so a
// no-change save never restarts the app. The OLD child app keeps running
// during a rebuild; it is restarted only when the new executable is ready
// (build succeeded and the exe is newer, or nothing is running). On a
// compile error the last good app stays alive. `--once` performs one
// build/run pass and exits when the program does.
// =============================================================================
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(__linux__)
#  include <fcntl.h>
#  include <sys/inotify.h>
#  include <sys/poll.h>
#  include <unistd.h>
#elif defined(__APPLE__)
#  include <fcntl.h>
#  include <sys/event.h>
#  include <sys/time.h>
#  include <time.h>
#  include <unistd.h>
#endif

#include "cp3/core/source.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/meta/module.hpp"

#include "detail/cache/cache.hpp"
#include "detail/core/commands.hpp"
#include "detail/core/util.hpp"
#include "detail/toolchain/pipeline.hpp"
#include "detail/toolchain/incremental.hpp"

namespace cp3::cli {
namespace {

constexpr std::chrono::milliseconds kDebounceWindow{25};
constexpr std::chrono::milliseconds kPollInterval{50};
constexpr std::chrono::milliseconds kEventTimeout{50};
constexpr std::size_t kMaxWatchDirs = 60;
constexpr std::size_t kEventBufferSize = 16384;

volatile std::sig_atomic_t g_devStopRequested = 0;

void requestDevStop(int) { g_devStopRequested = 1; }

class ScopedDevSignalHandler {
public:
    ScopedDevSignalHandler() {
        previous_ = std::signal(SIGINT, requestDevStop);
#if !defined(_WIN32)
        previousTerm_ = std::signal(SIGTERM, requestDevStop);
#else
        previousTerm_ = SIG_ERR;
#endif
    }
    ~ScopedDevSignalHandler() {
        std::signal(SIGINT, previous_);
#if !defined(_WIN32)
        if (previousTerm_ != SIG_ERR) std::signal(SIGTERM, previousTerm_);
#endif
    }

    ScopedDevSignalHandler(const ScopedDevSignalHandler&) = delete;
    ScopedDevSignalHandler& operator=(const ScopedDevSignalHandler&) = delete;

private:
    void (*previous_)(int);
    void (*previousTerm_)(int);
};

// ---------------------------------------------------------------------------
// Watched set: the entry, the module graph's units and the project's
// package.json, as normalized absolute paths plus lookup keys (lower-cased on
// Windows) and the distinct parent directories that event backends watch.
// ---------------------------------------------------------------------------
struct DevWatchSet {
    std::vector<fs::path> files;              // absolute, lexically_normal
    std::unordered_set<std::string> keySet;   // normalized comparison keys
    std::vector<fs::path> directories;        // distinct parent directories
};

// Normalizes a path for set comparisons: absolute + lexically normal, and
// lower-cased on Windows (the filesystem there is case-insensitive). The
// result is a generic-string key.
std::string normalizeWatchKey(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) absolute = path;
    absolute = absolute.lexically_normal();
    std::string key = absolute.generic_string();
#if defined(_WIN32)
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}

// Normalizes a path the same way, but returns the path itself.
fs::path normalizeWatchPath(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) absolute = path;
    return absolute.lexically_normal();
}

// The set of files the dev watcher cares about: the entry, the package.json of
// the nearest project root, and every file the module graph loader registers
// (local imports and injected preludes included). Graph load failures are not
// fatal; the entry and manifest are always watched.
DevWatchSet collectWatchedSet(const fs::path& entry) {
    DevWatchSet out;
    const auto addFile = [&out](const fs::path& path) {
        const fs::path normalized = normalizeWatchPath(path);
        const std::string key = normalizeWatchKey(normalized);
        if (out.keySet.insert(key).second) {
            out.files.push_back(normalized);
        }
    };
    addFile(entry);

    std::error_code ec;
    const fs::path manifest = findProjectRoot(entry) / "package.json";
    if (fs::is_regular_file(manifest, ec)) addFile(manifest);

    std::string err;
    SourceManager sm;
    SourceFile* file = nullptr;
    if (loadEntrySource(entry.string(), sm, file, err)) {
        CollectingDiagnosticSink sink;  // diagnostics are not rendered here
        std::unique_ptr<ModuleGraph> graph = loadModuleGraph(sm, *file, sink);
        if (graph) {
            for (const CompilationUnit* unit : graph->units()) {
                if (unit == nullptr || unit->file == nullptr) continue;
                const std::string& path = unit->file->canonicalPath.empty()
                                              ? unit->file->path
                                              : unit->file->canonicalPath;
                if (!path.empty()) addFile(fs::path(path));
            }
        }
    }

    std::unordered_set<std::string> dirKeys;
    for (const fs::path& file : out.files) {
        const fs::path dir = normalizeWatchPath(file.parent_path());
        const std::string key = normalizeWatchKey(dir);
        if (dirKeys.insert(key).second) out.directories.push_back(dir);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Fingerprints: a cheap stat signature (existence, size, mtime) and a content
// hash (FNV-1a of the bytes) for the debounce-quiet confirmation. A
// no-content-change save never schedules a rebuild.
// ---------------------------------------------------------------------------
struct StatFingerprint {
    bool exists = false;
    std::uintmax_t size = 0;
    fs::file_time_type mtime{};
};

std::vector<StatFingerprint> statFingerprint(const DevWatchSet& set) {
    std::vector<StatFingerprint> out;
    out.reserve(set.files.size());
    for (const fs::path& file : set.files) {
        std::error_code ec;
        StatFingerprint fp;
        fp.exists = fs::is_regular_file(file, ec);
        if (fp.exists) {
            fp.size = fs::file_size(file, ec);
            if (ec) fp.size = 0;
            fp.mtime = fs::last_write_time(file, ec);
            if (ec) fp.exists = false;  // unreadable stats count as changed
        }
        out.push_back(fp);
    }
    return out;
}

bool statMatches(const std::vector<StatFingerprint>& a,
                 const std::vector<StatFingerprint>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].exists != b[i].exists) return false;
        if (!a[i].exists) continue;
        if (a[i].size != b[i].size || a[i].mtime != b[i].mtime) return false;
    }
    return true;
}

struct ContentEntry {
    bool exists = false;
    std::uint64_t hash = 0;
    std::uintmax_t size = 0;
};

std::vector<ContentEntry> contentFingerprint(const DevWatchSet& set) {
    std::vector<ContentEntry> out;
    out.reserve(set.files.size());
    for (const fs::path& file : set.files) {
        std::string text;
        if (!readTextFile(file, text)) {
            out.push_back(ContentEntry{});  // unreadable file counts as changed
            continue;
        }
        ContentEntry entry;
        entry.exists = true;
        entry.hash = fnv1a64(text);
        entry.size = text.size();
        out.push_back(entry);
    }
    return out;
}

bool contentMatches(const std::vector<ContentEntry>& a,
                    const std::vector<ContentEntry>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].exists != b[i].exists) return false;
        if (a[i].exists && (a[i].hash != b[i].hash || a[i].size != b[i].size)) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// OS file-event watcher. waitForActivity() blocks up to the given timeout and
// reports whether a change matching the watched set was observed. The class is
// all-or-nothing: if any watch cannot be established, open() returns false and
// the caller falls back to stat polling. After a fatal runtime error (buffer
// overflow, watched directory removed) healthy() turns false and the caller
// switches to polling permanently; events already in flight are then covered by
// the fingerprint comparison the caller performs after every rebuild.
// ---------------------------------------------------------------------------
class FileEventWatcher {
public:
    FileEventWatcher() = default;
    ~FileEventWatcher() { close(); }

    FileEventWatcher(const FileEventWatcher&) = delete;
    FileEventWatcher& operator=(const FileEventWatcher&) = delete;

    bool open(const DevWatchSet& set);
    bool waitForActivity(std::chrono::milliseconds timeout);
    bool healthy() const noexcept { return healthy_; }
    void close();

private:
    bool isWatchedKey(const std::string& key) const {
        return keys_.find(key) != keys_.end();
    }
    void adopt(const DevWatchSet& set) {
        files_ = set.files;
        keys_ = set.keySet;
        directories_ = set.directories;
    }

#if defined(_WIN32)
    struct DirWatch {
        HANDLE handle = INVALID_HANDLE_VALUE;
        HANDLE event = nullptr;
        OVERLAPPED overlapped{};
        std::vector<char> buffer;
        fs::path dir;
    };
    std::vector<DirWatch> watches_;
    bool armDirWatch(DirWatch& watch);
#elif defined(__linux__)
    int fd_ = -1;
    std::unordered_map<int, std::string> watchDirs_;  // watch descriptor -> dir
#elif defined(__APPLE__)
    struct KqueueWatch {
        int fd = -1;
    };
    int kq_ = -1;
    std::vector<KqueueWatch> kwatches_;
    bool reopenKqueueWatches();
    bool armKqueueWatch(const fs::path& file);
#endif

    std::vector<fs::path> files_;              // normalized watched files
    std::vector<fs::path> directories_;        // normalized watched dirs
    std::unordered_set<std::string> keys_;     // normalized comparison keys
    bool healthy_ = false;
};

#if defined(_WIN32)

bool FileEventWatcher::armDirWatch(DirWatch& watch) {
    watch.buffer.assign(kEventBufferSize, 0);
    const std::wstring dirName = watch.dir.wstring();
    HANDLE handle =
        CreateFileW(dirName.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    HANDLE event = CreateEventW(nullptr, TRUE /* manual reset */, FALSE, nullptr);
    if (event == nullptr) {
        CloseHandle(handle);
        return false;
    }
    watch.handle = handle;
    watch.event = event;
    watch.overlapped = OVERLAPPED{};
    watch.overlapped.hEvent = event;
    const BOOL armed = ReadDirectoryChangesW(
        watch.handle, watch.buffer.data(), static_cast<DWORD>(watch.buffer.size()),
        FALSE /* watch this directory only */,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_CREATION,
        nullptr, &watch.overlapped, nullptr);
    if (!armed) {
        CloseHandle(watch.event);
        CloseHandle(watch.handle);
        watch.handle = INVALID_HANDLE_VALUE;
        watch.event = nullptr;
        return false;
    }
    return true;
}

bool FileEventWatcher::open(const DevWatchSet& set) {
    close();
    if (set.files.empty() || set.directories.empty()) return false;
    if (set.directories.size() > kMaxWatchDirs) return false;  // poll fallback
    adopt(set);
    bool ok = true;
    for (const fs::path& dir : set.directories) {
        DirWatch watch;
        watch.dir = dir;
        if (!armDirWatch(watch)) {
            ok = false;
            break;
        }
        watches_.push_back(std::move(watch));
    }
    if (!ok) {
        close();
        return false;
    }
    healthy_ = true;
    return true;
}

bool FileEventWatcher::waitForActivity(std::chrono::milliseconds timeout) {
    if (watches_.empty()) return false;
    std::vector<HANDLE> handles;
    handles.reserve(watches_.size());
    for (const DirWatch& watch : watches_) handles.push_back(watch.event);
    const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                handles.data(), FALSE,
                                                static_cast<DWORD>(timeout.count()));
    if (result == WAIT_TIMEOUT) return false;
    if (result == WAIT_FAILED) {
        healthy_ = false;
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(result - WAIT_OBJECT_0);
    if (index >= watches_.size()) {
        healthy_ = false;
        return false;
    }
    DirWatch& watch = watches_[index];
    DWORD bytes = 0;
    if (!GetOverlappedResult(watch.handle, &watch.overlapped, &bytes, FALSE)) {
        // Overflow (ERROR_NOTIFY_ENUM_DIR) or a lost watch: degrade to polling.
        healthy_ = false;
        return true;  // something changed that we could not enumerate
    }
    // Copy the completed buffer, then re-arm the directory BEFORE parsing:
    // notifications that arrive while we read belong to the next window, so
    // no event can fall into the gap between completion and re-arm.
    if (bytes > watch.buffer.size()) bytes = static_cast<DWORD>(watch.buffer.size());
    std::vector<char> snapshot(watch.buffer.begin(),
                               watch.buffer.begin() + static_cast<std::ptrdiff_t>(bytes));
    ResetEvent(watch.event);
    const BOOL armed = ReadDirectoryChangesW(
        watch.handle, watch.buffer.data(), static_cast<DWORD>(watch.buffer.size()),
        FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_CREATION,
        nullptr, &watch.overlapped, nullptr);
    if (!armed) healthy_ = false;

    bool matched = false;
    if (!snapshot.empty()) {
        std::size_t offset = 0;
        for (;;) {
            // The header is three DWORDs followed by FileNameLength/2 WCHARs.
            // Read it with memcpy: the buffer is not guaranteed to be aligned
            // for reinterpret_cast and the OS is trusted only to the bounds.
            constexpr std::size_t kFixed = 12;  // nextOff + action + fileNameLen
            if (offset + kFixed > snapshot.size()) {
                healthy_ = false;
                return true;
            }
            DWORD nextOff = 0;
            DWORD nameLen = 0;
            std::memcpy(&nextOff, snapshot.data() + offset, 4);
            std::memcpy(&nameLen, snapshot.data() + offset + 8, 4);
            if (offset + kFixed + nameLen > snapshot.size()) {
                healthy_ = false;
                return true;
            }
            std::wstring name(reinterpret_cast<const wchar_t*>(
                                  snapshot.data() + offset + kFixed),
                              nameLen / sizeof(wchar_t));
            const std::string key =
                normalizeWatchKey(watch.dir / fs::path(name));
            if (isWatchedKey(key)) matched = true;
            if (nextOff == 0) break;
            if (nextOff < kFixed + nameLen) {
                healthy_ = false;  // corrupt chain: degrade rather than loop
                return true;
            }
            offset += nextOff;
            if (offset >= snapshot.size()) break;
        }
    }
    return matched;
}

void FileEventWatcher::close() {
    for (DirWatch& watch : watches_) {
        if (watch.event != nullptr) CloseHandle(watch.event);
        if (watch.handle != INVALID_HANDLE_VALUE) CloseHandle(watch.handle);
    }
    watches_.clear();
    files_.clear();
    directories_.clear();
    keys_.clear();
    healthy_ = false;
}

#elif defined(__linux__)

bool FileEventWatcher::open(const DevWatchSet& set) {
    close();
    if (set.files.empty() || set.directories.empty()) return false;
    int fd = inotify_init();
    if (fd < 0) return false;
    // The drain loop reads until EAGAIN, so the descriptor must be
    // non-blocking; if it cannot be made so, use the polling fallback.
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        ::close(fd);
        return false;
    }
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    fd_ = fd;
    adopt(set);
    for (const fs::path& dir : directories_) {
        const int wd = inotify_add_watch(
            fd_, dir.generic_string().c_str(),
            IN_MODIFY | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE |
                IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR);
        if (wd < 0) {
            close();
            return false;
        }
        watchDirs_.emplace(wd, dir.generic_string());
    }
    healthy_ = true;
    return true;
}

bool FileEventWatcher::waitForActivity(std::chrono::milliseconds timeout) {
    if (fd_ < 0) return false;
    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    const int polled = poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (polled <= 0) return false;  // timeout, interrupted, or poll failure
    bool matched = false;
    bool lostAll = false;
    alignas(struct inotify_event) char buffer[16384];
    for (;;) {
        const ssize_t readBytes = ::read(fd_, buffer, sizeof(buffer));
        if (readBytes <= 0) break;
        for (const char* p = buffer; p < buffer + readBytes;) {
            const auto* event = reinterpret_cast<const struct inotify_event*>(p);
            if ((event->mask & IN_Q_OVERFLOW) != 0) {
                matched = true;  // events were lost; force a defensive rebuild
            } else if ((event->mask & IN_IGNORED) != 0) {
                const auto it = watchDirs_.find(event->wd);
                if (it != watchDirs_.end()) {
                    watchDirs_.erase(it);
                    if (watchDirs_.empty()) lostAll = true;
                }
            } else if (event->len > 0 && event->name[0] != '\0') {
                const auto it = watchDirs_.find(event->wd);
                if (it != watchDirs_.end()) {
                    const fs::path joined =
                        fs::path(it->second) / fs::path(event->name);
                    if (isWatchedKey(normalizeWatchKey(joined))) matched = true;
                }
            }
            p += sizeof(struct inotify_event) + event->len;
        }
    }
    if (lostAll) healthy_ = false;
    return matched;
}

void FileEventWatcher::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    watchDirs_.clear();
    files_.clear();
    directories_.clear();
    keys_.clear();
    healthy_ = false;
}

#elif defined(__APPLE__)

bool FileEventWatcher::armKqueueWatch(const fs::path& file) {
    KqueueWatch watch;
#if defined(O_EVTONLY)
    const int oflags = O_EVTONLY;
#else
    const int oflags = O_RDONLY;
#endif
    watch.fd = ::open(file.generic_string().c_str(), oflags);
    if (watch.fd < 0) return false;
    struct kevent event {};
    EV_SET(&event, watch.fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE,
           0, nullptr);
    if (kevent(kq_, &event, 1, nullptr, 0, nullptr) < 0) {
        ::close(watch.fd);
        return false;
    }
    kwatches_.push_back(watch);
    return true;
}

bool FileEventWatcher::reopenKqueueWatches() {
    for (const KqueueWatch& watch : kwatches_) {
        if (watch.fd >= 0) ::close(watch.fd);
    }
    kwatches_.clear();
    const std::vector<fs::path> watched = files_;
    bool ok = true;
    for (const fs::path& file : watched) {
        if (!armKqueueWatch(file)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        for (const KqueueWatch& watch : kwatches_) {
            if (watch.fd >= 0) ::close(watch.fd);
        }
        kwatches_.clear();
    }
    return ok;
}

bool FileEventWatcher::open(const DevWatchSet& set) {
    close();
    if (set.files.empty()) return false;
    int kq = kqueue();
    if (kq < 0) return false;
    kq_ = kq;
    adopt(set);
    bool ok = true;
    for (const fs::path& file : files_) {
        if (!armKqueueWatch(file)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        close();
        return false;
    }
    healthy_ = true;
    return true;
}

bool FileEventWatcher::waitForActivity(std::chrono::milliseconds timeout) {
    if (kq_ < 0) return false;
    struct kevent events[16];
    struct timespec ts{};
    ts.tv_sec = 0;
    ts.tv_nsec = static_cast<long>(timeout.count()) * 1000000L;
    const int count = kevent(kq_, nullptr, 0, events, 16, &ts);
    if (count <= 0) return false;
    bool needReopen = false;
    for (int i = 0; i < count; ++i) {
        if ((events[i].fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) != 0) {
            needReopen = true;
        }
    }
    if (needReopen) {
        // The reported fds are stale (editor atomic saves rename over the
        // watched path). Re-arm every watched file; if any cannot be reopened
        // the watcher degrades to polling.
        if (!reopenKqueueWatches()) {
            healthy_ = false;
        }
    }
    return true;  // every registered fd maps to a watched file
}

void FileEventWatcher::close() {
    if (kq_ >= 0) ::close(kq_);
    kq_ = -1;
    for (const KqueueWatch& watch : kwatches_) {
        if (watch.fd >= 0) ::close(watch.fd);
    }
    kwatches_.clear();
    files_.clear();
    directories_.clear();
    keys_.clear();
    healthy_ = false;
}

#else

bool FileEventWatcher::open(const DevWatchSet&) { return false; }
bool FileEventWatcher::waitForActivity(std::chrono::milliseconds) { return false; }
void FileEventWatcher::close() {
    files_.clear();
    directories_.clear();
    keys_.clear();
    healthy_ = false;
}

#endif

// ---------------------------------------------------------------------------
// The dev session: the watched set, the baselines the debounce confirmation
// compares against, and the child process.
// ---------------------------------------------------------------------------
struct DevSession {
    DevSession(const CliOptions& o, const fs::path& f) : opt(o), file(f) {}

    const CliOptions& opt;
    fs::path file;
    FileEventWatcher watcher;
    DevWatchSet watched;
    std::vector<StatFingerprint> baselineStat;
    std::vector<ContentEntry> baselineContent;
    // Edge-triggered mirror of the watched set's stat: a change fires only
    // when this moves, so a steady mismatch against the (older) build-time
    // baseline cannot re-arm the debounce on every poll iteration.
    std::vector<StatFingerprint> lastStat;
    ChildProcess child{};
    std::chrono::steady_clock::time_point lastChangeAt{};
    bool pendingChange = false;
    bool forceRebuild = false;
    bool usedFallback = false;
    bool fallbackLogged = false;
};

void initializeBaseline(DevSession& session) {
    session.baselineStat = statFingerprint(session.watched);
    session.baselineContent = contentFingerprint(session.watched);
    session.lastStat = session.baselineStat;
}

// One watched file's content entry difference (used for the classifier).
bool contentEntryDiffers(const ContentEntry& a, const ContentEntry& b) {
    if (a.exists != b.exists) return true;
    if (!a.exists) return false;
    return a.hash != b.hash || a.size != b.size;
}

// v0.2.5 Phase 4 Slice 1: classify this edit as hot-patch material. A
// single-file, single-unit body-only edit (interface unchanged since the
// last build) is a swap candidate once the runtime safe point lands
// (Slice 4); everything else takes the normal rebuild path. The build
// itself is never altered by this diagnostic.
void classifyEdit(const DevSession& session, const FrontendResult* frontend,
                  const std::unordered_map<std::string, IncrementalUnitEntry>& baseline) {
    const std::vector<ContentEntry>& contentBefore = session.baselineContent;
    if (frontend == nullptr || !frontend->program ||
        frontend->graph == nullptr) {
        std::fprintf(stderr,
                     "cp3c: patch classification: single-unit or unsupported "
                     "build; not a swap candidate\n");
        return;
    }
    const std::vector<ContentEntry> contentNow =
        contentFingerprint(session.watched);
    if (contentNow.size() != contentBefore.size()) {
        std::fprintf(stderr,
                     "cp3c: patch classification: watched set changed "
                     "(%zu -> %zu files); full rebuild\n",
                     contentBefore.size(), contentNow.size());
        return;
    }
    std::vector<fs::path> changed;
    for (std::size_t i = 0; i < contentNow.size(); ++i) {
        if (contentEntryDiffers(contentNow[i], contentBefore[i])) {
            changed.push_back(session.watched.files[i]);
        }
    }
    if (changed.size() != 1) {
        if (changed.empty()) {
            std::fprintf(stderr,
                         "cp3c: patch classification: no content change\n");
        } else {
            std::fprintf(stderr,
                         "cp3c: patch classification: %zu files changed; "
                         "full rebuild\n",
                         changed.size());
        }
        return;
    }
    const fs::path target = changed[0];
    const std::string targetNative = target.string();
    std::string targetGenericSlash = target.generic_string();
    std::replace(targetGenericSlash.begin(), targetGenericSlash.end(),
                 '\\', '/');
    const std::string targetGeneric = targetGenericSlash;
    const std::string targetName = target.filename().string();
    const CompilationUnit* unit = nullptr;
    for (const CompilationUnit* u : frontend->graph->units()) {
        if (u == nullptr || u->file == nullptr) continue;
        const std::string& up = u->file->canonicalPath.empty()
                                    ? u->file->path
                                    : u->file->canonicalPath;
        if (up.empty()) continue;
        if (up == targetNative) { unit = u; break; }
        std::string upGeneric = up;
        std::replace(upGeneric.begin(), upGeneric.end(), '\\', '/');
        if (upGeneric == targetGeneric) { unit = u; break; }
        const std::size_t slash = upGeneric.find_last_of('/');
        const std::string upName =
            slash == std::string::npos ? upGeneric : upGeneric.substr(slash + 1);
        if (upName == targetName) { unit = u; break; }
    }
    if (unit == nullptr) {
        std::fprintf(stderr,
                     "cp3c: patch classification: changed file outside the "
                     "module graph; full rebuild\n");
        return;
    }
    const auto ihOf = interfaceHashesForProgram(*frontend->graph,
                                                frontend->sema,
                                                frontend->unitSemas);
    const auto ihIt = ihOf.find(unit->id);
    const auto baseIt = baseline.find(incrementalUnitKey(*unit));
    const bool bodyOnly = baseIt != baseline.end() && ihIt != ihOf.end() &&
        !baseIt->second.interfaceHash.empty() &&
        baseIt->second.interfaceHash == incrementalHex64(ihIt->second);
    if (bodyOnly) {
        std::fprintf(stderr,
                     "cp3c: patch classification: body-only (interface "
                     "stable); swap candidate: %s\n",
                     unit->moduleName.c_str());
    } else {
        std::fprintf(stderr,
                     "cp3c: patch classification: interface changed; "
                     "full rebuild is the correct path: %s\n",
                     unit->moduleName.c_str());
    }
}

// Re-resolves the watched set from a fresh module graph load. Called after
// every build: the graph may have changed (new imports, renamed preludes).
void refreshWatchedSet(DevSession& session) {
    session.watched = collectWatchedSet(session.file);
    session.watcher.close();
    session.usedFallback = !session.watcher.open(session.watched);
    if (session.usedFallback && !session.fallbackLogged) {
        std::fprintf(stderr,
                     "cp3c: file events unavailable; polling the watched files "
                     "every 50 ms\n");
        session.fallbackLogged = true;
    }
}

void spawnChild(DevSession& session, const fs::path& exe) {
    std::vector<std::string> args{exe.string()};
    args.insert(args.end(), session.opt.programArgs.begin(),
                session.opt.programArgs.end());
    if (startChildProcess(args, session.child)) {
        std::fprintf(stderr, "cp3c: running %s\n", exe.string().c_str());
        std::fflush(stderr);
    } else {
        std::fprintf(stderr, "cp3c: could not start %s\n", exe.string().c_str());
    }
}

// Renames the just-linked temp executable over the final path. On Windows the
// child keeps the old image mapped, so the kernel may release the file a few
// milliseconds after the child exits; retry across a short window.
bool replaceDeferredExe(const fs::path& tmp, const fs::path& exe) {
    for (int attempt = 0; attempt < 40; ++attempt) {
#if defined(_WIN32)
        const std::wstring from = tmp.wstring();
        const std::wstring to = exe.wstring();
        if (MoveFileExW(from.c_str(), to.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
#else
        std::error_code ec;
        fs::rename(tmp, exe, ec);
        if (!ec) return true;
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}

std::optional<fs::file_time_type> exeMtimeIfExists(const fs::path& exe) {
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec)) return std::nullopt;
    const fs::file_time_type mtime = fs::last_write_time(exe, ec);
    if (ec) return std::nullopt;
    return mtime;
}

// One rebuild pass. The old child keeps running during cmdBuildOrRun; it is
// restarted only after a successful build whose executable is newer, or when
// nothing is running. On a compile error the last good app is left alone.
void rebuild(DevSession& session, bool isInitial) {
    const fs::path exe = makeBuildLayout(session.file).exePath;
    const DevWatchSet previousSet = session.watched;  // empty on the initial pass
    const std::vector<StatFingerprint> preBuildStat =
        statFingerprint(previousSet);
    const std::optional<fs::file_time_type> exeBefore = exeMtimeIfExists(exe);

    if (!isInitial) {
        std::fprintf(stderr, "cp3c: source changed; rebuilding...\n");
    }
    // v0.2.5 Phase 4 Slice 1: snapshot the pre-build incremental cache;
    // the content baseline is session.baselineContent, which still holds the
    // pre-build state at this point in rebuild().
    std::unordered_map<std::string, IncrementalUnitEntry> baselineCache;
    if (!isInitial && session.opt.verbose) {
        std::vector<IncrementalUnitEntry> entries;
        if (incrementalCacheLoad(incrementalCachePathFor(session.file),
                                 entries)) {
            for (IncrementalUnitEntry& entry : entries) {
                baselineCache.emplace(entry.unitKey, std::move(entry));
            }
        }
    }
    CliOptions effective = session.opt;
    effective.internalDeferExeReplace = true;
    std::unique_ptr<FrontendResult> frontend;
    const int rc = cmdBuildOrRun(effective, false, &frontend);
    std::fprintf(stderr, "cp3c: build %s (exit %d)\n",
                 rc == kExitSuccess ? "ok" : "reported problems", rc);
    if (!isInitial && session.opt.verbose && frontend) {
        classifyEdit(session, frontend.get(), baselineCache);
    }

    // Re-resolve the watched set: the graph just rebuilt from fresh sources,
    // so the file list (and its parent directories) may have changed.
    refreshWatchedSet(session);

    // Files that drifted while the build was running, or newly watched files
    // the build may not have seen, force one defensive rebuild next window.
    // Bounded: at most one forced pass per build window.
    const bool drifted = !isInitial &&
        (!statMatches(preBuildStat, statFingerprint(previousSet)) ||
         previousSet.keySet != session.watched.keySet);

    // Baseline the current state so the watch loop does not drift forever.
    initializeBaseline(session);
    if (drifted) {
        session.pendingChange = true;
        session.lastChangeAt = std::chrono::steady_clock::now();
        session.forceRebuild = true;
    }

    if (g_devStopRequested) return;

    if (rc == kExitSuccess) {
        const fs::path pendingTmp =
            exe.parent_path() / (exe.filename().string() + ".link-tmp");
        // Deferred executable replace (Windows): the running child mapped the
        // previous exe, so the link wrote the fresh one to the temp and left
        // the final path alone. The fresh exe is ready NOW; complete the swap:
        // stop the child, replace the image, spawn the new one.
        bool swapFailed = false;
        {
            std::error_code tmpEc;
            if (fs::is_regular_file(pendingTmp, tmpEc)) {
                if (session.child.native != -1) stopChildProcess(session.child);
                swapFailed = !replaceDeferredExe(pendingTmp, exe);
                if (swapFailed) {
                    std::fprintf(stderr,
                                 "cp3c: could not replace '%s'; the fresh exe is kept as '%s' (retried on the next rebuild)\n",
                                 exe.string().c_str(), pendingTmp.string().c_str());
                }
            }
        }
        const std::optional<fs::file_time_type> exeAfter = exeMtimeIfExists(exe);
        const bool exeChanged = exeAfter.has_value() &&
            (!exeBefore.has_value() || *exeAfter != *exeBefore);
        const bool childLive = session.child.native != -1;
        if (exeChanged || !childLive) {
            if (childLive) stopChildProcess(session.child);
            if (swapFailed) {
                // The final path still holds the last good exe: run it rather
                // than leaving the user with no app at all.
                std::fprintf(stderr, "cp3c: restarting the last good app\n");
            }
            spawnChild(session, exe);
        } else if (session.opt.verbose) {
            std::fprintf(stderr,
                         "cp3c: executable unchanged; keeping the running app\n");
        }
    } else if (session.child.native != -1) {
        std::fprintf(stderr, "cp3c: keeping the last good build running\n");
    }
}

void reapChild(DevSession& session) {
    if (session.child.native == -1) return;
    int exitCode = 0;
    const ChildProcessState state = pollChildProcess(session.child, exitCode);
    if (state == ChildProcessState::Exited) {
        std::fprintf(
            stderr,
            "cp3c: program exited (exit %d); waiting for changes\n", exitCode);
    } else if (state == ChildProcessState::Failed) {
        std::fprintf(stderr,
                     "cp3c: could not monitor the running program; "
                     "waiting for changes\n");
    }
}

}  // namespace

int cmdDev(const CliOptions& opt) {
    if (opt.file.empty()) {
        std::fprintf(stderr, "cp3c: dev needs a .cp3 file to watch\n");
        return kExitUsageError;
    }
    std::error_code ec;
    const fs::path file = normalizeWatchPath(fs::path(opt.file));
    if (!fs::exists(file, ec)) {
        std::fprintf(stderr, "cp3c: cannot open '%s'\n", opt.file.c_str());
        return kExitUsageError;
    }

    std::fprintf(stderr, "cp3c: watching %s (Ctrl+C to stop)\n",
                 opt.file.c_str());

    if (opt.once) {
        CliOptions effective = opt;
        effective.internalDeferExeReplace = true;
        const int rc = cmdBuildOrRun(effective, true, nullptr);
        std::fprintf(stderr, "cp3c: build %s (exit %d)\n",
                     rc == kExitSuccess ? "ok" : "reported problems", rc);
        return rc;
    }

    g_devStopRequested = 0;
    const ScopedDevSignalHandler signalHandler;

    DevSession session{opt, file};

    // Initial build: start the app when the build succeeds (spawned after the
    // executable is ready, as with every restart).
    rebuild(session, true);

    for (;;) {
        if (g_devStopRequested) break;

        bool activity = false;
        if (session.usedFallback) {
            std::this_thread::sleep_for(kPollInterval);
            const std::vector<StatFingerprint> stat =
                statFingerprint(session.watched);
            if (!statMatches(stat, session.lastStat)) {
                session.lastStat = stat;
                activity = true;
            }
        } else {
            activity = session.watcher.waitForActivity(kEventTimeout);
            if (!session.watcher.healthy()) {
                // The event path died at runtime: degrade to stat polling once.
                session.usedFallback = true;
                session.watcher.close();
                if (!session.fallbackLogged) {
                    std::fprintf(stderr,
                                 "cp3c: file events failed; polling the watched "
                                 "files every 50 ms\n");
                    session.fallbackLogged = true;
                }
            }
        }
        if (activity) {
            session.pendingChange = true;
            session.lastChangeAt = std::chrono::steady_clock::now();
        }

        reapChild(session);

        if (session.pendingChange &&
            std::chrono::steady_clock::now() - session.lastChangeAt >=
                kDebounceWindow) {
            session.pendingChange = false;
            const bool force = session.forceRebuild;
            session.forceRebuild = false;
            bool changed = force;
            if (!changed) {
                const std::vector<ContentEntry> content =
                    contentFingerprint(session.watched);
                changed = !contentMatches(content, session.baselineContent);
            }
            if (!changed) {
                // The save burst produced no content change: refresh the stat
                // baseline but do not restart the app.
                session.baselineStat = statFingerprint(session.watched);
                if (opt.verbose) {
                    std::fprintf(stderr,
                                 "cp3c: no source content changed; "
                                 "not rebuilding\n");
                }
            } else {
                rebuild(session, false);
            }
        }
    }

    stopChildProcess(session.child);
    std::fprintf(stderr, "cp3c: stopped\n");
    return kExitSuccess;
}

}  // namespace cp3::cli
