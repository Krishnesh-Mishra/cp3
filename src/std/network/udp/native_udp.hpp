// =============================================================================
// PASTE SNIPPETS for src/std/network/codegen/native.hpp (the std/network
// native package row) - UDP sub-surface.
//
// OWNERSHIP: the std/network family (src/std/network/udp/**). NOT compiled
// directly; the lead pastes the lines below into kNetworkNativePackage.
// The row is parallel to the existing entries: both arrays must stay the
// same length, in the same order.
// =============================================================================

// ---- 1. includeRoots: append next to "src/std/network/include" -------------
    "src/std/network/udp/include",

// ---- 2. runtimeSources: append next to the other network TUs ---------------
    "src/std/network/udp/runtime_udp.cpp",

// ---- 3. markers (parallel to runtimeSources): append next to "WebSocket" ---
    "createUdpSocket(",
