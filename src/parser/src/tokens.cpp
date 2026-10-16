// =============================================================================
// src/parser/src/tokens.cpp — token cursor helpers + syntax-error recovery.
//
// Implements the small set of shared helpers declared in tokens.h:
//   * free predicates used across parser units (statement starts, assignable
//     targets, type-name keywords, assignment-token classification);
//   * ParserImpl diagnostic/recovery primitives (`report`, `expect`,
//     `expectName`, `expectGreater`, `synchronize`, `consumeSemicolon`).
//
// Diagnostics use codes CP3201–CP3207 (cp3/diagnostic_codes.hpp) and are
// emitted through the DiagnosticSink; the parser never throws (STYLE.md §1/§6).
// =============================================================================
#include "tokens.h"

#include <string>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// True when `k` can begin a statement (panic-mode synchronisation target).
bool isStatementStart(TokenKind k) {
    switch (k) {
        case TokenKind::KwLet:
        case TokenKind::KwVar:
        case TokenKind::KwConst:
        case TokenKind::KwFunction:
        case TokenKind::KwClass:
        case TokenKind::KwInterface:
        case TokenKind::KwEnum:
        case TokenKind::KwType:
        case TokenKind::KwIf:
        case TokenKind::KwWhile:
        case TokenKind::KwDo:
        case TokenKind::KwFor:
        case TokenKind::KwSwitch:
        case TokenKind::KwBreak:
        case TokenKind::KwContinue:
        case TokenKind::KwReturn:
        case TokenKind::KwThrow:
        case TokenKind::KwTry:
        case TokenKind::KwImport:
        case TokenKind::KwExport:
        case TokenKind::KwAbstract:
        case TokenKind::KwAsync:
        case TokenKind::KwThis:
        case TokenKind::At:
        case TokenKind::LBrace:
            return true;
        default:
            return false;
    }
}

bool assignOpKind(TokenKind k, AssignOp& out) {
    switch (k) {
        case TokenKind::Assign: out = AssignOp::Assign; return true;
        case TokenKind::PlusAssign: out = AssignOp::Add; return true;
        case TokenKind::MinusAssign: out = AssignOp::Subtract; return true;
        case TokenKind::StarAssign: out = AssignOp::Multiply; return true;
        case TokenKind::SlashAssign: out = AssignOp::Divide; return true;
        case TokenKind::PercentAssign: out = AssignOp::Modulo; return true;
        case TokenKind::StarStarAssign: out = AssignOp::Power; return true;
        case TokenKind::AmpersandAssign: out = AssignOp::BitAnd; return true;
        case TokenKind::PipeAssign: out = AssignOp::BitOr; return true;
        case TokenKind::CaretAssign: out = AssignOp::BitXor; return true;
        case TokenKind::ShiftLeftAssign: out = AssignOp::ShiftLeft; return true;
        case TokenKind::ShiftRightAssign: out = AssignOp::ShiftRight; return true;
        case TokenKind::UnsignedShiftRightAssign: out = AssignOp::UnsignedShiftRight; return true;
        case TokenKind::AndAndAssign: out = AssignOp::LogicalAnd; return true;
        case TokenKind::OrOrAssign: out = AssignOp::LogicalOr; return true;
        case TokenKind::QuestionQuestionAssign: out = AssignOp::Nullish; return true;
        default: return false;
    }
}

bool isAssignableTarget(const Expr& e) {
    switch (e.kind) {
        case NodeKind::Identifier:
        case NodeKind::MemberExpr:
        case NodeKind::IndexExpr:
        case NodeKind::ArrayLiteral:
        case NodeKind::ObjectLiteral:
            return true;
        default:
            return false;
    }
}

bool isTypeNameKeyword(TokenKind k) {
    switch (k) {
        case TokenKind::KwVoid:
        case TokenKind::KwUnknown:
        case TokenKind::KwNull:
        case TokenKind::KwUndefined:
        case TokenKind::KwAny:
            return true;
        default:
            return false;
    }
}

// `anchor_` is the first token of the statement currently being parsed; a
// syntax problem is reported against it (statement-granular, matching the
// suite's diagnostics convention) and always carries the word "syntax".
void ParserImpl::report(const std::string& code, const std::string& message) {
    const SourceSpan& sp = anchor_ ? anchor_->span : peek().span;
    sink_->error(code, sp, "syntax error: " + message);
}

bool ParserImpl::expect(TokenKind k, std::string what) {
    if (check(k)) {
        (void)advance();
        return true;
    }
    report(diagcode::kExpectedToken, what);
    return false;
}

std::string ParserImpl::expectName(std::string what) {
    if (isIdentLike(peek())) return advanceText();
    report(diagcode::kUnexpectedToken, "expected " + what);
    return std::string();
}

// `>` handling for nested generics: `Box<Box<number>>` lexes the closing `>>`
// as a single ShiftRight token, so a logically pending `>` is tracked and
// consumed before falling back to the stream.
bool ParserImpl::expectGreater() {
    if (pendingGT_ > 0) {
        --pendingGT_;
        return true;
    }
    if (match(Greater)) return true;
    if (check(ShiftRight)) {
        (void)advance();
        pendingGT_ = 1;
        return true;
    }
    if (check(UnsignedShiftRight)) {
        (void)advance();
        pendingGT_ = 2;
        return true;
    }
    report(diagcode::kExpectedToken, "expected '>'");
    return false;
}

void ParserImpl::synchronize() {
    pendingGT_ = 0;
    while (!atEnd()) {
        if (check(Semicolon)) {
            (void)advance();
            return;
        }
        if (check(RBrace)) return;
        if (peek().precededByNewline && isStatementStart(peek().kind)) return;
        (void)advance();
    }
}

void ParserImpl::consumeSemicolon() {
    if (match(Semicolon)) return;
    if (check(RBrace) || atEnd()) return;  // ASI before a closing brace / EOF
    if (peek().precededByNewline) return;  // ASI across a newline
    report(diagcode::kMissingSemicolon, "expected ';'");
}

}  // namespace cp3
