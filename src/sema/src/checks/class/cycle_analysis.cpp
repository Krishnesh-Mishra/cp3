// =============================================================================
// src/sema/src/checks/class/cycle_analysis.cpp - the whole-program reference
// graph behind the automatic cycle collector.
//
// Builds the strong-reference graph over the class declarations of the unit
// (the imported classes are registered in the analyzer too, so the graph
// covers them), computes its strongly connected components with an iterative
// Tarjan pass, and classifies every class as cycle-capable or acyclic.
//
// The classification is fail-closed: a field whose type the analysis cannot
// see through (an `object`/`unknown` carrier, a type parameter, a native
// handle, a Promise) edges to the OPAQUE sink, and every class that reaches
// the sink is cycle-capable. A `weak` field creates no edge.
// =============================================================================
#include "detail/cycle_analysis.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cp3 {

namespace {

// The fail-closed sink: a type the analysis cannot see through.
constexpr const char* kOpaqueNode = "OPAQUE";

// The node key of a class.
std::string classNode(const std::string& name) { return "C:" + name; }

// The compiler-provided reference types that cannot hold a user class
// reference (they own bytes, a time value or an error payload). They create
// no outgoing edges, so a field typed with one is not an escape.
bool isLeafRuntimeClass(const std::string& name) {
    static const std::set<std::string> leaves = {
        "Buffer",       "Error",     "TypeError",  "RangeError",
        "SyntaxError",  "ReferenceError", "EvalError", "URIError",
        "AggregateError", "Date",    "URL",        "URLSearchParams",
        "TextEncoder",  "TextDecoder", "WeakRef",  "AbortController",
        "AbortSignal",  "Intl",
    };
    return leaves.count(name) != 0;
}

// The standard containers whose type arguments are strong references.
bool isContainerClass(const std::string& name) {
    return name == "Map" || name == "Set" || name == "WeakMap" ||
           name == "WeakSet" || name == "LRUCache" || name == "MultiMap" ||
           name == "ConcurrentMap" || name == "Queue" || name == "Stack" ||
           name == "PriorityQueue" || name == "RingBuffer" || name == "BitSet";
}

// One reference the analysis found in a type.
struct RefTarget {
    std::string node;   // the node key of the target
    std::string path;   // the concrete path that reaches it
    bool opaque = false;
};

// The interface names a structural type label stands for (the closed world:
// an interface-typed field may hold any implementer).
std::vector<std::string> interfacesForLabel(
    const CycleAnalysisInput& in, const std::string& label) {
    const auto it = in.interfaceByLabel.find(label);
    if (it == in.interfaceByLabel.end()) return {};
    return it->second;
}

// Collects the class references a type carries, with the path that reaches
// them. `path` is the concrete field path of the owner ("Node.next").
void collectRefs(const CycleAnalysisInput& in, const Type& t, const std::string& path,
                 std::vector<RefTarget>& out) {
    switch (t.kind) {
        case TypeKind::Class: {
            const std::string& name = t.name;
            if (isLeafRuntimeClass(name)) return;  // no outgoing edge
            if (name == "Promise") {
                // A promise can carry any value: an opaque carrier.
                out.push_back({kOpaqueNode, path + " (promise)", true});
                return;
            }
            if (isContainerClass(name)) {
                // The element types of a container are strong references.
                for (std::size_t i = 0; i < t.parts.size(); ++i) {
                    collectRefs(in, t.parts[i], path + "<" + std::to_string(i) + ">",
                                out);
                }
                return;
            }
            if (!in.classIds.count(name)) {
                // A class this unit cannot see (an import whose declaration
                // was not registered): an opaque carrier.
                out.push_back({kOpaqueNode, path + " (opaque)", true});
                return;
            }
            out.push_back({classNode(name), path, false});
            return;
        }
        case TypeKind::Object: {
            // A structural type: a tuple, a record, or an interface. The
            // shape label carries the member types.
            const std::string& label = t.name;
            if (label.rfind("$t", 0) == 0) {
                // A tuple: the element types are strong references.
                for (std::size_t i = 0; i < t.parts.size(); ++i) {
                    collectRefs(in, t.parts[i], path + "[" + std::to_string(i) + "]",
                                out);
                }
                return;
            }
            const std::vector<std::string> ifaces = interfacesForLabel(in, label);
            if (!ifaces.empty()) {
                // An interface-typed field: every implementer is a candidate.
                for (const std::string& iface : ifaces) {
                    const auto it = in.implementersOf.find(iface);
                    if (it == in.implementersOf.end()) continue;
                    for (const std::string& impl : it->second) {
                        out.push_back({classNode(impl), path + " (" + iface + ")",
                                       false});
                    }
                }
                return;
            }
            // A record: the member types ride in the shape, not in the type.
            // The analyzer passes the resolved member types separately, so a
            // record type contributes no edge here.
            return;
        }
        case TypeKind::Array: {
            if (t.parts.empty()) return;
            collectRefs(in, t.parts.front(), path + "[]", out);
            return;
        }
        case TypeKind::Union:
        case TypeKind::Nullable: {
            for (const Type& part : t.parts) {
                collectRefs(in, part, path, out);
            }
            return;
        }
        case TypeKind::Function: {
            // A closure: the parameter and result types can reference a
            // class, and the closure may capture more. The type-level edge
            // is the conservative one.
            for (const Type& part : t.parts) {
                collectRefs(in, part, path + "()", out);
            }
            return;
        }
        case TypeKind::TypeParam: {
            // A type parameter: the concrete carrier is unknown here.
            out.push_back({kOpaqueNode, path + " (type parameter)", true});
            return;
        }
        case TypeKind::Native: {
            // A native handle can hold a reference the analysis cannot see.
            out.push_back({kOpaqueNode, path + " (native)", true});
            return;
        }
        case TypeKind::Unknown:
        case TypeKind::Error: {
            out.push_back({kOpaqueNode, path + " (unknown)", true});
            return;
        }
        default:
            return;
    }
}

// Tarjan's strongly connected components, iterative (the graph can be deep)
// and deterministic (the node and successor orders are sorted).
std::vector<std::vector<std::string>> tarjan(
    const std::vector<std::string>& nodes,
    const std::map<std::string, std::vector<std::string>>& succ) {
    std::map<std::string, int> index;
    std::map<std::string, int> lowlink;
    std::vector<std::string> stack;
    std::set<std::string> onStack;
    std::vector<std::vector<std::string>> components;
    int counter = 0;

    for (const std::string& root : nodes) {
        if (index.count(root) != 0) continue;
        // The explicit work stack: (node, the successor index to visit next).
        std::vector<std::pair<std::string, std::size_t>> work;
        work.emplace_back(root, 0);
        index[root] = counter;
        lowlink[root] = counter;
        ++counter;
        stack.push_back(root);
        onStack.insert(root);
        while (!work.empty()) {
            auto& [v, childIdx] = work.back();
            const auto it = succ.find(v);
            const std::vector<std::string> empty;
            const std::vector<std::string>& next = it != succ.end() ? it->second : empty;
            if (childIdx < next.size()) {
                const std::string w = next[childIdx++];
                if (index.count(w) == 0) {
                    index[w] = counter;
                    lowlink[w] = counter;
                    ++counter;
                    stack.push_back(w);
                    onStack.insert(w);
                    work.emplace_back(w, 0);
                } else if (onStack.count(w) != 0) {
                    lowlink[v] = std::min(lowlink[v], index[w]);
                }
                continue;
            }
            // The node is fully explored: close it.
            if (lowlink[v] == index[v]) {
                std::vector<std::string> component;
                while (true) {
                    const std::string w = stack.back();
                    stack.pop_back();
                    onStack.erase(w);
                    component.push_back(w);
                    if (w == v) break;
                }
                std::sort(component.begin(), component.end());
                components.push_back(std::move(component));
            }
            work.pop_back();
            if (!work.empty()) {
                const std::string parent = work.back().first;
                lowlink[parent] = std::min(lowlink[parent], lowlink[v]);
            }
        }
    }
    return components;
}

}  // namespace

