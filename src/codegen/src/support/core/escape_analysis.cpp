// =============================================================================
// src/codegen/src/support/core/escape_analysis.cpp - conservative local-object
// escape analysis and stack-allocation sites.
//
// This first lowering handles freshly constructed user classes whose only
// references are reads of primitive fields in the defining frame. All other
// shapes retain Ref<T>/make_shared. The intentionally narrow proof is a safe
// base for extending escape facts to aggregates and closures.
// =============================================================================
#include "detail/generator/generator.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "detail/class/class_model.h"
#include "detail/class/structural_walk.h"
#include "detail/generator/family.h"
#include "detail/generator/type_map.h"

namespace cp3 {
namespace {

struct ObjectUse {
    std::uint32_t identifier = 0;
    const MemberExpr* member = nullptr;
    int frame = 0;
};

struct IndexUse {
    std::uint32_t identifier = 0;
    std::uint32_t expression = 0;
    int frame = 0;
};

struct MethodUse {
    std::uint32_t identifier = 0;
    const CallExpr* call = nullptr;
    const MemberExpr* member = nullptr;
    int frame = 0;
};

struct CallArgUse {
    std::uint32_t identifier = 0;
    const CallExpr* call = nullptr;
    std::size_t argument = 0;
    int frame = 0;
};

struct MemberWrite {
    const MemberExpr* member = nullptr;
    int frame = 0;
};

struct IndexedMemberUse {
    std::uint32_t index = 0;
    const MemberExpr* member = nullptr;
    int frame = 0;
};

struct AllocationCandidate {
    const VariableDeclarator* declaration = nullptr;
    const NewExpr* construction = nullptr;
    bool array = false;
    int frame = 0;
};

class EscapeScan final : public detail::StructuralWalk {
public:
    explicit EscapeScan(const SemaResult& sema) : sema_(sema) {}
    std::unordered_map<std::string, AllocationCandidate> candidates;
    std::unordered_map<std::string, std::vector<std::pair<std::uint32_t, int>>> uses;
    std::unordered_map<std::string, std::vector<ObjectUse>> memberUses;
    std::unordered_map<std::string, std::vector<IndexUse>> indexUses;
    std::unordered_map<std::string, std::vector<MethodUse>> methodUses;
    std::unordered_map<std::string, std::vector<CallArgUse>> callArgUses;
    std::unordered_map<std::string, std::vector<MemberWrite>> memberWrites;
    std::unordered_map<std::string, std::vector<IndexedMemberUse>>
        indexedMemberUses;
    std::unordered_set<std::string> indexedMemberWrites;

protected:
    bool onNode(const Node& node) override {
        if (node.kind == NodeKind::MemberExpr) {
            const auto& member = static_cast<const MemberExpr&>(node);
            if (member.object && member.object->kind == NodeKind::Identifier) {
                const auto& id = static_cast<const Identifier&>(*member.object);
                memberUses[key(id)].push_back({id.nodeId, &member, frame_});
            } else if (member.object && member.object->kind == NodeKind::IndexExpr) {
                const auto& index = static_cast<const IndexExpr&>(*member.object);
                if (index.object && index.object->kind == NodeKind::Identifier) {
                    const auto& id = static_cast<const Identifier&>(*index.object);
                    indexedMemberUses[key(id)].push_back(
                        {index.nodeId, &member, frame_});
                }
            }
        } else if (node.kind == NodeKind::IndexExpr) {
            const auto& index = static_cast<const IndexExpr&>(node);
            if (index.object && index.object->kind == NodeKind::Identifier) {
                const auto& id = static_cast<const Identifier&>(*index.object);
                indexUses[key(id)].push_back({id.nodeId, index.nodeId, frame_});
            }
        } else if (node.kind == NodeKind::CallExpr) {
            const auto& call = static_cast<const CallExpr&>(node);
            for (std::size_t i = 0; i < call.arguments.size(); ++i) {
                const Expr* argument = call.arguments[i].get();
                if (argument && argument->kind == NodeKind::Identifier) {
                    const auto& id = static_cast<const Identifier&>(*argument);
                    callArgUses[key(id)].push_back({id.nodeId, &call, i, frame_});
                }
            }
            if (call.callee && call.callee->kind == NodeKind::MemberExpr) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                if (member.object && member.object->kind == NodeKind::Identifier) {
                    const auto& id = static_cast<const Identifier&>(*member.object);
                    methodUses[key(id)].push_back(
                        {id.nodeId, &call, &member, frame_});
                }
            }
        } else if (node.kind == NodeKind::AssignmentExpr) {
            const auto& assignment = static_cast<const AssignmentExpr&>(node);
            markMemberWrite(assignment.target.get());
        } else if (node.kind == NodeKind::UpdateExpr) {
            const auto& update = static_cast<const UpdateExpr&>(node);
            markMemberWrite(update.operand.get());
        }
        return false;
    }

    void readIdentifier(const Identifier& id) override {
        uses[key(id)].push_back({id.nodeId, frame_});
    }

    void boundDeclarator(const VariableDeclarator& declaration) override {
        if (!declaration.id || declaration.id->kind != NodeKind::Identifier ||
            !declaration.init) {
            return;
        }
        const std::string binding = std::to_string(declaration.nodeId);
        if (declaration.init->kind == NodeKind::NewExpr) {
            const auto& construction =
                static_cast<const NewExpr&>(*declaration.init);
            candidates[binding] = {&declaration, &construction, false, frame_};
        } else if (declaration.init->kind == NodeKind::ArrayLiteral &&
                   static_cast<const ArrayLiteral&>(*declaration.init)
                       .elements.empty()) {
            candidates[binding] = {&declaration, nullptr, true, frame_};
        }
    }

    void enterFrame(const Node&, const ParamList*) override { ++frame_; }
    void leaveFrame() override { --frame_; }

private:
    std::string key(const Identifier& id) const {
        const auto resolved = sema_.resolvedBindings.find(id.nodeId);
        if (resolved == sema_.resolvedBindings.end()) return "unresolved:" + id.name;
        return std::to_string(resolved->second);
    }

    void markMemberWrite(const Node* target) {
        if (target == nullptr || target->kind != NodeKind::MemberExpr) return;
        const auto& member = static_cast<const MemberExpr&>(*target);
        if (!member.object) return;
        if (member.object->kind == NodeKind::Identifier) {
            memberWrites[key(static_cast<const Identifier&>(*member.object))]
                .push_back({&member, frame_});
        } else if (member.object->kind == NodeKind::IndexExpr) {
            const auto& index = static_cast<const IndexExpr&>(*member.object);
            if (index.object && index.object->kind == NodeKind::Identifier) {
                indexedMemberWrites.insert(
                    key(static_cast<const Identifier&>(*index.object)));
            }
        }
    }

