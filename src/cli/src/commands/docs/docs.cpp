#include "detail/core/commands.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cp3_docs_data.hpp"
#include "detail/core/docs.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cp3::cli {
namespace {

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void closeSocket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void closeSocket(Socket socket) { ::close(socket); }
#endif

std::string htmlEscape(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (const char ch : input) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string fileUrl(const std::filesystem::path& path) {
    std::string raw = std::filesystem::absolute(path).generic_string();
#if defined(_WIN32)
    if (raw.size() >= 2 && raw[1] == ':') raw.insert(raw.begin(), '/');
#endif
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded = "file://";
    for (const unsigned char ch : raw) {
        if (std::isalnum(ch) || ch == '/' || ch == ':' || ch == '-' ||
            ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[ch >> 4]);
            encoded.push_back(hex[ch & 0x0F]);
        }
    }
    return encoded;
}

struct DocSection {
    std::string slug;
    std::string title;
    std::string chapter;
    std::string markdown;
};

std::vector<std::string> terms(std::string_view text) {
    static const std::unordered_set<std::string> stop = {
        "about", "after", "also", "and", "are", "can", "chapter", "does",
        "for", "from", "have", "into", "its", "not", "one", "only", "that",
        "the", "their", "then", "this", "through", "use", "when", "where",
        "which", "with", "will", "you", "your", "cp3", "compiler", "value",
        "values", "function", "functions", "type", "types"
    };
    std::vector<std::string> out;
    std::string word;
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) {
            word.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!word.empty()) {
            if (word.size() >= 2 && !stop.contains(word)) out.push_back(word);
            word.clear();
        }
    }
    if (word.size() >= 2 && !stop.contains(word)) out.push_back(word);
    return out;
}

std::vector<DocSection> docSections() {
    std::vector<DocSection> sections;
    sections.reserve(std::size(docs::kEntries));
    for (const docs::Entry& entry : docs::kEntries) {
        sections.push_back(DocSection{std::string(entry.slug),
                                     std::string(entry.title),
                                     std::string(entry.chapter),
                                     std::string(entry.markdown)});
    }
    return sections;
}

struct RankedSection {
    DocSection section;
    double score = 0.0;
};

std::vector<RankedSection> relatedSections(const Diagnostic& diagnostic) {
    const std::vector<DocSection> sections = docSections();
    if (sections.empty()) return {};
    std::string query = diagnostic.message;
    for (const DiagnosticNote& note : diagnostic.notes) query += " " + note.message;
    for (const std::string& suggestion : diagnostic.suggestions) query += " " + suggestion;
    const std::vector<std::string> queryTerms = terms(query);
    std::unordered_map<std::string, std::size_t> documentFrequency;
    std::vector<std::unordered_map<std::string, std::size_t>> counts(sections.size());
    for (std::size_t i = 0; i < sections.size(); ++i) {
        for (const std::string& term : terms(sections[i].title + " " +
                                             sections[i].chapter + " " +
                                             sections[i].markdown)) {
            ++counts[i][term];
        }
        for (const auto& entry : counts[i]) ++documentFrequency[entry.first];
    }
    std::unordered_map<std::string, std::size_t> queryCounts;
    for (const std::string& term : queryTerms) ++queryCounts[term];
    const auto idf = [&](const std::string& term) {
        const auto found = documentFrequency.find(term);
        const std::size_t df = found == documentFrequency.end() ? 0 : found->second;
        return std::log(1.0 + (static_cast<double>(sections.size()) + 1.0) /
                                   (static_cast<double>(df) + 1.0));
    };
    double queryNorm = 0.0;
    std::unordered_map<std::string, double> queryWeights;
    for (const auto& entry : queryCounts) {
        const double weight = std::log1p(static_cast<double>(entry.second)) * idf(entry.first);
        queryWeights.emplace(entry.first, weight);
        queryNorm += weight * weight;
    }
    queryNorm = std::sqrt(queryNorm);
    std::vector<RankedSection> ranked;
    ranked.reserve(sections.size());
    for (std::size_t i = 0; i < sections.size(); ++i) {
        double dot = 0.0;
        double norm = 0.0;
        for (const auto& entry : counts[i]) {
            const double weight = std::log1p(static_cast<double>(entry.second)) * idf(entry.first);
            norm += weight * weight;
            const auto queryWeight = queryWeights.find(entry.first);
            if (queryWeight != queryWeights.end()) dot += weight * queryWeight->second;
        }
        const double score = (norm == 0.0 || queryNorm == 0.0)
            ? 0.0 : dot / (std::sqrt(norm) * queryNorm);
        ranked.push_back(RankedSection{sections[i], score});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const RankedSection& left,
                                                       const RankedSection& right) {
        return left.score > right.score;
    });
    if (ranked.size() > 3) ranked.resize(3);
    return ranked;
}

