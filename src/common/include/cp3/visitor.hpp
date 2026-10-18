// =============================================================================
// cp3/visitor.hpp - exhaustive AST visitor dispatch.
//
// FROZEN INTERFACE (Beta One). See ARCHITECTURE.md.
//
// Two ways to walk the tree:
//
//   1. Subclass AstVisitor and override the visit() overloads you care about
//      (the base gives every node an empty default). Then call
//      cp3::dispatch(node, visitor) at each node you want to hand off.
//
//   2. Recurse manually through the public fields. A visitor that wants a full
//      pre-order walk implements one small `walkChildren` helper; the AST does
//      not force a particular traversal order.
//
// The dispatch table is generated from CP3_AST_NODE_LIST, so it can never drift
// out of sync with the node set: adding a node to the list is enough.
// =============================================================================
#pragma once

#include "cp3/ast.hpp"

namespace cp3 {

class AstVisitor {
public:
    virtual ~AstVisitor() = default;

#define CP3_VISIT_DECL(Name) virtual void visit(Name&) {}
    CP3_AST_NODE_LIST(CP3_VISIT_DECL)
#undef CP3_VISIT_DECL
};

// A visitor that also receives a const node (used by printers/analyses that
// do not mutate the tree).
class ConstAstVisitor {
public:
    virtual ~ConstAstVisitor() = default;

#define CP3_VISIT_DECL(Name) virtual void visit(const Name&) {}
    CP3_AST_NODE_LIST(CP3_VISIT_DECL)
#undef CP3_VISIT_DECL
};

// Double-dispatch entry point. The node's `kind` tag selects the overload.
inline void dispatch(Node& node, AstVisitor& visitor) {
    switch (node.kind) {
#define CP3_DISPATCH_CASE(Name)                          \
    case NodeKind::Name:                                 \
        visitor.visit(static_cast<Name&>(node));         \
        return;
        CP3_AST_NODE_LIST(CP3_DISPATCH_CASE)
#undef CP3_DISPATCH_CASE
    }
}

inline void dispatch(const Node& node, ConstAstVisitor& visitor) {
    switch (node.kind) {
#define CP3_DISPATCH_CASE(Name)                          \
    case NodeKind::Name:                                 \
        visitor.visit(static_cast<const Name&>(node));   \
        return;
        CP3_AST_NODE_LIST(CP3_DISPATCH_CASE)
#undef CP3_DISPATCH_CASE
    }
}

// A CRTP base that recursively walks every child and calls `dispatch` for each
// node, in source order. Override `visit(Name&)` to observe a node; the default
// `visitChildren` is regenerated from the node set. Stage authors who want a
// bespoke traversal should subclass AstVisitor directly instead.
//
// NOTE: walkChildren is intentionally NOT auto-generated here - the shapes of
// e.g. optional children vs. lists differ enough that a hand-written walker in
// sema/codegen is clearer and less error-prone. This class exists only to make
// "visit every node once" trivial when that is all that is needed.
class AstWalker : public AstVisitor {
public:
    void walk(Node& node) { dispatch(node, *this); }
    void walkAll(const StmtList& stmts) {
        for (auto& s : stmts) {
            if (s) walk(*s);
        }
    }
    void walkAll(const ExprList& exprs) {
        for (auto& e : exprs) {
            if (e) walk(*e);
        }
    }
};

}  // namespace cp3