    const SemaResult& sema_;
    int frame_ = 0;
};

bool scalarFieldType(const Type& type) {
    switch (type.kind) {
        case TypeKind::Number:
        case TypeKind::String:
        case TypeKind::Boolean:
        case TypeKind::Enum:
        case TypeKind::Native:
            return true;
        case TypeKind::Literal:
            return type.literalBase == TypeKind::Number ||
                   type.literalBase == TypeKind::String ||
                   type.literalBase == TypeKind::Boolean;
        default:
            return false;
    }
}

bool pureConstructorValue(const Expr& value) {
    switch (value.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::Identifier:
            return true;
        case NodeKind::UnaryExpr: {
            const auto& unary = static_cast<const UnaryExpr&>(value);
            return unary.operand && unary.op != UnaryOp::Delete &&
                   pureConstructorValue(*unary.operand);
        }
        case NodeKind::BinaryExpr: {
            const auto& binary = static_cast<const BinaryExpr&>(value);
            return binary.left && binary.right &&
                   pureConstructorValue(*binary.left) &&
                   pureConstructorValue(*binary.right);
        }
        case NodeKind::LogicalExpr: {
            const auto& logical = static_cast<const LogicalExpr&>(value);
            return logical.left && logical.right &&
                   pureConstructorValue(*logical.left) &&
                   pureConstructorValue(*logical.right);
        }
        case NodeKind::ConditionalExpr: {
            const auto& conditional = static_cast<const ConditionalExpr&>(value);
            return conditional.condition && conditional.whenTrue &&
                   conditional.whenFalse &&
                   pureConstructorValue(*conditional.condition) &&
                   pureConstructorValue(*conditional.whenTrue) &&
                   pureConstructorValue(*conditional.whenFalse);
        }
        default:
            return false;
    }
}

bool stackSafeConstructor(const detail::ClassModel& model) {
    if (!model.decl || !model.baseName.empty()) {
        return false;
    }
    const ConstructorDeclaration* constructor = nullptr;
    for (const detail::ClassMemberModel& member : model.members) {
        if (member.hasInitializer || member.isWeak) return false;
        if (member.ctor != nullptr) {
            if (constructor != nullptr) return false;
            constructor = member.ctor;
        }
    }
    if (constructor == nullptr || constructor->body == nullptr) return true;
    if (constructor->body->kind != NodeKind::BlockStatement) return false;
    const auto& body = static_cast<const BlockStatement&>(*constructor->body);
    for (const StmtPtr& statement : body.body) {
        if (!statement || statement->kind != NodeKind::ExpressionStatement) {
            return false;
        }
        const auto& expression =
            static_cast<const ExpressionStatement&>(*statement).expression;
        if (!expression || expression->kind != NodeKind::AssignmentExpr) return false;
        const auto& assignment = static_cast<const AssignmentExpr&>(*expression);
        if (assignment.op != AssignOp::Assign || !assignment.target ||
            assignment.target->kind != NodeKind::MemberExpr || !assignment.value) {
            return false;
        }
        const auto& target = static_cast<const MemberExpr&>(*assignment.target);
        if (!target.object || target.object->kind != NodeKind::ThisExpr ||
            target.property.empty() || !pureConstructorValue(*assignment.value)) {
            return false;
        }
        bool fieldFound = false;
        for (const detail::ClassMemberModel& member : model.members) {
            if (member.name == target.property && member.field != nullptr) {
                fieldFound = true;
                break;
            }
        }
        if (!fieldFound) return false;
    }
    return true;
}

const detail::ClassMemberModel* dataField(const detail::ClassModel& model,
                                          const std::string& name) {
    for (const detail::ClassMemberModel& member : model.members) {
        if (member.name == name && member.field != nullptr && !member.isWeak) {
            return &member;
        }
    }
    return nullptr;
}

bool safeGetter(const detail::ClassModel& model, const std::string& name) {
    for (const detail::ClassMemberModel& member : model.members) {
        if (member.name != name || member.method == nullptr || member.isAbstract ||
            member.method->isStatic || !member.method->params.empty() ||
            member.method->body == nullptr ||
            member.method->body->kind != NodeKind::BlockStatement) {
            continue;
        }
        const auto& body = static_cast<const BlockStatement&>(*member.method->body);
        if (body.body.size() != 1 || !body.body.front() ||
            body.body.front()->kind != NodeKind::ReturnStatement) {
            return false;
        }
        const auto& ret = static_cast<const ReturnStatement&>(*body.body.front());
        if (!ret.argument || ret.argument->kind != NodeKind::MemberExpr) return false;
        const auto& field = static_cast<const MemberExpr&>(*ret.argument);
        if (!field.object || field.object->kind != NodeKind::ThisExpr) return false;
        return dataField(model, field.property) != nullptr;
    }
    return false;
}

}  // namespace