std::string excerpt(std::string text) {
    constexpr std::size_t limit = 280;
    for (char& ch : text) {
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch == '`' ||
            ch == '#' || ch == '*' || ch == '-' || ch == '[' || ch == ']' ||
            ch == '(' || ch == ')') ch = ' ';
    }
    std::string compact;
    compact.reserve(text.size());
    bool space = true;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!space) compact.push_back(' ');
            space = true;
        } else {
            compact.push_back(ch);
            space = false;
        }
    }
    text = std::move(compact);
    if (text.size() > limit) {
        text.resize(limit);
        text += "…";
    }
    return text;
}

std::string makeDiagnosticHtml(const Diagnostic& diagnostic) {
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>CP3 diagnostic " << htmlEscape(diagnostic.code)
         << "</title><style>"
            "body{margin:0;background:#171a1d;color:#e5e8ea;font:15px/1.65 Segoe UI,Arial,sans-serif}"
            "main{max-width:850px;margin:52px auto;padding:0 24px;animation:enter .24s ease both}@keyframes enter{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:translateY(0)}}.brand{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:#83b6d2;font-weight:700}"
            "h1{font-size:30px;letter-spacing:-.03em;margin:10px 0 4px}"
            ".code{font:600 13px Consolas,monospace;color:#a8d3ea;background:#24343d;border-radius:4px;padding:3px 8px}"
            ".panel{background:#202428;border:0;border-radius:8px;padding:18px 20px;margin:20px 0}"
            ".where{color:#adb6bd;font-size:13px}.excerpt{white-space:pre-wrap;background:#22272b;padding:14px;border-radius:6px;font:13px/1.6 Consolas,monospace;overflow:auto}"
            "a{color:#83b6d2;text-decoration:none}a:hover{text-decoration:underline}li{margin:5px 0}"
            "</style></head><body><main><div class=\"brand\">CP3 compiler help</div><h1>Compiler diagnostic</h1><p><span class=\"code\">"
         << htmlEscape(diagnostic.code) << "</span> &nbsp; "
         << htmlEscape(diagnostic.message) << "</p>";
    if (diagnostic.span.valid() && diagnostic.span.file != nullptr) {
        const auto line = diagnostic.span.begin.line == 0 ? 1 : diagnostic.span.begin.line;
        const auto col = diagnostic.span.begin.column == 0 ? 1 : diagnostic.span.begin.column;
        html << "<p class=\"where\">" << htmlEscape(diagnostic.span.file->path)
             << ':' << line << ':' << col << "</p><div class=\"panel\"><strong>Source</strong><pre class=\"excerpt\">"
             << htmlEscape(diagnostic.span.file->lineText(line)) << "\n";
        for (std::uint32_t i = 1; i < col && i < 500; ++i) html << ' ';
        html << "^</pre></div>";
    }
    if (!diagnostic.notes.empty()) {
        html << "<div class=\"panel\"><strong>Related notes</strong><ul>";
        for (const DiagnosticNote& note : diagnostic.notes) {
            html << "<li>" << htmlEscape(note.message) << "</li>";
        }
        html << "</ul></div>";
    }
    if (!diagnostic.suggestions.empty()) {
        html << "<div class=\"panel\"><strong>Suggested next steps</strong><ul>";
        for (const std::string& suggestion : diagnostic.suggestions) {
            html << "<li>" << htmlEscape(suggestion) << "</li>";
        }
        html << "</ul></div>";
    }
    const std::vector<RankedSection> matches = relatedSections(diagnostic);
    html << "<div class=\"panel\"><strong>Related guide sections</strong>"
            "<p>Ranked by TF-IDF cosine similarity between this diagnostic text "
            "and the text of each guide section. Scores rank text overlap; they "
            "are not probabilities or guarantees.</p><ol>";
    for (const RankedSection& match : matches) {
        html << "<li><strong>" << htmlEscape(match.section.title)
             << "</strong> <span class=\"where\">similarity "
             << match.score << "</span><p>" << htmlEscape(excerpt(match.section.markdown))
             << "</p></li>";
    }
    html << "</ol></div>";
    html << "<div class=\"panel\"><strong>What to do</strong><p>Use the code "
            "<span class=\"code\">" << htmlEscape(diagnostic.code)
         << "</span> to find this diagnostic in the CP3 language guide. Start with "
            "the source location above, then apply any suggested correction. "
            "The page is a local copy created by this compiler invocation. Run "
            "<code>cp3 docs</code> to browse the full guide.</p></div>"
            "</main></body></html>";
    return html.str();
}

bool parsePort(std::string_view value, std::uint16_t& port) {
    if (value.empty()) return false;
    unsigned parsed = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') return false;
        parsed = parsed * 10 + static_cast<unsigned>(ch - '0');
        if (parsed > 65535) return false;
    }
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

bool sendAll(Socket socket, std::string_view data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
#if defined(_WIN32)
        const int sent = ::send(socket, data.data() + offset,
                                static_cast<int>(data.size() - offset), 0);
#else
        const auto sent = ::send(socket, data.data() + offset,
                                 data.size() - offset, 0);
#endif
        if (sent <= 0) return false;
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

std::string jsonEscape(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() + 16);
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[ch >> 4]);
                    out.push_back(hex[ch & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
        }
    }
    return out;
}

