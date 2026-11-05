// =============================================================================
// src/std/network/express/middleware/native/middleware_native.hpp
// Native-package row snippet for this subtree (D68). NOT a shared-file edit:
// the integrator pastes the row below into src/std/network/codegen/native.hpp
// (see README.md). Include root + TU + marker are additive only.
// =============================================================================
#pragma once

// Include root to add:
//   "src/std/network/express/middleware/include"
// (the family ABI tree src/std/network/include already covers cp3/std/...;
//  this extra root serves cp3/std/network/express/middleware.hpp)
//
// Runtime TU to add to runtimeSources:
//   "src/std/network/express/middleware/runtime/middleware_engine.cpp"
//   "src/std/network/express/middleware/sema/middleware_sema.cpp"
//   "src/std/network/express/middleware/codegen/middleware_codegen.cpp"
//
// Marker (same index as runtimeSources) to add:
//   "MiddlewareEngine(|express::normalizeMount(|express::mountMatches("
//
// The engine reuses the std/network native libs (uSockets/libuv) already in
// the row - no new third-party dependency.