void Generator::analyzeEscapeAllocations(const Program& program) {
    stackAllocDecls_.clear();
    stackValueArrayDecls_.clear();
    nonOwningParamDecls_.clear();
    std::unordered_set<std::uint32_t> exportedBindings;
    for (const StmtPtr& statement : program.body) {
        if (!statement || statement->kind != NodeKind::ExportDeclaration) continue;
        const auto& exported = static_cast<const ExportDeclaration&>(*statement);
        if (!exported.declaration ||
            exported.declaration->kind != NodeKind::VariableDeclaration) continue;
        const auto& declaration =
            static_cast<const VariableDeclaration&>(*exported.declaration);
        for (const VariableDeclaratorPtr& binding : declaration.declarations) {
            if (binding) exportedBindings.insert(binding->nodeId);
        }
    }
    EscapeScan scan(sema_);
    for (const StmtPtr& statement : program.body) {
        if (statement) scan.walk(*statement);
    }

    const auto nonOwningArgument = [&](const CallArgUse& use) {
        if (!use.call || !use.call->callee ||
            use.call->callee->kind != NodeKind::Identifier) return false;
        const std::string& name =
            static_cast<const Identifier&>(*use.call->callee).name;
        for (const StmtPtr& statement : program.body) {
            if (!statement || statement->kind != NodeKind::FunctionDeclaration) continue;
            const auto& function = static_cast<const FunctionDeclaration&>(*statement);
            if (function.name != name || use.argument >= function.params.size() ||
                !function.params[use.argument]) continue;
            if (nonOwningParamDecls_.count(function.params[use.argument]->nodeId) != 0)
                return true;
        }
        return false;
    };

    // Summarize simple read-only parameters. A parameter is eligible only
    // when every resolved use is the receiver of a getter whose body is a
    // single return of a field; bare uses, writes, returns and unknown calls
    // keep the owning Ref<T> ABI.
    for (const StmtPtr& statement : program.body) {
        if (!statement || statement->kind != NodeKind::FunctionDeclaration) continue;
        const auto& function = static_cast<const FunctionDeclaration&>(*statement);
        for (const ParameterPtr& parameter : function.params) {
            if (!parameter || !parameter->name ||
                parameter->name->kind != NodeKind::Identifier) continue;
            const auto symbol = sema_.symbols.find(parameter->nodeId);
            if (symbol == sema_.symbols.end() ||
                symbol->second.type.kind != TypeKind::Class) continue;
            const std::string binding = std::to_string(parameter->nodeId);
            const auto reads = scan.uses.find(binding);
            const auto members = scan.memberUses.find(binding);
            const auto methods = scan.methodUses.find(binding);
            if (reads == scan.uses.end() || members == scan.memberUses.end() ||
                methods == scan.methodUses.end()) continue;
            const detail::ClassModel* model =
                detail::ModelRegistry::get().classByName(symbol->second.type.name);
            if (!model) continue;
            std::unordered_set<std::uint32_t> allowed;
            for (const ObjectUse& use : members->second) {
                if (!use.member) continue;
                bool getter = false;
                for (const MethodUse& method : methods->second) {
                    if (method.member == use.member && method.call &&
                        safeGetter(*model, use.member->property)) {
                        getter = true;
                        break;
                    }
                }
                if (getter) allowed.insert(use.identifier);
            }
            if (allowed.empty()) continue;
            bool safe = true;
            for (const auto& read : reads->second) {
                if (allowed.count(read.first) == 0) { safe = false; break; }
            }
            if (safe) nonOwningParamDecls_.insert(parameter->nodeId);
        }
    }

    for (const auto& entry : scan.candidates) {
        const std::string& name = entry.first;
        const AllocationCandidate& candidate = entry.second;
        const auto uses = scan.uses.find(name);
        if (candidate.declaration == nullptr ||
            exportedBindings.count(candidate.declaration->nodeId) != 0) continue;
        if (uses == scan.uses.end() || uses->second.empty()) continue;

        if (candidate.array) {
            // Mutating an array may reallocate its backing storage or change
            // the observed contents. Keep the current owning representation.
            if (scan.memberWrites.count(name) != 0) continue;
            const Type* arrayType = sema_.typeOf(candidate.declaration->nodeId);
            if (arrayType == nullptr || arrayType->kind != TypeKind::Array ||
                arrayType->parts.empty()) {
                continue;
            }
            const auto memberUses = scan.memberUses.find(name);
            const auto indexUses = scan.indexUses.find(name);
            const auto methodUses = scan.methodUses.find(name);
            std::unordered_set<std::uint32_t> allowed;
            bool safe = true;
            if (memberUses != scan.memberUses.end()) {
                for (const ObjectUse& use : memberUses->second) {
                    if (use.member == nullptr || use.frame != candidate.frame ||
                        use.member->optional) {
                        safe = false;
                        break;
                    }
                    const Type* memberType = sema_.typeOf(use.member->nodeId);
                    bool isLength = use.member->property == "length" &&
                                    memberType != nullptr &&
                                    memberType->kind == TypeKind::Number;
                    bool isMutation = false;
                    if (methodUses != scan.methodUses.end()) {
                        for (const MethodUse& method : methodUses->second) {
                            if (method.member == use.member &&
                                method.frame == candidate.frame &&
                                (method.member->property == "push" ||
                                 method.member->property == "pop")) {
                                isMutation = true;
                                break;
                            }
                        }
                    }
                    if (!isLength && !isMutation) {
                        safe = false;
                        break;
                    }
                    allowed.insert(use.identifier);
                }
            }
            if (indexUses != scan.indexUses.end()) {
                for (const IndexUse& use : indexUses->second) {
                    if (use.frame != candidate.frame) {
                        safe = false;
                        break;
                    }
                    allowed.insert(use.identifier);
                }
            }
            for (const auto& use : uses->second) {
                if (use.second != candidate.frame ||
                    allowed.count(use.first) == 0) {
                    safe = false;
                    break;
                }
            }
            if (safe) {
                stackAllocDecls_.insert(candidate.declaration->nodeId);

                // A local array of class handles can store class values when
                // every element is freshly constructed by push(), indexed
                // only for scalar-field reads, and the array itself is proven
                // not to escape by the complete use check above. This removes
                // both the per-element shared_ptr control block and its
                // reference-count traffic while retaining value semantics for
                // every observable operation.
                const Type& elementType = arrayType->parts.front();
                if (elementType.kind == TypeKind::Class &&
                    scan.indexedMemberWrites.count(name) == 0) {
                    const detail::ClassModel* elementModel =
                        detail::ModelRegistry::get().classByName(elementType.name);
                    const auto indexedMembers = scan.indexedMemberUses.find(name);
                    const auto methods = scan.methodUses.find(name);
                    bool valueSafe = elementModel != nullptr &&
                                     stackSafeConstructor(*elementModel) &&
                                     indexUses != scan.indexUses.end() &&
                                     indexedMembers != scan.indexedMemberUses.end() &&
                                     methods != scan.methodUses.end() &&
                                     !methods->second.empty();
                    std::unordered_set<std::uint32_t> memberIndices;
                    if (valueSafe) {
                        for (const IndexedMemberUse& use : indexedMembers->second) {
                            if (use.member == nullptr ||
                                use.frame != candidate.frame ||
                                use.member->optional ||
                                dataField(*elementModel,
                                          use.member->property) == nullptr) {
                                valueSafe = false;
                                break;
                            }
                            const Type* memberType = sema_.typeOf(use.member->nodeId);
                            if (memberType == nullptr ||
                                !scalarFieldType(*memberType)) {
                                valueSafe = false;
                                break;
                            }
                            memberIndices.insert(use.index);
                        }
                    }
                    if (valueSafe) {
                        for (const IndexUse& use : indexUses->second) {
                            if (use.frame != candidate.frame ||
                                memberIndices.count(use.expression) == 0) {
                                valueSafe = false;
                                break;
                            }
                        }
                    }
                    if (valueSafe) {
                        for (const MethodUse& method : methods->second) {
                            if (method.member == nullptr || method.call == nullptr ||
                                method.frame != candidate.frame ||
                                method.member->property != "push" ||
                                method.call->arguments.size() != 1 ||
                                !method.call->arguments.front() ||
                                method.call->arguments.front()->kind !=
                                    NodeKind::NewExpr) {
                                valueSafe = false;
                                break;
                            }
                            const auto& construction = static_cast<const NewExpr&>(
                                *method.call->arguments.front());
                            if (!construction.callee ||
                                construction.callee->kind != NodeKind::Identifier ||
                                static_cast<const Identifier&>(*construction.callee)
                                        .name != elementType.name) {
                                valueSafe = false;
                                break;
                            }
                        }
                    }
                    if (valueSafe) {
                        stackValueArrayDecls_.insert(candidate.declaration->nodeId);
                    }
                }
            }
            continue;
        }
        if (candidate.construction == nullptr) continue;
        const NewExpr& construction = *candidate.construction;
        if (!construction.callee || construction.callee->kind != NodeKind::Identifier) {
            continue;
        }
        const std::string className =
            static_cast<const Identifier&>(*construction.callee).name;
        const detail::ClassModel* model =
            detail::ModelRegistry::get().classByName(className);
        const bool ctorSafe = model != nullptr && stackSafeConstructor(*model);
        if (model == nullptr || !ctorSafe) {
            continue;
        }

        // Writes to scalar fields are safe for a non-escaping local object.
        // Writes of references, cross-frame writes, or unknown properties can
        // create aliases and therefore keep the conservative heap lowering.
        const auto writes = scan.memberWrites.find(name);
        if (writes != scan.memberWrites.end()) {
            bool writesSafe = true;
            for (const MemberWrite& write : writes->second) {
                const Type* memberType = write.member == nullptr
                                              ? nullptr
                                              : sema_.typeOf(write.member->nodeId);
                if (!write.member || write.frame != candidate.frame ||
                    write.member->optional || memberType == nullptr ||
                    !scalarFieldType(*memberType) ||
                    dataField(*model, write.member->property) == nullptr) {
                    writesSafe = false;
                    break;
                }
            }
            if (!writesSafe) continue;
        }

        const auto memberUses = scan.memberUses.find(name);
        const auto methodUses = scan.methodUses.find(name);
        std::unordered_set<std::uint32_t> allowed;
        bool safe = true;
        if (memberUses != scan.memberUses.end()) for (const ObjectUse& use : memberUses->second) {
            if (use.member == nullptr || use.frame != candidate.frame ||
                use.member->optional || use.member->property.empty()) {
                safe = false;
                break;
            }
            const Type* memberType = sema_.typeOf(use.member->nodeId);
            const detail::ClassMemberModel* field =
                dataField(*model, use.member->property);
            bool fieldRead = field != nullptr && memberType != nullptr &&
                             scalarFieldType(*memberType);
            bool getterCall = false;
            if (methodUses != scan.methodUses.end()) {
                for (const MethodUse& method : methodUses->second) {
                    if (method.member == use.member &&
                        method.frame == candidate.frame && method.call != nullptr &&
                        safeGetter(*model, use.member->property) &&
                        sema_.typeOf(method.call->nodeId) != nullptr &&
                        scalarFieldType(*sema_.typeOf(method.call->nodeId))) {
                        getterCall = true;
                        break;
                    }
                }
            }
            if (!fieldRead && !getterCall) { safe = false; break; }
            allowed.insert(use.identifier);
        }
        if (!safe) continue;
        if (allowed.empty() && scan.callArgUses.count(name) == 0) continue;
        for (const auto& use : uses->second) {
            bool safeCall = false;
            const auto callArgs = scan.callArgUses.find(name);
            if (callArgs != scan.callArgUses.end()) {
                for (const CallArgUse& call : callArgs->second) {
                    if (call.identifier == use.first && call.frame == candidate.frame &&
                        nonOwningArgument(call)) { safeCall = true; break; }
                }
            }
            if (use.second != candidate.frame ||
                (allowed.count(use.first) == 0 && !safeCall)) {
                safe = false;
                break;
            }
        }
        if (safe) {
            stackAllocDecls_.insert(candidate.declaration->nodeId);
        }
    }
}

