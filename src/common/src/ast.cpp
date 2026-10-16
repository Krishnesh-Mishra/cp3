// =============================================================================
// cp3/ast.cpp — out-of-line AST helpers declared in cp3/ast.hpp.
//
// Everything here is DERIVED from CP3_AST_NODE_LIST; there is deliberately no
// second list of node names to drift out of sync. To add a node, edit ast.hpp's
// registry only (see ARCHITECTURE.md "Extending the AST").
// =============================================================================
#include "cp3/ast.hpp"

namespace cp3 {

// nodeKindName — generated directly from the single source of truth. The
// switch is exhaustive by construction: CP3_AST_NODE_LIST defines every
// enumerator of NodeKind.
const char* nodeKindName(NodeKind kind) noexcept {
    switch (kind) {
#define CP3_KIND_NAME(Name) \
    case NodeKind::Name:    \
        return #Name;
        CP3_AST_NODE_LIST(CP3_KIND_NAME)
#undef CP3_KIND_NAME
    }
    return "Unknown";
}

// True when `node` can stand as a binding target: an Identifier or any of the
// destructuring-pattern kinds (ArrayPattern, ObjectPattern, RestElement,
// AssignmentPattern). ObjectPatternProperty/ArrayPattern nested elements are
// reached through their parent patterns, not checked here.
bool isPatternNode(const Node& node) noexcept {
    switch (node.kind) {
        case NodeKind::Identifier:
        case NodeKind::ArrayPattern:
        case NodeKind::ObjectPattern:
        case NodeKind::RestElement:
        case NodeKind::AssignmentPattern:
            return true;
        default:
            return false;
    }
}

Identifier* asIdentifier(Node* node) noexcept {
    if (node == nullptr || node->kind != NodeKind::Identifier) {
        return nullptr;
    }
    return static_cast<Identifier*>(node);
}

const Identifier* asIdentifier(const Node* node) noexcept {
    if (node == nullptr || node->kind != NodeKind::Identifier) {
        return nullptr;
    }
    return static_cast<const Identifier*>(node);
}

}  // namespace cp3
