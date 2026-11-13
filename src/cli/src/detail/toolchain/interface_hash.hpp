// =============================================================================
// src/cli/src/detail/toolchain/interface_hash.hpp - canonical interface hashing
// DECLARATIONS for one cp3 module's export surface.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/toolchain/runtime/interface_hash.cpp (STYLE.md §3/§8:
// headers declare, `.cpp` defines).
//
// The pipeline's export surface (ExportSurface in src/toolchain/runtime/
// pipeline.cpp) is a private struct: it binds each exported name to a
// cp3::ImportedBinding. This module flattens that surface into plain,
// self-delimiting strings (InterfaceExportEntry / InterfaceClassShape) so a
// stable hash can be taken over it without leaking pipeline.cpp's private
// types. The ExportCollector is NOT wired up yet; the orchestrator calls the
// shape/entry builders below from pipeline.cpp and feeds the results here.
//
// Canonical-signature format ("cp3-interface-signature/1"):
//   * UTF-8 bytes, line-based; every record line ends in '\n' (including the
//     last one). The first line is the literal header `cp3-interface-signature/1`.
//   * Every string payload is length-prefixed as `<byte-len>:<raw bytes>`; no
//     value is ever emitted bare except the fixed vocabularies (record tags,
//     `flags=<5 bits>`, bare 0/1 list elements, decimal counts).
//   * Entries are sorted by (name, kind, rendered line) and classes by
//     (label, members joined by '\n') before serialization, so callers do NOT
//     need to iterate in any particular order (map iteration order never leaks
//     into the output). memberText order WITHIN a class shape is significant
//     (declaration order) and is preserved. Union member order in type texts
//     is preserved as stored and never sorted.
//   * Identical input bytes hash to identical output on every platform.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/core/types.hpp"
#include "cp3/sema.hpp"