bool Generator::isStackValueArray(const Expr& expression) const {
    const Expr* base = &expression;
    if (base->kind == NodeKind::IndexExpr) {
        base = static_cast<const IndexExpr&>(*base).object.get();
    }
    if (base == nullptr || base->kind != NodeKind::Identifier) return false;
    const auto& id = static_cast<const Identifier&>(*base);
    const auto resolved = sema_.resolvedBindings.find(id.nodeId);
    return resolved != sema_.resolvedBindings.end() &&
           stackValueArrayDecls_.count(resolved->second) != 0;
}

namespace {

const Identifier* identifier(const Node* node) {
    return node && node->kind == NodeKind::Identifier
               ? &static_cast<const Identifier&>(*node)
               : nullptr;
}

bool numberIs(const Expr* expression, double value) {
    return expression && expression->kind == NodeKind::NumberLiteral &&
           static_cast<const NumberLiteral&>(*expression).value == value;
}

bool isName(const Node* node, const std::string& name) {
    const Identifier* id = identifier(node);
    return id && id->name == name;
}

bool assignmentTo(const Stmt* statement, const std::string& name,
                  const AssignmentExpr*& assignment) {
    if (!statement || statement->kind != NodeKind::ExpressionStatement) return false;
    const auto& es = static_cast<const ExpressionStatement&>(*statement);
    if (!es.expression || es.expression->kind != NodeKind::AssignmentExpr) return false;
    assignment = &static_cast<const AssignmentExpr&>(*es.expression);
    return assignment->op == AssignOp::Assign && isName(assignment->target.get(), name);
}

bool unitIncrement(const AssignmentExpr& assignment, const std::string& index) {
    if (!assignment.value || assignment.value->kind != NodeKind::BinaryExpr) return false;
    const auto& add = static_cast<const BinaryExpr&>(*assignment.value);
    return add.op == BinaryOp::Add && isName(add.left.get(), index) &&
           numberIs(add.right.get(), 1.0);
}

bool offsetForIndex(const Expr* offset, const std::string& index,
                    std::size_t width) {
    if (width == 1 && isName(offset, index)) return true;
    if (!offset || offset->kind != NodeKind::BinaryExpr) return false;
    const auto& mul = static_cast<const BinaryExpr&>(*offset);
    return mul.op == BinaryOp::Multiply && isName(mul.left.get(), index) &&
           numberIs(mul.right.get(), static_cast<double>(width));
}

struct ProvenLoop {
    const WhileStatement* loop = nullptr;
    const CallExpr* read = nullptr;
    const MemberExpr* callee = nullptr;
    const Expr* offset = nullptr;
    const Expr* bound = nullptr;
    std::string receiver;
    std::size_t width = 0;
    bool float64 = false;
};

struct ExactRange {
    long double minimum = 0.0L;
    long double maximum = 0.0L;
    unsigned fractionalBits = 0;
};

std::optional<unsigned> exactFractionalBits(double value) {
    if (!std::isfinite(value)) return std::nullopt;
    for (unsigned bits = 0; bits <= 52; ++bits) {
        const long double scaled = std::ldexp(static_cast<long double>(value),
                                              static_cast<int>(bits));
        if (std::trunc(scaled) == scaled &&
            std::fabs(scaled) <= 9007199254740992.0L) {
            return bits;
        }
    }
    return std::nullopt;
}

bool exactAtRange(const ExactRange& range) {
    if (range.fractionalBits > 52 ||
        !std::isfinite(static_cast<double>(range.minimum)) ||
        !std::isfinite(static_cast<double>(range.maximum))) return false;
    const long double scale = std::ldexp(1.0L,
                                         static_cast<int>(range.fractionalBits));
    return std::max(std::fabs(range.minimum), std::fabs(range.maximum)) * scale <=
           9007199254740992.0L;
}

bool sideEffectFreeValue(const Expr& expression) {
    switch (expression.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::Identifier:
        case NodeKind::ThisExpr:
            return true;
        case NodeKind::MemberExpr: {
            const auto& member = static_cast<const MemberExpr&>(expression);
            return !member.optional && member.object &&
                   sideEffectFreeValue(*member.object);
        }
        case NodeKind::IndexExpr: {
            const auto& index = static_cast<const IndexExpr&>(expression);
            return index.object && index.index &&
                   sideEffectFreeValue(*index.object) &&
                   sideEffectFreeValue(*index.index);
        }
        case NodeKind::UnaryExpr: {
            const auto& unary = static_cast<const UnaryExpr&>(expression);
            return unary.op != UnaryOp::Delete && unary.operand &&
                   sideEffectFreeValue(*unary.operand);
        }
        case NodeKind::BinaryExpr: {
            const auto& binary = static_cast<const BinaryExpr&>(expression);
            return binary.left && binary.right &&
                   sideEffectFreeValue(*binary.left) &&
                   sideEffectFreeValue(*binary.right);
        }
        case NodeKind::LogicalExpr: {
            const auto& logical = static_cast<const LogicalExpr&>(expression);
            return logical.left && logical.right &&
                   sideEffectFreeValue(*logical.left) &&
                   sideEffectFreeValue(*logical.right);
        }
        case NodeKind::ConditionalExpr: {
            const auto& conditional = static_cast<const ConditionalExpr&>(expression);
            return conditional.condition && conditional.whenTrue &&
                   conditional.whenFalse &&
                   sideEffectFreeValue(*conditional.condition) &&
                   sideEffectFreeValue(*conditional.whenTrue) &&
                   sideEffectFreeValue(*conditional.whenFalse);
        }
        default:
            return false;
    }
}

std::optional<ExactRange> exactExpressionRange(
    const Expr& expression, const std::string& induction,
    long double inductionMaximum,
    const std::unordered_map<std::string, double>& constants) {
    if (expression.kind == NodeKind::NumberLiteral) {
        const double value = static_cast<const NumberLiteral&>(expression).value;
        const auto bits = exactFractionalBits(value);
        if (!bits) return std::nullopt;
        return ExactRange{value, value, *bits};
    }
    if (expression.kind == NodeKind::Identifier) {
        const std::string& name = static_cast<const Identifier&>(expression).name;
        if (name == induction) return ExactRange{0.0L, inductionMaximum, 0};
        const auto constant = constants.find(name);
        if (constant == constants.end()) return std::nullopt;
        const auto bits = exactFractionalBits(constant->second);
        if (!bits) return std::nullopt;
        return ExactRange{constant->second, constant->second, *bits};
    }
    if (expression.kind != NodeKind::BinaryExpr) return std::nullopt;
    const auto& binary = static_cast<const BinaryExpr&>(expression);
    if (!binary.left || !binary.right) return std::nullopt;
    const auto left = exactExpressionRange(*binary.left, induction,
                                           inductionMaximum, constants);
    const auto right = exactExpressionRange(*binary.right, induction,
                                            inductionMaximum, constants);
    if (!left || !right) return std::nullopt;
    ExactRange result;
    if (binary.op == BinaryOp::Add) {
        result.minimum = left->minimum + right->minimum;
        result.maximum = left->maximum + right->maximum;
        result.fractionalBits = std::max(left->fractionalBits,
                                         right->fractionalBits);
    } else if (binary.op == BinaryOp::Subtract) {
        result.minimum = left->minimum - right->maximum;
        result.maximum = left->maximum - right->minimum;
        result.fractionalBits = std::max(left->fractionalBits,
                                         right->fractionalBits);
    } else if (binary.op == BinaryOp::Multiply) {
        const long double products[] = {
            left->minimum * right->minimum,
            left->minimum * right->maximum,
            left->maximum * right->minimum,
            left->maximum * right->maximum};
        result.minimum = *std::min_element(std::begin(products), std::end(products));
        result.maximum = *std::max_element(std::begin(products), std::end(products));
        result.fractionalBits = left->fractionalBits + right->fractionalBits;
    } else if (binary.op == BinaryOp::Modulo &&
               left->fractionalBits == 0 && right->minimum == right->maximum &&
               right->minimum > 0.0L &&
               std::trunc(right->minimum) == right->minimum &&
               left->minimum >= 0.0L) {
        result.minimum = 0.0L;
        result.maximum = std::min(left->maximum, right->maximum - 1.0L);
        result.fractionalBits = 0;
    } else {
        return std::nullopt;
    }
    if (!exactAtRange(result)) return std::nullopt;
    return result;
}

class IdentifierWrites final : public detail::StructuralWalk {
public:
    explicit IdentifierWrites(const SemaResult& sema) : sema_(sema) {}

