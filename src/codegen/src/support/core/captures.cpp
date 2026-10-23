// =============================================================================
// src/codegen/src/support/captures.cpp - closure-capture analysis
// (STYLE.md §8).
//
// Implements detail/captures.h on top of the structural walk
// (detail/structural_walk.h):
//
//   * `CaptureWalker` keeps one Frame per Program / function / arrow. A name
//     reference resolves to the innermost frame that declares or shadows it; a
//     reference from a deeper frame marks the owner as captured, and a write
//     marks it assigned. A local that is both is a shared cell.
//   * `PatternScan` is the int64 fast path's bail predicate: the first
//     construct the fast path does not model stops the walk.
// =============================================================================
#include "detail/class/captures.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "detail/class/structural_walk.h"

namespace cp3 {
namespace detail {

namespace {

class CaptureWalker final : public StructuralWalk {
public:
    CaptureInfo analyze(const Program& program) {
        Frame frame;
        frame.id = program.nodeId;
        stack_.push_back(std::move(frame));
        for (const auto& s : program.body) {
            if (s) walk(*s);
        }
        finishFrame();

        CaptureInfo info;
        info.arrowOf = std::move(arrowOf_);
        for (const Frame& f : frames_) {
            if (f.id == program.nodeId) continue;  // globals are never cells
            for (const std::string& name : f.locals) {
                if (f.assigned.count(name) != 0 && f.captured.count(name) != 0) {
                    info.frameCells[f.id].insert(name);
                }
            }
        }
        return info;
    }

protected:
    void readIdentifier(const Identifier& id) override { reference(id.name); }

    void writeIdentifier(const Identifier& id) override { assign(id.name); }

    void bindNames(const std::vector<std::string>& names, bool declaresLocals) override {
        for (const std::string& name : names) {
            if (declaresLocals) {
                stack_.back().locals.insert(name);
            } else {
                stack_.back().shadowed.insert(name);
            }
        }
    }

    // `let f = (...) => ...`: the arrow's parameter list carries the defaults
    // a C++ lambda cannot (call.cpp passes them at every call through `f`).
    void boundDeclarator(const VariableDeclarator& d) override {
        if (!d.id || d.id->kind != NodeKind::Identifier || !d.init) return;
        if (d.init->kind != NodeKind::ArrowFunction) return;
        arrowOf_[static_cast<const Identifier&>(*d.id).name] =
            static_cast<const ArrowFunction*>(d.init.get());
    }

    void enterFrame(const Node& node, const ParamList* params) override {
        Frame frame;
        frame.id = node.nodeId;
        if (params) {
            for (const auto& p : *params) {
                if (!p || !p->name) continue;
                std::vector<std::string> names;
                collectPatternNames(*p->name, names);
                for (const std::string& name : names) frame.shadowed.insert(name);
            }
        }
        stack_.push_back(std::move(frame));
    }

    void leaveFrame() override { finishFrame(); }

private:
    struct Frame {
        std::uint32_t id = 0;
        std::unordered_set<std::string> locals;    // cell candidates
        std::unordered_set<std::string> shadowed;  // params + loop bindings
        std::unordered_set<std::string> assigned;  // written in the frame subtree
        std::unordered_set<std::string> captured;  // observed from a nested frame
    };

    void finishFrame() {
        frames_.push_back(std::move(stack_.back()));
        stack_.pop_back();
    }

    // Finds the innermost frame owning `name` (`nullptr` for an unresolvable
    // global/imported name) and reports whether it is an outer frame.
    Frame* owner(const std::string& name, bool& fromNested) {
        for (std::size_t i = stack_.size(); i-- > 0;) {
            if (stack_[i].locals.count(name) != 0 ||
                stack_[i].shadowed.count(name) != 0) {
                fromNested = i + 1 != stack_.size();
                return &stack_[i];
            }
        }
        fromNested = false;
        return nullptr;
    }

    void reference(const std::string& name) {
        bool fromNested = false;
        Frame* f = owner(name, fromNested);
        if (f && fromNested) f->captured.insert(name);
    }

    void assign(const std::string& name) {
        bool fromNested = false;
        Frame* f = owner(name, fromNested);
        if (!f) return;
        f->assigned.insert(name);
        if (fromNested) f->captured.insert(name);
    }

    std::vector<Frame> stack_;
    std::vector<Frame> frames_;
    std::unordered_map<std::string, const ArrowFunction*> arrowOf_;
};

class PatternScan final : public StructuralWalk {
public:
    bool found(const Stmt& body) {
        walk(body);
        return stopped();
    }

protected:
    bool onNode(const Node& n) override {
        switch (n.kind) {
            case NodeKind::ArrowFunction:
            case NodeKind::FunctionDeclaration:
            case NodeKind::ClassDeclaration:
            case NodeKind::ObjectLiteral:
            case NodeKind::ForOfStatement:
            case NodeKind::ForInStatement:
            case NodeKind::ArrayPattern:
            case NodeKind::ObjectPattern:
                return true;
            default:
                return false;
        }
    }
};

}  // namespace

CaptureInfo analyzeCaptures(const Program& program) {
    CaptureWalker walker;
    return walker.analyze(program);
}

bool bodyHasClosureOrPattern(const Stmt& body) {
    PatternScan scan;
    return scan.found(body);
}

}  // namespace detail
}  // namespace cp3
