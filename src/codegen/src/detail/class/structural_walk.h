// =============================================================================
// src/codegen/src/detail/structural_walk.h - PRIVATE structural AST walk.
//
// The child-descending walk shared by the capture analysis
// (support/captures.cpp) and the int64 bail scan: `visit` reports every
// identifier read, every identifier write, every binding-pattern name and every
// frame boundary to the subclass. A subclass can stop the walk from `onNode`.
//
// The descenders are grouped by AST shape (frames, value expressions, access
// expressions, operator expressions, branch expressions, statements, loops,
// iterations, try statements) so each stays inside the layout budget; `visit`
// is the dispatcher that routes a node to its group.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "cp3/core/ast.hpp"

namespace cp3 {
namespace detail {

// The names a binding pattern introduces, in source order.
void collectPatternNames(const Node& n, std::vector<std::string>& out);

class StructuralWalk {
public:
    virtual ~StructuralWalk() = default;

    void walk(const Node& n) { visit(n); }

protected:
    bool stopped() const { return stopped_; }

    // Return true to stop the whole walk at this node.
    virtual bool onNode(const Node&) { return false; }
    virtual void readIdentifier(const Identifier&) {}
    virtual void writeIdentifier(const Identifier& id) { readIdentifier(id); }
    // `declaresLocals` separates a variable declarator (a frame local) from a
    // `for` header binding (a per-iteration copy, never a cell).
    virtual void bindNames(const std::vector<std::string>&, bool) {}
    // One `let`/`const`/`var` declarator (initializer included).
    virtual void boundDeclarator(const VariableDeclarator&) {}
    // A function / arrow frame: `params` may be null for the module body.
    virtual void enterFrame(const Node&, const ParamList*) {}
    virtual void leaveFrame() {}

private:
    void visit(const Node& n);
    static bool isPatternNode(const Node& n);

    // A binding pattern in a binding position (`write == false`) declares its
    // names; in an assignment position it writes them. Default expressions,
    // computed keys and nested patterns are walked either way.
    void visitPattern(const Node& n, bool write);
    void visitVarDecl(const VariableDeclaration& vd, bool declaresLocals);

    // The `visit` descenders, one per AST shape group.
    void visitFrame(const Node& n);       // function / arrow
    void visitValueExpr(const Node& n);   // template/array/object/spread/weak
    void visitAccessExpr(const Node& n);  // member/index/call/new
    void visitArithExpr(const Node& n);   // unary/update/binary/logical/await/range
    void visitBranchExpr(const Node& n);  // conditional/assignment
    void visitStatement(const Node& n);   // declaration/block/if/switch/flow
    void visitLoop(const Node& n);        // while/do-while/for
    void visitIteration(const Node& n);   // for-of/for-in
    void visitTry(const Node& n);         // try/catch/finally

    bool stopped_ = false;
    bool patternDeclares_ = false;
};

}  // namespace detail
}  // namespace cp3