    std::unordered_set<std::uint32_t> bindings;

protected:
    void writeIdentifier(const Identifier& id) override {
        const auto binding = sema_.resolvedBindings.find(id.nodeId);
        if (binding != sema_.resolvedBindings.end()) bindings.insert(binding->second);
    }

private:
    const SemaResult& sema_;
};

class ArrayUseScan final : public detail::StructuralWalk {
public:
    explicit ArrayUseScan(const SemaResult& sema) : sema_(sema) {}
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> reads;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> indexReads;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> indexBaseReads;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> bufferReads;
    std::unordered_set<std::uint32_t> writes;

protected:
    bool onNode(const Node& node) override {
        if (node.kind == NodeKind::IndexExpr) {
            const auto& index = static_cast<const IndexExpr&>(node);
            if (const Identifier* id = identifier(index.object.get())) {
                const auto binding = sema_.resolvedBindings.find(id->nodeId);
                if (binding != sema_.resolvedBindings.end()) {
                    indexReads[binding->second].push_back(index.nodeId);
                    indexBaseReads[binding->second].push_back(id->nodeId);
                }
            }
        } else if (node.kind == NodeKind::MemberExpr) {
            const auto& member = static_cast<const MemberExpr&>(node);
            if (member.property == "buffer") {
                if (const Identifier* id = identifier(member.object.get())) {
                    const auto binding = sema_.resolvedBindings.find(id->nodeId);
                    if (binding != sema_.resolvedBindings.end()) {
                        bufferReads[binding->second].push_back(id->nodeId);
                    }
                }
            }
        }
        return false;
    }

    void readIdentifier(const Identifier& id) override {
        const auto binding = sema_.resolvedBindings.find(id.nodeId);
        if (binding != sema_.resolvedBindings.end()) {
            reads[binding->second].insert(id.nodeId);
        }
    }

    void writeIdentifier(const Identifier& id) override {
        const auto binding = sema_.resolvedBindings.find(id.nodeId);
        if (binding != sema_.resolvedBindings.end()) writes.insert(binding->second);
    }

private:
    const SemaResult& sema_;
};

class ArrayIndexWriteScan final : public detail::StructuralWalk {
public:
    explicit ArrayIndexWriteScan(const SemaResult& sema) : sema_(sema) {}
    std::unordered_map<std::uint32_t, std::vector<const IndexExpr*>> writes;

protected:
    bool onNode(const Node& node) override {
        const Expr* target = nullptr;
        if (node.kind == NodeKind::AssignmentExpr) {
            const auto& assignment = static_cast<const AssignmentExpr&>(node);
            if (assignment.op == AssignOp::Assign) target = assignment.target.get();
        } else if (node.kind == NodeKind::UpdateExpr) {
            target = static_cast<const UpdateExpr&>(node).operand.get();
        }
        if (!target || target->kind != NodeKind::IndexExpr) return false;
        const auto& index = static_cast<const IndexExpr&>(*target);
        const Identifier* id = identifier(index.object.get());
        if (!id) return false;
        const auto binding = sema_.resolvedBindings.find(id->nodeId);
        if (binding != sema_.resolvedBindings.end()) {
            writes[binding->second].push_back(&index);
        }
        return false;
    }

private:
    const SemaResult& sema_;
};

class DataViewUseScan final : public detail::StructuralWalk {
public:
    explicit DataViewUseScan(const SemaResult& sema) : sema_(sema) {}
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> reads;
    std::unordered_map<std::uint32_t, std::vector<const CallExpr*>> calls;

protected:
    bool onNode(const Node& node) override {
        if (node.kind != NodeKind::CallExpr) return false;
        const auto& call = static_cast<const CallExpr&>(node);
        if (!call.callee || call.callee->kind != NodeKind::MemberExpr) return false;
        const auto& member = static_cast<const MemberExpr&>(*call.callee);
        const Identifier* receiver = identifier(member.object.get());
        if (!receiver) return false;
        const auto binding = sema_.resolvedBindings.find(receiver->nodeId);
        if (binding != sema_.resolvedBindings.end()) {
            calls[binding->second].push_back(&call);
        }
        return false;
    }

