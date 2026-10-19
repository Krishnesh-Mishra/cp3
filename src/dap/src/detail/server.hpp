// =============================================================================
// src/dap/src/detail/server.hpp - the DAP server object, split by concern.
//
// OWNERSHIP: cp3-debug workstream (src/dap/**). PRIVATE header.
//
// This header is the seam between server.cpp (stdio transport, request
// dispatch, session events) and server_requests.cpp (one handler per DAP
// request). The class only holds the session, the response sequence and the
// two loop flags; everything else is a method declaration.
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "cp3/dap.hpp"

#include "detail/dap_json.hpp"
#include "detail/session.hpp"

namespace cp3::dap {

class Server {
public:
    explicit Server(const BuildFn& build) : build_(build) {}
    int run();

private:
    // ---- transport (server.cpp) -------------------------------------------
    bool readOne();
    void writeJson(const Json& message);
    void sendResponse(const std::string& requestSeq, const std::string& command,
                      Json body);
    void sendErrorResponse(const std::string& requestSeq,
                           const std::string& command,
                           const std::string& message);
    void sendEvent(const std::string& name, Json body);
    void sendOutput(const std::string& category, const std::string& text);

    // ---- dispatch (server.cpp) --------------------------------------------
    void handleMessage(const Json& message);
    void dispatch(const std::string& command, const std::string& requestSeq,
                  const Json& arguments);
    GdbSession* liveSession() const;

    // ---- requests (server_requests.cpp) -----------------------------------
    void handleInitialize(const std::string& requestSeq);
    void handleLaunch(const std::string& requestSeq, const Json& arguments);
    void handleSetBreakpoints(const std::string& requestSeq,
                              const Json& arguments);
    void handleConfigurationDone(const std::string& requestSeq);
    void handleResume(const std::string& command, const std::string& requestSeq,
                      const std::string& action);
    void handlePause(const std::string& requestSeq);
    void handleThreads(const std::string& requestSeq);
    void handleStackTrace(const std::string& requestSeq);
    void handleScopes(const std::string& requestSeq, const Json& arguments);
    void handleVariables(const std::string& requestSeq, const Json& arguments);
    void handleDisconnect(const std::string& requestSeq);
    void handleTerminate(const std::string& requestSeq);

    // ---- session events (server.cpp) --------------------------------------
    void pumpSession();
    void forwardEvent(const SessionEvent& event);

    BuildFn build_;
    std::unique_ptr<GdbSession> session_;
    std::int64_t nextSeq_ = 1;
    bool done_ = false;
    bool pendingExit_ = 0;
};

}  // namespace cp3::dap