namespace cp3::cli {

// One exported binding flattened to plain strings. `paramOptional` holds
// "0"/"1" per parameter (parallel to paramTypes); `typeParams` holds the
// canonical type text of each declared type parameter.
struct InterfaceBindingField {
    std::string name;
    std::string typeText;
    bool isValue = true;
    bool isType = false;
    bool isFunction = false;
    bool isAsync = false;
    bool hasRest = false;
    std::vector<std::string> typeParams;
    std::vector<std::string> paramTypes;
    std::vector<std::string> paramOptional;
    std::string returnType;
};

// One exported binding as one signature record.
// `kind` is "value" | "type" | "default" (any other spelling is passed through
// verbatim; serialization stays deterministic either way).
struct InterfaceExportEntry {
    std::string name;
    std::string kind;
    InterfaceBindingField binding;
};

// The canonical shape of one exported class/interface/alias. `label` is the
// header line of the shape (e.g. "class Box<T>"); `memberText` holds one
// canonical one-line member signature per element, in declaration order.
struct InterfaceClassShape {
    std::string label;
    std::vector<std::string> memberText;
};

// Serializes entries and classes into the canonical signature text described
// above. Entries and classes are canonically re-sorted internally; input order
// is not significant.
std::string serializeInterfaceSignature(const std::vector<InterfaceExportEntry>& entries,
                                        const std::vector<InterfaceClassShape>& classes);

// fnv1a64 over serializeInterfaceSignature(...). Returns 0 on allocation
// failure (the function is noexcept; in-memory text building otherwise).
std::uint64_t interfaceHashOf(const std::vector<InterfaceExportEntry>& entries,
                              const std::vector<InterfaceClassShape>& classes) noexcept;

// Recursive canonical print of any Type. Every TypeKind has a rendering:
//   error, unknown, number, string, boolean, undefined, null, void,
//   (<element>)[]                    Array
//   (<p0>, <p1>) => <return>         Function (return type is last in parts)
//   <m0> | <m1> | ...                Union (stored order preserved; a Union,
//                                    Function or Nullable member is parenthesized)
//   (<inner>)?                       Nullable
//   object[:label]                   Object (label by name)
//   class:<name>                     Class
//   enum:<name>                      Enum
//   typeparam:<name>                 TypeParam
//   native:<id>                      Native (canonical id)
//   literal:number:<numberRaw | shortest round-trip spelling of numberValue>
//   literal:string:"<escaped value>"
//   literal:boolean:true|false
//   literal:<baseKindName>:unknown   Literal with an unknown base (total fallback)
// A kind outside the declared enum renders as unknown-kind:<typeKindName>.
// Payload strings escape '\\', control bytes and DEL; literal string values
// are quoted. The result is always a single line (no raw '\n' can occur).
std::string typeToCanonicalText(const Type& t);

// Flattens an ImportedBinding to a canonical entry record. `kind` is the
// export kind ("value" | "type" | "default"). The binding's `type` is
// printed via typeToCanonicalText; a default-constructed returnType (non-
// function binding) prints as `unknown`.
InterfaceExportEntry importedBindingToEntry(const std::string& name,
                                            const ImportedBinding& b,
                                            const std::string& kind);

// Flattens an ImportedBinding to one entry line: `entry kind=... name=...
// field=... type=... flags=abcde typeParams=n(...) paramTypes=n(...)
// paramOptional=n(...) returnType=...`. `kind` is the export kind
// ("value" | "type" | "default"). No trailing '\n'.
std::string bindingToInterfaceEntry(const std::string& name, const ImportedBinding& b,
                                    const std::string& kind);

// Renders a ClassDeclaration to the multi-line shape text:
//   line 0:  label            "class <name>[<T, ...>]" ("abstract class ..."
//                              when isAbstract)
//   line 1+: member lines     "extends <type>" / "implements <type>" heritage
//                              edges first (in implementsTypes order), then the
//                              members in source order:
//                              "field <name>[!|?]: <type> [modifiers]"
//                              "method <name>(<params>) -> <ret> [modifiers]"
//                              "accessor <name>(<params>) -> <ret> [access] [static] get|set"
//                              "constructor(<params>) [modifiers]"
//                              "static-block"
//                              "unknown <NodeKindName>"  (unknown member kinds)
//   Modifiers are suffixes in a fixed order:
//   public|private|protected, static, abstract, async, readonly, override,
//   weak (fields only), get|set (accessors only). Parameter text prints the
//   declared type (or `unknown`), with `...` prefix for rest parameters,
//   `ref ` prefix for ref parameters and `?` suffix for optional ones.
//   Missing type annotations print as `unknown`. Parameter names are not
//   printed and member generic parameter lists are omitted (the spec's
//   one-line member form carries no type parameters). Defensive fallbacks
//   (expected never to occur in a parsed program): a null member pointer
//   prints "unknown <null>", a missing key prints "<no-key>", an empty union
//   prints "union".
// Lines are joined by '\n' with no trailing newline.
std::string classDeclToInterfaceShape(const ClassDeclaration& decl);

// Renders an InterfaceDeclaration to the multi-line shape text:
//   line 0:  label            "interface <name>[<T, ...>]"
//   line 1+: "extends <type>" per extendsTypes entry (source order), then the
//            members in source order:
//            "prop <name>[?]: <type> [readonly]"
//            "method <name>[?](<params>) -> <ret> [function]"   (function
//             suffix only when hasFunctionKeyword)
//            "index (<keyType>) -> <valueType> [readonly]"
//            "unknown <NodeKindName>"  (unknown member kinds)
// Lines are joined by '\n' with no trailing newline.
std::string interfaceDeclToInterfaceShape(const InterfaceDeclaration& decl);

// Renders a TypeAliasDeclaration to the multi-line shape text:
//   line 0:  label            "type <name>[<T, ...>]"
//   line 1:  "alias <name> = <type>"   (<type> is the canonical print of the
//                                        underlying TypeNode; `unknown` when absent)
// Lines are joined by '\n' with no trailing newline.
std::string aliasDeclToInterfaceShape(const TypeAliasDeclaration& decl);

}  // namespace cp3::cli