std::string docsJson() {
    std::string json = "[";
    bool first = true;
    for (const docs::Entry& entry : docs::kEntries) {
        if (!first) json.push_back(',');
        first = false;
        json += "{\"slug\":\"" + jsonEscape(entry.slug) +
                "\",\"title\":\"" + jsonEscape(entry.title) +
                "\",\"chapter\":\"" + jsonEscape(entry.chapter) +
                "\",\"markdown\":\"" + jsonEscape(entry.markdown) + "\"}";
    }
    json.push_back(']');
    return json;
}

void serveOne(Socket client) {
    std::array<char, 2048> request{};
#if defined(_WIN32)
    const int count = ::recv(client, request.data(),
                             static_cast<int>(request.size() - 1), 0);
#else
    const auto count = ::recv(client, request.data(), request.size() - 1, 0);
#endif
    const std::string_view line(request.data(), count > 0
        ? static_cast<std::size_t>(count) : 0);
    const bool get = line.substr(0, 4) == "GET ";
    const std::size_t pathStart = get ? 4 : 0;
    const std::size_t pathEnd = get ? line.find(' ', pathStart) : std::string_view::npos;
    const std::string_view route = pathEnd == std::string_view::npos
        ? std::string_view{} : line.substr(pathStart, pathEnd - pathStart);
    const bool api = route == "/api/docs";
    const std::string json = api ? docsJson() : std::string{};
    const std::string_view body = get
        ? (api ? std::string_view(json) : docs::kViewerHtml)
        : std::string_view("<!doctype html><title>Bad request</title><h1>Bad request</h1>");
    const std::string header = std::string(get ? "HTTP/1.1 200 OK\r\n" :
                                                  "HTTP/1.1 400 Bad Request\r\n") +
        std::string("Content-Type: ") + (api ? "application/json" : "text/html") +
        "; charset=utf-8\r\nCache-Control: no-store\r\n" +
        "Content-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n";
    sendAll(client, header);
    sendAll(client, body);
    closeSocket(client);
}

}  // namespace

std::string writeDiagnosticHelpPage(const Diagnostic& diagnostic) {
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string safeCode = diagnostic.code.empty() ? "unknown" : diagnostic.code;
    const auto path = temp / ("cp3-diagnostic-" + safeCode + "-" +
                              std::to_string(stamp) + ".html");
    std::ofstream page(path, std::ios::binary | std::ios::trunc);
    if (!page) return {};
    page << makeDiagnosticHtml(diagnostic);
    page.close();
    if (!page) return {};
    return fileUrl(path);
}

int cmdDocs(const std::vector<std::string>& args) {
    std::uint16_t port = 4173;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            std::fputs("Usage: cp3 docs [--port <0-65535>]\n"
                       "Serve the documentation bundled in this compiler on localhost.\n",
                       stdout);
            return kExitSuccess;
        }
        if (args[i] == "--port" && i + 1 < args.size()) {
            if (!parsePort(args[++i], port)) {
                std::fputs("cp3c docs: --port must be an integer from 0 to 65535\n", stderr);
                return kExitUsageError;
            }
            continue;
        }
        std::fprintf(stderr, "cp3c docs: unknown option '%s'\n", args[i].c_str());
        std::fputs("usage: cp3 docs [--port <0-65535>]\n", stderr);
        return kExitUsageError;
    }

#if defined(_WIN32)
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        std::fputs("cp3c docs: could not initialize Windows sockets\n", stderr);
        return kExitCompileError;
    }
#endif
    Socket server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == kInvalidSocket) {
        std::fputs("cp3c docs: could not create a local server socket\n", stderr);
#if defined(_WIN32)
        WSACleanup();
#endif
        return kExitCompileError;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
#if defined(_WIN32)
               reinterpret_cast<const char*>(&reuse), sizeof(reuse)
#else
               &reuse, sizeof(reuse)
#endif
    );
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(server, 16) != 0) {
        std::fprintf(stderr, "cp3c docs: could not listen on 127.0.0.1:%u\n",
                     static_cast<unsigned>(port));
        closeSocket(server);
#if defined(_WIN32)
        WSACleanup();
#endif
        return kExitCompileError;
    }
    socklen_t addressSize = sizeof(address);
    if (getsockname(server, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0) {
        port = ntohs(address.sin_port);
    }
    std::fprintf(stdout, "CP3 documentation is available at http://127.0.0.1:%u/\n",
                 static_cast<unsigned>(port));
    std::fputs("Press Ctrl+C to stop the local server.\n", stdout);
    std::fflush(stdout);
    for (;;) {
        Socket client = ::accept(server, nullptr, nullptr);
        if (client == kInvalidSocket) continue;
        serveOne(client);
    }
}

}  // namespace cp3::cli
