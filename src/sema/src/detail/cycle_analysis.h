// =============================================================================
// src/sema/src/detail/cycle_analysis.h - the whole-program reference graph
// behind the automatic cycle collector.
//
// The graph answers one question per type: can this type hold a strong
// reference back to itself? The answer decides whether a value of that type
// needs the trial-deletion collector (cycle-capable) or stays on plain
// refcounting (acyclic, zero collector cost).
//
// A node is a type that can hold a strong reference to another type:
//   "C:<class>"   a class type
//   "OPAQUE"      the fail-closed sink: an `object`/`unknown` carrier, a
//                 type parameter, a native handle or a Promise - something
//                 the analysis cannot see through, so any class that reaches
//                 it is treated as cycle-capable.
//
// An edge is a strong reference. A `weak` field creates no edge (the link
// does not keep its target alive). A type is cycle-capable when it sits in a
// strongly connected component of size > 1, when it has a (transitive) self
// edge, or when it reaches the OPAQUE sink (the fail-closed rule).
//
// The graph is built per class declaration. A generic class contributes the
// edges of its declaration; an instantiation substitutes the type arguments
// where the sema knows them, and a field still typed with a type parameter
// edges to OPAQUE (the fail-closed rule).
// =============================================================================
#ifndef CP3_SEMA_DETAIL_CYCLE_ANALYSIS_H
#define CP3_SEMA_DETAIL_CYCLE_ANALYSIS_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "cp3/core/types.hpp"

namespace cp3 {

// One strong reference in the graph, with the concrete path that forms it.
struct CycleEdge {
    std::string from;    // the node key of the owner
    std::string to;      // the node key of the target
    std::string path;    // "Node.next", "Node.items[]", "Node.map<K,V>"
    bool weak = false;   // a `weak` field: recorded, but not a strong edge
    bool opaque = false; // the target is the fail-closed sink
};

// One strongly connected component of the graph.
struct CycleComponent {
    std::vector<std::string> members;  // the node keys, sorted
    std::vector<std::string> labels;   // the printable class names
    bool cycleCapable = false;          // size > 1, a self edge, or opaque
    bool hasOpaque = false;             // the component reaches the sink
    std::vector<std::string> paths;     // the concrete field paths of the cycle
};

// The result of the analysis.
struct CycleAnalysis {
    std::vector<CycleEdge> edges;
    std::vector<CycleComponent> components;
    // The cycle-capable verdict per class, keyed by the class declaration's
    // node id (the same id the symbol table uses).
    std::unordered_map<std::uint32_t, bool> cycleCapable;
    // The collector-eligible verdict per class: a cycle-capable class whose
    // every strong edge is a walkable handle to another eligible class. The
    // collector can only reclaim such a class; the others keep the manual
    // `weak` requirement.
    std::unordered_map<std::uint32_t, bool> cycleEligible;
    // The class names that are acyclic (zero collector cost), sorted.
    std::vector<std::string> acyclicClasses;
    // The class names that are cycle-capable (the collector handles them),
    // sorted.
    std::vector<std::string> cycleCapableClasses;
    // The class names the collector can reclaim, sorted.
    std::vector<std::string> collectorEligibleClasses;
};

// The input the analysis needs from the analyzer.
struct CycleAnalysisInput {
    // The class name -> declaration node id map.
    std::unordered_map<std::string, std::uint32_t> classIds;
    // The class name -> base class name map. An inherited field is a strong
    // edge of the derived class too.
    std::unordered_map<std::string, std::string> baseOf;
    // The interface name -> implementing class names map (the closed world:
    // an interface-typed field may hold any implementer).
    std::unordered_map<std::string, std::vector<std::string>> implementersOf;
    // The shape label -> interface names map: an interface is a structural
    // type, so the field type identifies it by its label.
    std::unordered_map<std::string, std::vector<std::string>> interfaceByLabel;
    // One resolved field edge.
    struct FieldEdge {
        std::string owner;
        std::string field;
        Type type;
        bool weak = false;
        bool isStatic = false;
    };
    std::vector<FieldEdge> fields;
};

// True when the collector can walk a field of this type: a direct class
// handle or an optional of one. An array, a container, a record, a closure or
// a union of two classes holds its references behind a container the visitor
// cannot enumerate, so such a field keeps the manual `weak` requirement.
bool isWalkableHandle(const Type& t);

// Builds the reference graph, computes the strongly connected components and
// the cycle-capable verdict. Deterministic: the same input always yields the
// same graph, the same component order and the same verdicts.
CycleAnalysis analyzeCycles(const CycleAnalysisInput& in);

}  // namespace cp3

#endif  // CP3_SEMA_DETAIL_CYCLE_ANALYSIS_H