bool isWalkableHandle(const Type& t) {
    // A direct class handle.
    if (t.kind == TypeKind::Class) return true;
    // An optional of a class handle: `T | null` / `T | undefined`.
    if (t.kind == TypeKind::Nullable) {
        return t.parts.size() == 1 && t.parts.front().kind == TypeKind::Class;
    }
    if (t.kind == TypeKind::Union) {
        const Type* cls = nullptr;
        for (const Type& part : t.parts) {
            if (part.isNullish()) continue;
            if (cls != nullptr || part.kind != TypeKind::Class) return false;
            cls = &part;
        }
        return cls != nullptr;
    }
    return false;
}

// True when the analysis can prove a type holds no class reference anywhere
// (primitives, literals, enums, leaves, tuples/arrays/unions of scalars).
// Everything else is assumed to be able to carry class references the
// analysis cannot see through; a field of such a type that yields no
// visible reference edges to OPAQUE instead (fail closed).
bool knownNoClassRefs(const Type& t) {
    switch (t.kind) {
        case TypeKind::Number:
        case TypeKind::String:
        case TypeKind::Boolean:
        case TypeKind::Undefined:
        case TypeKind::Null:
        case TypeKind::Void:
        case TypeKind::Enum:
        case TypeKind::Literal:
            return true;
        case TypeKind::Class:
            // A leaf runtime class ref (an Error, a Date, a URL) owns no
            // user reference.
            return isLeafRuntimeClass(t.name);
        case TypeKind::Array:
            return t.parts.size() == 1 && knownNoClassRefs(t.parts.front());
        case TypeKind::Union:
        case TypeKind::Nullable:
            for (const Type& part : t.parts) {
                if (!knownNoClassRefs(part)) return false;
            }
            return true;
        case TypeKind::Object: {
            const std::string& label = t.name;
            if (label.rfind("$t", 0) == 0) {
                // A tuple: the element types ride in the type itself.
                for (const Type& part : t.parts) {
                    if (!knownNoClassRefs(part)) return false;
                }
                return true;
            }
            // A record or interface: the member types ride in the shape,
            // not in the type, so the analysis cannot see them here.
            return false;
        }
        default:
            // Function types (a closure can capture anything), type
            // parameters, native handles, unknown: never "no class refs".
            return false;
    }
}

