// health-server (cpp/drogon entry) -- Drogon, single event-loop thread.
//
// Build (from the repository root, after `vcpkg install drogon:x64-windows`):
//   cmake -S benchmarks/cases/health-server/cpp -B benchmarks/.cache/build/health-server-drogon \
//         -DCMAKE_TOOLCHAIN_FILE=C:/Users/Krishnesh/dev/vcpkg/scripts/buildsystems/vcpkg.cmake \
//         -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_BUILD_TYPE=Release
//   cmake --build benchmarks/.cache/build/health-server-drogon --config Release
// Run:
//   benchmarks/.cache/build/health-server-drogon/Release/health-server-drogon.exe [port]
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only. One event-loop thread keeps this entry comparable to
// the single-core row.

#include <drogon/drogon.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>

namespace {

using Callback = std::function<void(const drogon::HttpResponsePtr &)>;

void sendText(Callback &&callback, drogon::HttpStatusCode status, std::string body) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(status);
    response->setContentTypeString("text/plain");
    response->setBody(std::move(body));
    callback(response);
}

void notFound(const drogon::HttpRequestPtr &, Callback &&callback) {
    sendText(std::move(callback), drogon::k404NotFound, "not found");
}

void handleHealthz(const drogon::HttpRequestPtr &request, Callback &&callback) {
    if (request->method() != drogon::Get) {
        notFound(request, std::move(callback));
        return;
    }
    sendText(std::move(callback), drogon::k200OK, "ok");
}

void handleRoot(const drogon::HttpRequestPtr &request, Callback &&callback) {
    if (request->method() != drogon::Get) {
        notFound(request, std::move(callback));
        return;
    }
    sendText(std::move(callback), drogon::k200OK, "health-server ok");
}

}  // namespace

int main(int argc, char **argv) {
    int port = 8080;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    if (port < 1 || port > 65535) {
        std::fprintf(stderr, "invalid port\n");
        return 2;
    }

    auto &app = drogon::app();
    app.addListener("127.0.0.1", static_cast<uint16_t>(port))
            .setThreadNum(1)
            .setLogLevel(trantor::Logger::kWarn)
            .registerHandler(
                    "/healthz",
                    [](const drogon::HttpRequestPtr &request, Callback &&callback) {
                        handleHealthz(request, std::move(callback));
                    })
            .registerHandler(
                    "/",
                    [](const drogon::HttpRequestPtr &request, Callback &&callback) {
                        handleRoot(request, std::move(callback));
                    })
            .setDefaultHandler(notFound);

    std::printf("listening on http://127.0.0.1:%d\n", port);
    std::fflush(stdout);
    app.run();
    return 0;
}
