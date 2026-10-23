// =============================================================================
// src/sema/src/checks/class_docs.cpp - the documentation of a class and its
// members.
//
// OWNERSHIP: sema module (src/sema/**). One concern per unit (STYLE.md §8):
// the class body checks live in class_check.cpp, the class documentation here.
//
// A class and its members can carry a doc comment (the JSDoc-style comment
// above the declaration, see support/docs.cpp). This unit publishes those
// comments into `SemaResult::docs`, keyed by the declaration's node id, and
// publishes the members' own types so a hover on a method can show its
// signature.
//
// A doc comment is documentation only: nothing here reports a diagnostic and
// nothing here changes what the program does.
// =============================================================================
#include "detail/analyzer.h"

#include "cp3/core/ast.hpp"
#include "cp3/core/types.hpp"

namespace cp3 {

void Analyzer::publishClassDocs(ClassDeclaration& c) {
    publishDoc(c.nodeId, c.span);
    for (const ClassMemberPtr& member : c.members) {
        if (member == nullptr) continue;
        publishDoc(member->nodeId, member->span);
        if (member->kind != NodeKind::MethodDeclaration) continue;
        const auto& method = static_cast<const MethodDeclaration&>(*member);
        // The method's own type, so a hover on the declaration can show the
        // signature next to the documentation.
        const auto sig = sigs_.find(method.nodeId);
        if (sig == sigs_.end()) continue;
        setType(method, Type::function(sig->second.params, sig->second.ret));
    }
}

}  // namespace cp3