    void readIdentifier(const Identifier& id) override {
        const auto binding = sema_.resolvedBindings.find(id.nodeId);
        if (binding != sema_.resolvedBindings.end()) {
            reads[binding->second].insert(id.nodeId);
        }
    }

private:
    const SemaResult& sema_;
};

const VariableDeclarator* findDeclarator(const Program& program,
                                         std::uint32_t nodeId) {
    for (const StmtPtr& statement : program.body) {
        if (!statement) continue;
        const VariableDeclaration* declaration = nullptr;
        if (statement->kind == NodeKind::VariableDeclaration) {
            declaration = &static_cast<const VariableDeclaration&>(*statement);
        } else if (statement->kind == NodeKind::ExportDeclaration) {
            const auto& exported = static_cast<const ExportDeclaration&>(*statement);
            if (exported.declaration &&
                exported.declaration->kind == NodeKind::VariableDeclaration) {
                declaration = &static_cast<const VariableDeclaration&>(
                    *exported.declaration);
            }
        }
        if (!declaration) continue;
        for (const VariableDeclaratorPtr& item : declaration->declarations) {
            if (item && item->nodeId == nodeId) return item.get();
        }
    }
    return nullptr;
}

// A DataView may only use this raw-pointer path when its backing bytes come
// from a fresh, fixed-size, ordinary typed array whose only aliases are direct
// indexed accesses and the one DataView buffer construction. This excludes
// shared buffers, escaped ArrayBuffers, buffer replacement, and unknown calls.
bool hasStableTypedArrayBacking(const Program& program, std::uint32_t binding,
                               const std::unordered_set<std::uint32_t>& exports,
                               const IdentifierWrites& writes,
                               const ArrayUseScan& uses,
                               const SemaResult& sema) {
    if (exports.count(binding) != 0 || writes.bindings.count(binding) != 0) {
        return false;
    }
    const VariableDeclarator* declaration = findDeclarator(program, binding);
    if (!declaration || !declaration->init ||
        declaration->init->kind != NodeKind::NewExpr) return false;
    const auto& allocation = static_cast<const NewExpr&>(*declaration->init);
    if (!allocation.callee || allocation.callee->kind != NodeKind::Identifier ||
        allocation.arguments.size() != 1 || !allocation.arguments.front()) {
        return false;
    }
    // The resolved declaration type identifies the concrete typed-array
    // family. Requiring a numeric length constructor rejects buffer-backed
    // views, including SharedArrayBuffer-backed views.
    if (allocation.arguments.front()->kind != NodeKind::Identifier &&
        allocation.arguments.front()->kind != NodeKind::NumberLiteral) {
        return false;
    }
    const Type* arrayType = sema.typeOf(declaration->nodeId);
    const detail::CppInfo arrayInfo = arrayType ? detail::classify(*arrayType)
                                                : detail::CppInfo{};
    if (arrayInfo.shape != detail::Shape::TypedArray) return false;
    const Type* lengthType = sema.typeOf(allocation.arguments.front()->nodeId);
    if (!lengthType ||
        (lengthType->kind != TypeKind::Number &&
         lengthType->kind != TypeKind::Native &&
         !(lengthType->kind == TypeKind::Literal &&
           lengthType->literalBase == TypeKind::Number))) return false;
    const auto buffer = uses.bufferReads.find(binding);
    const auto indexed = uses.indexBaseReads.find(binding);
    const auto reads = uses.reads.find(binding);
    if (buffer == uses.bufferReads.end() || buffer->second.size() != 1 ||
        reads == uses.reads.end()) return false;
    std::unordered_set<std::uint32_t> allowed(buffer->second.begin(),
                                              buffer->second.end());
    if (indexed != uses.indexBaseReads.end()) {
        allowed.insert(indexed->second.begin(), indexed->second.end());
    }
    return !allowed.empty() && reads->second == allowed;
}

std::optional<ExactRange> proveExactArrayFill(
    const WhileStatement& loop, const std::string& arrayName,
    const std::string& indexName,
    const std::unordered_set<std::string>& zeroIndices,
    const std::unordered_map<std::string, double>& constants,
    const SemaResult& sema, const ArrayIndexWriteScan& writes,
    const ArrayUseScan& uses, std::uint32_t arrayBinding,
    const IndexExpr** fillTarget = nullptr) {
    if (!zeroIndices.count(indexName) || !loop.condition ||
        loop.condition->kind != NodeKind::BinaryExpr || !loop.body ||
        loop.body->kind != NodeKind::BlockStatement) return std::nullopt;
    const auto& condition = static_cast<const BinaryExpr&>(*loop.condition);
    const Identifier* bound = identifier(condition.right.get());
    if (condition.op != BinaryOp::LessThan ||
        !isName(condition.left.get(), indexName) || !bound ||
        !constants.count(bound->name)) return std::nullopt;
    const double length = constants.at(bound->name);
    if (!(length > 0.0) || std::floor(length) != length ||
        length > 9007199254740991.0) return std::nullopt;
    const auto& body = static_cast<const BlockStatement&>(*loop.body);
    if (body.body.size() != 3 || !body.body.back() ||
        body.body.back()->kind != NodeKind::ExpressionStatement) return std::nullopt;
    const auto& incrementStmt = static_cast<const ExpressionStatement&>(*body.body.back());
    if (!incrementStmt.expression ||
        incrementStmt.expression->kind != NodeKind::AssignmentExpr) return std::nullopt;
    const auto& increment = static_cast<const AssignmentExpr&>(*incrementStmt.expression);
    if (increment.op != AssignOp::Assign || !unitIncrement(increment, indexName)) {
        return std::nullopt;
    }
    const AssignmentExpr* fill = nullptr;
    for (std::size_t i = 0; i + 1 < body.body.size(); ++i) {
        if (!body.body[i] || body.body[i]->kind != NodeKind::ExpressionStatement) {
            return std::nullopt;
        }
        const auto& statement = static_cast<const ExpressionStatement&>(*body.body[i]);
        if (!statement.expression || statement.expression->kind != NodeKind::AssignmentExpr) {
            return std::nullopt;
        }
        const auto& assignment = static_cast<const AssignmentExpr&>(*statement.expression);
        if (assignment.op != AssignOp::Assign || !assignment.value ||
            !sideEffectFreeValue(*assignment.value) || !assignment.target ||
            assignment.target->kind != NodeKind::IndexExpr) return std::nullopt;
        const auto& target = static_cast<const IndexExpr&>(*assignment.target);
        const Identifier* targetArray = identifier(target.object.get());
        const Type* targetType = targetArray ? sema.typeOf(targetArray->nodeId) : nullptr;
        const detail::CppInfo targetInfo = targetType
            ? detail::classify(*targetType) : detail::CppInfo{};
        if (!targetArray || targetInfo.shape != detail::Shape::TypedArray ||
            !isName(target.index.get(), indexName)) return std::nullopt;
        if (!isName(target.object.get(), arrayName)) continue;
        if (fill) return std::nullopt;
        fill = &assignment;
    }
    if (!fill) return std::nullopt;
    if (fillTarget) *fillTarget = &static_cast<const IndexExpr&>(*fill->target);
    const auto allWrites = writes.writes.find(arrayBinding);
    if (allWrites == writes.writes.end() || allWrites->second.size() != 1 ||
        allWrites->second.front()->nodeId !=
            static_cast<const IndexExpr&>(*fill->target).nodeId) return std::nullopt;
    const auto indexReads = uses.indexReads.find(arrayBinding);
    const auto indexBaseReads = uses.indexBaseReads.find(arrayBinding);
    const auto bufferReads = uses.bufferReads.find(arrayBinding);
    const auto allReads = uses.reads.find(arrayBinding);
    if (indexReads == uses.indexReads.end() || indexReads->second.size() != 1 ||
        indexReads->second.front() != allWrites->second.front()->nodeId ||
        indexBaseReads == uses.indexBaseReads.end() ||
        indexBaseReads->second.size() != 1 ||
        bufferReads == uses.bufferReads.end() || bufferReads->second.size() != 1 ||
        allReads == uses.reads.end()) return std::nullopt;
    std::unordered_set<std::uint32_t> allowedReads{
        indexBaseReads->second.front(), bufferReads->second.front()};
    if (allowedReads.size() != 2 || allReads->second != allowedReads ||
        uses.writes.count(arrayBinding) != 0) return std::nullopt;
    const Type* indexType = sema.typeOf(condition.left->nodeId);
    if (indexType == nullptr ||
        (indexType->kind != TypeKind::Native && indexType->kind != TypeKind::Number)) {
        return std::nullopt;
    }
    return exactExpressionRange(*fill->value, indexName,
                                static_cast<long double>(length - 1.0), constants);
}

bool proveDataViewLoop(const WhileStatement& loop, const std::string& index,
                       const std::unordered_set<std::string>& zeroIndices,
                       const std::unordered_map<std::string, double>& constants,
                       const SemaResult& sema, ProvenLoop& proven) {
    if (!zeroIndices.count(index) || !loop.condition ||
        loop.condition->kind != NodeKind::BinaryExpr || !loop.body ||
        loop.body->kind != NodeKind::BlockStatement) return false;
    const auto& condition = static_cast<const BinaryExpr&>(*loop.condition);
    if (condition.op != BinaryOp::LessThan || !isName(condition.left.get(), index)) return false;
    const Identifier* boundName = identifier(condition.right.get());
    if (!boundName || !constants.count(boundName->name)) return false;
    if (constants.at(boundName->name) < 0.0 ||
        std::floor(constants.at(boundName->name)) != constants.at(boundName->name) ||
        constants.at(boundName->name) > 9007199254740991.0) return false;

    const auto& body = static_cast<const BlockStatement&>(*loop.body);
    if (body.body.size() != 2) return false;
    const AssignmentExpr* reduce = nullptr;
    const AssignmentExpr* increment = nullptr;
    if (!assignmentTo(body.body[0].get(), "", reduce)) {
        // The reduction target is discovered from its assignment node below.
        if (!body.body[0] || body.body[0]->kind != NodeKind::ExpressionStatement) return false;
        const auto& es = static_cast<const ExpressionStatement&>(*body.body[0]);
        if (!es.expression || es.expression->kind != NodeKind::AssignmentExpr) return false;
        reduce = &static_cast<const AssignmentExpr&>(*es.expression);
    }
    if (!reduce || reduce->op != AssignOp::Assign || !identifier(reduce->target.get()) ||
        !reduce->value || reduce->value->kind != NodeKind::BinaryExpr) return false;
    if (!assignmentTo(body.body[1].get(), index, increment) || !unitIncrement(*increment, index)) return false;
    const auto& sum = static_cast<const BinaryExpr&>(*reduce->value);
    if (sum.op != BinaryOp::Add || !isName(sum.left.get(), identifier(reduce->target.get())->name) ||
        !sum.right || sum.right->kind != NodeKind::CallExpr) return false;
    const auto& call = static_cast<const CallExpr&>(*sum.right);
    if (!call.callee || call.callee->kind != NodeKind::MemberExpr || call.optional) return false;
    const auto& member = static_cast<const MemberExpr&>(*call.callee);
    const Identifier* receiver = identifier(member.object.get());
    if (!receiver || member.optional || call.arguments.empty()) return false;

    std::size_t width = 0;
    bool float64 = false;
    if (member.property == "getUint8" && call.arguments.size() == 1) width = 1;
    if (member.property == "getFloat64" && call.arguments.size() == 2 &&
        call.arguments[1] && call.arguments[1]->kind == NodeKind::BoolLiteral &&
        static_cast<const BoolLiteral&>(*call.arguments[1]).value) {
        width = 8;
        float64 = true;
    }
    if (!width || !offsetForIndex(call.arguments[0].get(), index, width)) return false;

    const Type* receiverType = sema.typeOf(receiver->nodeId);
    if (!receiverType || receiverType->kind != TypeKind::Class || receiverType->name != "DataView") return false;

    // The receiver's declaration is checked in the outer sequential scan.
    proven.loop = &loop;
    proven.read = &call;
    proven.callee = &member;
    proven.offset = call.arguments[0].get();
    proven.bound = condition.right.get();
    proven.receiver = receiver->name;
    proven.width = width;
    proven.float64 = float64;
    return true;
}

}  // namespace

