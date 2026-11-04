// health-server (cpp entry) -- minimal hand-rolled HTTP/1.1 over sockets, no
// libraries beyond the platform sockets API (Winsock2 on Windows, BSD sockets
// elsewhere).
//
// Build:  g++ -std=c++20 -O3 -o health-server.exe main.cpp -lws2_32   (Windows)
//         g++ -std=c++20 -O3 -o health-server main.cpp                (POSIX)
// Run:    health-server.exe [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only. One detached thread per connection, HTTP/1.1
// keep-alive: a connection stays open and serves requests until the client
// closes it or the peer errors. Pipelined requests are not supported (the
// bench client never pipelines), anything already buffered past the headers is
// carried over to the next read.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
using socket_t = SOCKET;
static void closeSocket(socket_t s) { closesocket(s); }
static bool initSocketLayer() {
  WSADATA wsa;
  return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}
#else
using socket_t = int;
static void closeSocket(socket_t s) { close(s); }
static bool initSocketLayer() { return true; }
#endif

static const socket_t kInvalidSocket = (socket_t)(-1);

static void respond(socket_t client) {
  std::string buffer;
  for (;;) {
    // Accumulate one request head (up to the blank line); the client's next
    // request arrives only after this response, so no pipelining exists here.
    size_t end = buffer.find("\r\n\r\n");
    while (end == std::string::npos) {
      char chunk[4096];
      int n = recv(client, chunk, (int)sizeof(chunk), 0);
      if (n <= 0) {
        closeSocket(client);
        return;
      }
      buffer.append(chunk, (size_t)n);
      end = buffer.find("\r\n\r\n");
      if (buffer.size() > 65536) {
        closeSocket(client);
        return;
      }
    }
    std::string head = buffer.substr(0, end + 4);
    buffer.erase(0, end + 4);

    std::string method, path;
    std::istringstream firstLine(head.substr(0, head.find("\r\n")));
    firstLine >> method >> path;

    int status = 404;
    std::string reason = "Not Found";
    std::string body = "not found";
    if (method == "GET" && path == "/healthz") {
      status = 200;
      reason = "OK";
      body = "ok";
    } else if (method == "GET" && path == "/") {
      status = 200;
      reason = "OK";
      body = "health-server ok";
    }

    std::string response = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n" +
                           "Content-Type: text/plain\r\n" +
                           "Content-Length: " + std::to_string(body.size()) + "\r\n" +
                           "Connection: keep-alive\r\n\r\n" + body;
    if (send(client, response.data(), (int)response.size(), 0) <= 0) {
      closeSocket(client);
      return;
    }
  }
}

int main(int argc, char** argv) {
  int port = argc > 1 ? std::atoi(argv[1]) : 8080;

  if (!initSocketLayer()) {
    std::fprintf(stderr, "socket layer init failed\n");
    return 1;
  }

  socket_t server = socket(AF_INET, SOCK_STREAM, 0);
  if (server == kInvalidSocket) {
    std::fprintf(stderr, "socket() failed\n");
    return 1;
  }
  int reuse = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, (int)sizeof(reuse));

  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons((unsigned short)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1
  if (bind(server, (sockaddr*)&address, (int)sizeof(address)) != 0 || listen(server, 128) != 0) {
    std::fprintf(stderr, "bind/listen failed on port %d\n", port);
    return 1;
  }

  std::printf("listening on http://127.0.0.1:%d\n", port);
  std::fflush(stdout);

  for (;;) {
    socket_t client = accept(server, nullptr, nullptr);
    if (client == kInvalidSocket) continue;
    std::thread(respond, client).detach();
  }
}
