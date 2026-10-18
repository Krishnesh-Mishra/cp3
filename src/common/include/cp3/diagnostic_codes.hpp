// =============================================================================
// cp3/diagnostic_codes.hpp - the canonical diagnostic code table.
//
// FROZEN INTERFACE (Beta One). See ARCHITECTURE.md.
//
// Every diagnostic carries a stable "CP3xxx" code so tests can assert on it
// (see tests/cases/README.md). Codes are grouped by owning stage; new codes
// are appended inside their existing range and never renumbered.
//
//   3100-3199  lexical        (lexer)
//   3200-3299  syntax         (parser)
//   3300-3399  name/resolution(sema)
//   3400-3499  types          (sema)
//   3500-3599  statements/control flow (sema)
//   3600-3699  modules/imports(pipeline / sema)
//   3700-3799  classes/interfaces (sema)
//   3800-3899  banned JS patterns (sema)   -- see language-spec 08 / 23
//   3900-3999  concurrency    (sema)
//   3001-3009  internal / not-yet-implemented
// =============================================================================
#pragma once

namespace cp3::diagcode {

// --- internal -----------------------------------------------------------------
inline constexpr const char* kInternalError     = "CP3001";
inline constexpr const char* kNotImplemented    = "CP3002";

// --- lexical (lexer) ----------------------------------------------------------
inline constexpr const char* kInvalidCharacter  = "CP3101";
inline constexpr const char* kUnterminatedString = "CP3102";
inline constexpr const char* kUnterminatedTemplate = "CP3103";
inline constexpr const char* kUnterminatedComment = "CP3104";
inline constexpr const char* kInvalidNumericLiteral = "CP3105";
inline constexpr const char* kInvalidEscape     = "CP3106";
inline constexpr const char* kInvalidRegExp     = "CP3107";

// --- syntax (parser) ----------------------------------------------------------
inline constexpr const char* kUnexpectedToken   = "CP3201";
inline constexpr const char* kExpectedToken     = "CP3202";
inline constexpr const char* kUnexpectedEof     = "CP3203";
inline constexpr const char* kInvalidAssignmentTarget = "CP3204";
inline constexpr const char* kInvalidStatement  = "CP3205";
inline constexpr const char* kExpectedDeclaration = "CP3206";
inline constexpr const char* kMissingSemicolon  = "CP3207";

// --- name resolution (sema) ---------------------------------------------------
inline constexpr const char* kUndefinedIdentifier = "CP3301";
inline constexpr const char* kUndefinedType     = "CP3302";
inline constexpr const char* kDuplicateDeclaration = "CP3303";
inline constexpr const char* kUnresolvedImport  = "CP3304";
inline constexpr const char* kMemberNotFound    = "CP3305";
inline constexpr const char* kUndefinedLabel    = "CP3306";

// --- types (sema) -------------------------------------------------------------
inline constexpr const char* kTypeMismatch      = "CP3401";
inline constexpr const char* kNotAssignable     = "CP3402";
inline constexpr const char* kArityMismatch     = "CP3403";
inline constexpr const char* kNotCallable       = "CP3404";
inline constexpr const char* kNotIndexable      = "CP3405";
inline constexpr const char* kInvalidOperandTypes = "CP3406";
inline constexpr const char* kUnknownTypeArgument = "CP3407";
inline constexpr const char* kConstReassignment = "CP3408";
inline constexpr const char* kExcessProperty    = "CP3409";
inline constexpr const char* kMissingProperty   = "CP3410";
inline constexpr const char* kMixedTypeArithmetic = "CP3411";
inline constexpr const char* kConstraintNotSatisfied = "CP3412";
inline constexpr const char* kCannotInstantiateAbstract = "CP3413";
inline constexpr const char* kUnknownType       = "CP3414";
inline constexpr const char* kInvalidCondition  = "CP3415";
inline constexpr const char* kNoMatchingOverload = "CP3416";

// --- statements / control flow (sema) ----------------------------------------
inline constexpr const char* kReturnOutsideFunction = "CP3501";
inline constexpr const char* kBreakOutsideLoop  = "CP3502";
inline constexpr const char* kContinueOutsideLoop = "CP3503";
inline constexpr const char* kMissingReturn     = "CP3504";
inline constexpr const char* kUnreachableCode   = "CP3505";

// --- modules / imports --------------------------------------------------------
inline constexpr const char* kModuleNotFound    = "CP3601";
inline constexpr const char* kPackageNotFound   = "CP3602";
inline constexpr const char* kNativeImportUnsupported = "CP3603";
inline constexpr const char* kMissingDefaultExport = "CP3604";
inline constexpr const char* kPreludeError      = "CP3605";

// --- classes / interfaces (sema) ---------------------------------------------
inline constexpr const char* kMissingInterfaceMember = "CP3701";
inline constexpr const char* kPrivateFieldInLiteral = "CP3702";
inline constexpr const char* kMultipleInheritance = "CP3703";
inline constexpr const char* kInvalidOverride   = "CP3704";
inline constexpr const char* kDuplicateMember   = "CP3705";
inline constexpr const char* kMixedEnumBacking  = "CP3706";

// --- banned JS patterns (sema) - language-spec 08 / 23 -----------------------
inline constexpr const char* kDeleteBanned      = "CP3801";
inline constexpr const char* kUndeclaredProperty = "CP3802";
inline constexpr const char* kIndexSignatureBanned = "CP3803";
inline constexpr const char* kArgumentsBanned   = "CP3804";
inline constexpr const char* kForInPlainObjectBanned = "CP3805";
inline constexpr const char* kAnyTypeBanned     = "CP3806";
inline constexpr const char* kGeneratorsUnsupported = "CP3807";
inline constexpr const char* kDecoratorsUnsupported = "CP3808";

// --- concurrency (sema) - language-spec 13 -----------------------------------
inline constexpr const char* kParallelSharedMutation = "CP3901";
inline constexpr const char* kParallelCaptureReassign = "CP3902";
inline constexpr const char* kUnknownWorkerFile = "CP3903";

}  // namespace cp3::diagcode