CycleAnalysis analyzeCycles(const CycleAnalysisInput& in) {
    CycleAnalysis out;

    // --- the nodes -----------------------------------------------------------
    std::vector<std::string> nodes;
    nodes.reserve(in.classIds.size() + 1);
    for (const auto& [name, id] : in.classIds) {
        (void)id;
        nodes.push_back(classNode(name));
    }
    nodes.push_back(kOpaqueNode);
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());

    // --- the edges ------------------------------------------------------------
    // The fields of a base class are strong edges of the derived class too.
    std::map<std::string, std::vector<const CycleAnalysisInput::FieldEdge*>> fieldsOf;
    for (const auto& f : in.fields) {
        fieldsOf[f.owner].push_back(&f);
    }
    std::map<std::string, std::vector<std::string>> succ;
    // The edges of a class: the strong edge targets and whether every field
    // that holds one is a walkable handle.
    struct OwnerEdges {
        std::vector<std::string> targets;
        bool allWalkable = true;
        bool hasOpaque = false;
    };
    std::map<std::string, OwnerEdges> ownerEdges;
    for (const auto& [name, id] : in.classIds) {
        (void)id;
        // The inherited fields: the base chain, nearest first.
        std::vector<std::string> chain;
        std::string cursor = name;
        std::set<std::string> seen;
        while (!cursor.empty() && seen.insert(cursor).second) {
            chain.push_back(cursor);
            const auto it = in.baseOf.find(cursor);
            cursor = it != in.baseOf.end() ? it->second : std::string();
        }
        for (const std::string& owner : chain) {
            const auto it = fieldsOf.find(owner);
            if (it == fieldsOf.end()) continue;
            for (const CycleAnalysisInput::FieldEdge* f : it->second) {
                const std::string path =
                    owner + "." + f->field + (f->isStatic ? " (static)" : "");
                std::vector<RefTarget> refs;
                collectRefs(in, f->type, path, refs);
                if (refs.empty() && !knownNoClassRefs(f->type) && !f->weak) {
                    // No visible reference, but the type can hold one the
                    // analysis cannot see through (an object/record/closure
                    // an interface with no known implementers): fail closed.
                    refs.push_back({kOpaqueNode, path + " (opaque)", true});
                }
                // A field with no class reference cannot form an edge: it is
                // safe regardless of its shape (a number, a string, a typed
                // array of numbers, ...).
                if (refs.empty()) continue;
                OwnerEdges& oe = ownerEdges[name];
                if (f->weak) continue;  // a weak link is not a strong edge
                if (!isWalkableHandle(f->type)) oe.allWalkable = false;
                for (const RefTarget& r : refs) {
                    CycleEdge e;
                    e.from = classNode(name);
                    e.to = r.node;
                    e.path = r.path;
                    e.weak = f->weak;
                    e.opaque = r.opaque;
                    out.edges.push_back(e);
                    if (r.opaque) {
                        oe.hasOpaque = true;
                        continue;
                    }
                    oe.targets.push_back(r.node);
                    succ[classNode(name)].push_back(r.node);
                }
            }
        }
    }
    for (auto& [from, list] : succ) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

    // --- the components -------------------------------------------------------
    const std::vector<std::vector<std::string>> components = tarjan(nodes, succ);
    // The classes that reach the OPAQUE sink are cycle-capable (fail closed).
    std::set<std::string> reachesOpaque;
    {
        // A reverse reachability pass from the sink over the strong edges.
        std::map<std::string, std::vector<std::string>> preds;
        for (const auto& [from, list] : succ) {
            for (const std::string& to : list) preds[to].push_back(from);
        }
        std::vector<std::string> work{kOpaqueNode};
        while (!work.empty()) {
            const std::string cur = work.back();
            work.pop_back();
            const auto it = preds.find(cur);
            if (it == preds.end()) continue;
            for (const std::string& p : it->second) {
                if (reachesOpaque.insert(p).second) work.push_back(p);
            }
        }
    }
    for (const std::vector<std::string>& members : components) {
        CycleComponent component;
        component.members = members;
        bool selfEdge = false;
        for (const std::string& m : members) {
            if (m == kOpaqueNode) {
                component.hasOpaque = true;
                continue;
            }
            component.labels.push_back(m.rfind("C:", 0) == 0 ? m.substr(2) : m);
            const auto it = succ.find(m);
            if (it != succ.end() &&
                std::find(it->second.begin(), it->second.end(), m) != it->second.end()) {
                selfEdge = true;
            }
        }
        component.cycleCapable = members.size() > 1 || selfEdge ||
                                 reachesOpaque.count(members.front()) != 0;
        std::sort(component.labels.begin(), component.labels.end());
        out.components.push_back(std::move(component));
    }
    // The concrete field paths that form each cycle-capable component.
    for (CycleComponent& component : out.components) {
        if (!component.cycleCapable) continue;
        std::set<std::string> members(component.members.begin(),
                                      component.members.end());
        for (const CycleEdge& e : out.edges) {
            if (e.weak) continue;
            if (members.count(e.from) == 0) continue;
            if (members.count(e.to) == 0) continue;
            component.paths.push_back(e.path);
        }
        std::sort(component.paths.begin(), component.paths.end());
        component.paths.erase(std::unique(component.paths.begin(),
                                          component.paths.end()),
                              component.paths.end());
    }

    // --- the verdict ----------------------------------------------------------
    std::set<std::string> capable;
    for (const CycleComponent& component : out.components) {
        for (const std::string& label : component.labels) {
            if (component.cycleCapable) capable.insert(label);
        }
    }
    for (const auto& [name, id] : in.classIds) {
        const bool isCapable = capable.count(name) != 0;
        out.cycleCapable[id] = isCapable;
        if (isCapable) {
            out.cycleCapableClasses.push_back(name);
        } else {
            out.acyclicClasses.push_back(name);
        }
    }
    std::sort(out.cycleCapableClasses.begin(), out.cycleCapableClasses.end());
    std::sort(out.acyclicClasses.begin(), out.acyclicClasses.end());

    // --- the collector eligibility -------------------------------------------
    // A class the collector can reclaim: cycle-capable, every strong edge a
    // walkable handle (a direct class handle or an optional of one), no
    // opaque edge, and every edge target eligible too - the collector can
    // only trial-delete an object it understands. The fixpoint removes the
    // classes whose targets are not eligible until it is stable.
    //
    // Builtin classes (the Error family: declNodeId == 0) are excluded: their
    // ABI is fixed to `Ref<Error>`, so a handle conversion would be a
    // user-visible change with no benefit.
    std::set<std::string> eligible;
    for (const auto& [name, id] : in.classIds) {
        if (id == 0) continue;
        if (capable.count(name) == 0) continue;
        const auto it = ownerEdges.find(name);
        if (it == ownerEdges.end() || !it->second.allWalkable ||
            it->second.hasOpaque) {
            continue;
        }
        eligible.insert(name);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = eligible.begin(); it != eligible.end();) {
            const auto oe = ownerEdges.find(*it);
            bool keep = true;
            if (oe != ownerEdges.end()) {
                for (const std::string& target : oe->second.targets) {
                    const std::string cls = target.rfind("C:", 0) == 0
                                                ? target.substr(2)
                                                : target;
                    if (eligible.count(cls) == 0) {
                        keep = false;
                        break;
                    }
                }
            }
            if (!keep) {
                it = eligible.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    for (const auto& [name, id] : in.classIds) {
        const bool isEligible = eligible.count(name) != 0;
        out.cycleEligible[id] = isEligible;
        if (isEligible) out.collectorEligibleClasses.push_back(name);
    }
    std::sort(out.collectorEligibleClasses.begin(),
              out.collectorEligibleClasses.end());
    return out;
}

}  // namespace cp3