void Generator::analyzeDataViewLoops(const Program& program) {
    fastDataViewReads_.clear();
    fastDataViewLoops_.clear();
    fastTypedArrayLoops_.clear();
    fastTypedArrayWriteSites_.clear();
    activeFastTypedArrays_.clear();
    activeFastDataViews_.clear();
    std::unordered_set<std::uint32_t> fixedDataViews;
    std::unordered_set<std::uint32_t> exportedBindings;
    std::unordered_map<std::uint32_t, std::uint32_t> dataViewSources;
    std::unordered_set<std::string> zeroIndices;
    std::unordered_map<std::string, double> constants;
    IdentifierWrites writes(sema_);
    ArrayUseScan arrayUses(sema_);
    ArrayIndexWriteScan arrayWrites(sema_);
    DataViewUseScan dataViewUses(sema_);
    for (const StmtPtr& statement : program.body) {
        if (statement) {
            writes.walk(*statement);
            arrayUses.walk(*statement);
            arrayWrites.walk(*statement);
            dataViewUses.walk(*statement);
        }
        if (statement && statement->kind == NodeKind::ExportDeclaration) {
            const auto& exported = static_cast<const ExportDeclaration&>(*statement);
            if (exported.declaration &&
                exported.declaration->kind == NodeKind::VariableDeclaration) {
                const auto& declaration = static_cast<const VariableDeclaration&>(
                    *exported.declaration);
                for (const VariableDeclaratorPtr& binding : declaration.declarations) {
                    if (binding) exportedBindings.insert(binding->nodeId);
                }
            }
        }
    }

    for (const StmtPtr& statement : program.body) {
        if (!statement || statement->kind != NodeKind::VariableDeclaration) continue;
        const auto& declaration = static_cast<const VariableDeclaration&>(*statement);
        for (const auto& item : declaration.declarations) {
            if (!item || !item->id || item->id->kind != NodeKind::Identifier || !item->init) continue;
            if (item->init->kind != NodeKind::NewExpr) continue;
            const auto& dv = static_cast<const NewExpr&>(*item->init);
            if (!identifier(dv.callee.get()) ||
                static_cast<const Identifier&>(*dv.callee).name != "DataView" ||
                dv.arguments.empty() || !dv.arguments[0] ||
                dv.arguments[0]->kind != NodeKind::MemberExpr) continue;
            const auto& buffer = static_cast<const MemberExpr&>(*dv.arguments[0]);
            const Type* sourceType = buffer.object ? sema_.typeOf(buffer.object->nodeId) : nullptr;
            const detail::CppInfo sourceInfo = sourceType
                ? detail::classify(*sourceType) : detail::CppInfo{};
            if (buffer.property == "buffer" &&
                sourceInfo.shape == detail::Shape::TypedArray) {
                const Identifier* source = identifier(buffer.object.get());
                const auto resolvedSource = source
                    ? sema_.resolvedBindings.find(source->nodeId)
                    : sema_.resolvedBindings.end();
                if (source && resolvedSource != sema_.resolvedBindings.end() &&
                    exportedBindings.count(item->nodeId) == 0 &&
                    !writes.bindings.count(item->nodeId) &&
                    hasStableTypedArrayBacking(program, resolvedSource->second,
                                               exportedBindings, writes,
                                               arrayUses, sema_)) {
                    fixedDataViews.insert(item->nodeId);
                    dataViewSources[item->nodeId] = resolvedSource->second;
                }
            }
        }
    }

    std::unordered_map<std::uint32_t, ExactRange> exactArrayContents;

    const auto analyzeList = [&](const StmtList& statements, auto&& self,
                                 std::unordered_set<std::string>& zero,
                                 std::unordered_map<std::string, double>& numbers) -> void {
        for (const StmtPtr& statement : statements) {
            if (!statement) continue;
            if (statement->kind == NodeKind::VariableDeclaration) {
                const auto& declaration = static_cast<const VariableDeclaration&>(*statement);
                for (const auto& item : declaration.declarations) {
                    if (!item || !item->id || item->id->kind != NodeKind::Identifier || !item->init) continue;
                    const std::string name = static_cast<const Identifier&>(*item->id).name;
                    if (item->init->kind == NodeKind::NumberLiteral) {
                        const double value = static_cast<const NumberLiteral&>(*item->init).value;
                        if (declaration.kind == VarKind::Const) numbers[name] = value;
                        if (value == 0.0) zero.insert(name);
                        else zero.erase(name);
                    } else {
                        zero.erase(name);
                    }
                }
                continue;
            }
            if (statement->kind == NodeKind::ExpressionStatement) {
                const auto& es = static_cast<const ExpressionStatement&>(*statement);
                if (es.expression && es.expression->kind == NodeKind::AssignmentExpr) {
                    const auto& assignment = static_cast<const AssignmentExpr&>(*es.expression);
                    const Identifier* target = identifier(assignment.target.get());
                    if (target && assignment.op == AssignOp::Assign && numberIs(assignment.value.get(), 0.0)) {
                        zero.insert(target->name);
                    } else if (target) {
                        zero.erase(target->name);
                    }
                }
                continue;
            }
            if (statement->kind == NodeKind::BlockStatement) {
                const auto& block = static_cast<const BlockStatement&>(*statement);
                self(block.body, self, zero, numbers);
                continue;
            }
            if (statement->kind != NodeKind::WhileStatement) continue;

            const auto& loop = static_cast<const WhileStatement&>(*statement);
            const Identifier* index = loop.condition && loop.condition->kind == NodeKind::BinaryExpr
                ? identifier(static_cast<const BinaryExpr&>(*loop.condition).left.get()) : nullptr;
            if (index) {
                for (const auto& source : dataViewSources) {
                    const std::uint32_t sourceBinding = source.second;
                    if (exportedBindings.count(sourceBinding) != 0) continue;
                    const VariableDeclarator* arrayDecl =
                        findDeclarator(program, sourceBinding);
                    if (!arrayDecl || !arrayDecl->init ||
                        arrayDecl->init->kind != NodeKind::NewExpr) continue;
                    const auto& allocation = static_cast<const NewExpr&>(*arrayDecl->init);
                    const Identifier* constructor = identifier(allocation.callee.get());
                    const detail::FamilyClass* family = constructor
                        ? detail::familyClass(constructor->name) : nullptr;
                    if (!family || family->tag[0] == '\0' ||
                        allocation.arguments.size() != 1 ||
                        !identifier(allocation.arguments.front().get())) continue;
                    const std::string& lengthName = static_cast<const Identifier&>(
                        *allocation.arguments.front()).name;
                    const auto length = numbers.find(lengthName);
                    if (length == numbers.end()) continue;
                    const Identifier* arrayName = arrayDecl->id
                        ? identifier(arrayDecl->id.get()) : nullptr;
                    if (!arrayName) continue;
                    const IndexExpr* fillTarget = nullptr;
                    const auto exact = proveExactArrayFill(
                        loop, arrayName->name, index->name, zero, numbers, sema_,
                        arrayWrites, arrayUses, sourceBinding, &fillTarget);
                    if (!exact || !fillTarget) continue;
                    const double rangeLength = length->second;
                    const auto* condition = static_cast<const BinaryExpr*>(
                        loop.condition.get());
                    const Identifier* loopBound = identifier(condition->right.get());
                    if (!loopBound || !numbers.count(loopBound->name) ||
                        numbers.at(loopBound->name) != rangeLength) continue;
                    FastTypedArrayWrite write{
                        sourceBinding, arrayName->name, fillTarget->index.get(),
                        static_cast<std::size_t>(family->bytes), family->tag};
                    FastTypedArrayLoop& fastLoop = fastTypedArrayLoops_[loop.nodeId];
                    fastLoop.bound = condition->right.get();
                    fastLoop.writes.push_back(write);
                    fastTypedArrayWriteSites_[fillTarget->nodeId] = write;
                    const bool exactStoredValues =
                        constructor->name == "Float64Array" ||
                        (constructor->name == "Uint8Array" &&
                         exact->fractionalBits == 0 &&
                         exact->minimum >= 0.0L && exact->maximum <= 255.0L);
                    if (exactStoredValues) {
                        exactArrayContents[sourceBinding] = *exact;
                    }
                }
                ProvenLoop proof;
                if (proveDataViewLoop(loop, index->name, zero, numbers, sema_, proof)) {
                    const Identifier* viewUse = identifier(proof.callee->object.get());
                    const auto viewBinding = viewUse
                        ? sema_.resolvedBindings.find(viewUse->nodeId)
                        : sema_.resolvedBindings.end();
                    if (viewUse && viewBinding != sema_.resolvedBindings.end() &&
                        fixedDataViews.count(viewBinding->second) != 0) {
                        const auto viewReads = dataViewUses.reads.find(viewBinding->second);
                        const auto viewCalls = dataViewUses.calls.find(viewBinding->second);
                        const bool isolatedRead =
                            viewReads != dataViewUses.reads.end() &&
                            viewReads->second.size() == 1 &&
                            viewReads->second.count(viewUse->nodeId) != 0 &&
                            viewCalls != dataViewUses.calls.end() &&
                            viewCalls->second.size() == 1 &&
                            viewCalls->second.front() == proof.read;
                        if (isolatedRead) {
                            bool exactReduction = false;
                            const auto sourceBinding =
                                dataViewSources.find(viewBinding->second);
                            if (sourceBinding != dataViewSources.end()) {
                                const VariableDeclarator* sourceDecl =
                                    findDeclarator(program, sourceBinding->second);
                                const auto* allocation = sourceDecl && sourceDecl->init &&
                                    sourceDecl->init->kind == NodeKind::NewExpr
                                    ? static_cast<const NewExpr*>(sourceDecl->init.get())
                                    : nullptr;
                                const Identifier* sourceType = allocation
                                    ? identifier(allocation->callee.get()) : nullptr;
                                const bool matchingType = sourceType &&
                                    ((proof.float64 && sourceType->name == "Float64Array") ||
                                     (!proof.float64 && sourceType->name == "Uint8Array"));
                                const auto values = exactArrayContents.find(
                                    sourceBinding->second);
                                const auto boundName = identifier(proof.bound);
                                if (matchingType && values != exactArrayContents.end() &&
                                    boundName && numbers.count(boundName->name)) {
                                    const long double count =
                                        numbers.at(boundName->name);
                                    const long double maxValue = std::max(
                                        std::fabs(values->second.minimum),
                                        std::fabs(values->second.maximum));
                                    const long double units = std::ldexp(
                                        count * maxValue,
                                        static_cast<int>(values->second.fractionalBits));
                                    exactReduction =
                                        std::isfinite(static_cast<double>(units)) &&
                                        units <= 9007199254740992.0L;
                                }
                            }
                            fastDataViewLoops_[loop.nodeId] = {
                                proof.receiver, proof.bound, proof.width,
                                exactReduction};
                            fastDataViewReads_[proof.read->nodeId] = {
                                loop.nodeId, proof.receiver, proof.offset,
                                proof.width, proof.float64};
                        }
                    }
                }
                // A while may change its induction variable regardless of
                // whether the fast-path proof succeeds.
                zero.erase(index->name);
            }
            if (loop.body && loop.body->kind == NodeKind::BlockStatement) {
                auto nestedZero = zero;
                auto nestedNumbers = numbers;
                const auto& block = static_cast<const BlockStatement&>(*loop.body);
                self(block.body, self, nestedZero, nestedNumbers);
            }
        }
    };
    analyzeList(program.body, analyzeList, zeroIndices, constants);
}

}  // namespace cp3
