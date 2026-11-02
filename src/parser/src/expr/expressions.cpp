// =============================================================================
// src/parser/src/expressions.cpp - operator-precedence expression chain.
//
// Implements the expression grammar as a descending chain of functions, one
// per precedence level (assignment > conditional > nullish > || > && >
// | > ^ > & > equality > relational > range > shift > additive >
// multiplicative > power > unary > postfix). Primary/postfix forms live in
// primary.cpp.
// =============================================================================
#include "detail/tokens.h"

#include <memory>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

ExprPtr ParserImpl::parseExpression() { return parseAssignment(); }

ExprPtr ParserImpl::parseAssignment() {
    const Token start = peek();
    ExprPtr left = parseConditional();
    if (!left) return nullptr;

    AssignOp op = AssignOp::Assign;
    bool hasOp = false;
    if (assignOpKind(peek().kind, op)) {
        hasOp = true;
        (void)advance();
    } else if (check(TokenKind::UnsignedShiftRight) &&
               peek(1).kind == TokenKind::Assign &&
               peek().span.end.offset == peek(1).span.begin.offset) {
        // `>>>=` split by a lexer that emits `>>>` + `=`.
        op = AssignOp::UnsignedShiftRight;
        hasOp = true;
        (void)advance();
        (void)advance();
    }
    if (!hasOp) return left;

    if (!isAssignableTarget(*left)) {
        report(diagcode::kInvalidAssignmentTarget, "invalid assignment target");
    }
    ExprPtr right = parseAssignment();
    auto assign = std::make_unique<AssignmentExpr>(start.span);
    assign->op = op;
    assign->target = std::move(left);
    assign->value = std::move(right);
    assign->span = spanFrom(start);
    return assign;
}

ExprPtr ParserImpl::parseConditional() {
    const Token start = peek();
    ExprPtr cond = parseNullish();
    if (!cond) return nullptr;
    if (match(TokenKind::Question)) {
        ExprPtr whenTrue = parseAssignment();
        (void)expect(TokenKind::Colon, "expected ':' in conditional expression");
        ExprPtr whenFalse = parseAssignment();
        auto expr = std::make_unique<ConditionalExpr>(start.span);
        expr->condition = std::move(cond);
        expr->whenTrue = std::move(whenTrue);
        expr->whenFalse = std::move(whenFalse);
        expr->span = spanFrom(start);
        return expr;
    }
    return cond;
}

ExprPtr ParserImpl::parseNullish() {
    const Token start = peek();
    ExprPtr left = parseLogicalOr();
    if (!left) return nullptr;
    while (check(TokenKind::QuestionQuestion)) {
        (void)advance();
        ExprPtr right = parseLogicalOr();
        auto expr = std::make_unique<LogicalExpr>(start.span);
        expr->op = LogicalOp::Nullish;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
    return left;
}

ExprPtr ParserImpl::parseLogicalOr() {
    const Token start = peek();
    ExprPtr left = parseLogicalAnd();
    if (!left) return nullptr;
    while (check(TokenKind::OrOr)) {
        (void)advance();
        ExprPtr right = parseLogicalAnd();
        auto expr = std::make_unique<LogicalExpr>(start.span);
        expr->op = LogicalOp::Or;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
    return left;
}

ExprPtr ParserImpl::parseLogicalAnd() {
    const Token start = peek();
    ExprPtr left = parseBitOr();
    if (!left) return nullptr;
    while (check(TokenKind::AndAnd)) {
        (void)advance();
        ExprPtr right = parseBitOr();
        auto expr = std::make_unique<LogicalExpr>(start.span);
        expr->op = LogicalOp::And;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
    return left;
}

ExprPtr ParserImpl::parseBitOr() {
    const Token start = peek();
    ExprPtr left = parseBitXor();
    if (!left) return nullptr;
    while (check(TokenKind::Pipe)) {
        (void)advance();
        ExprPtr right = parseBitXor();
        auto expr = std::make_unique<BinaryExpr>(start.span);
        expr->op = BinaryOp::BitOr;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
    return left;
}

ExprPtr ParserImpl::parseBitXor() {
    const Token start = peek();
    ExprPtr left = parseBitAnd();
    if (!left) return nullptr;
    while (check(TokenKind::Caret)) {
        (void)advance();
        ExprPtr right = parseBitAnd();
        auto expr = std::make_unique<BinaryExpr>(start.span);
        expr->op = BinaryOp::BitXor;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
    return left;
}

ExprPtr ParserImpl::parseBitAnd() {
    const Token start = peek();
    ExprPtr left = parseEquality();
    if (!left) return nullptr;
    while (check(TokenKind::Ampersand)) {
        (void)advance();
        ExprPtr right = parseEquality();
        auto expr = std::make_unique<BinaryExpr>(start.span);
        expr->op = BinaryOp::BitAnd;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
    return left;
}

ExprPtr ParserImpl::parseEquality() {
    const Token start = peek();
    ExprPtr left = parseRelational();
    if (!left) return nullptr;
    for (;;) {
        BinaryOp op;
        switch (peek().kind) {
            case TokenKind::Equal: op = BinaryOp::Equal; break;
            case TokenKind::NotEqual: op = BinaryOp::NotEqual; break;
            case TokenKind::StrictEqual: op = BinaryOp::StrictEqual; break;
            case TokenKind::StrictNotEqual: op = BinaryOp::StrictNotEqual; break;
            default: return left;
        }
        (void)advance();
        ExprPtr right = parseRelational();
        auto expr = std::make_unique<BinaryExpr>(start.span);
        expr->op = op;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
}

ExprPtr ParserImpl::parseRelational() {
    const Token start = peek();
    ExprPtr left = parseRange();
    if (!left) return nullptr;
    for (;;) {
        BinaryOp op;
        bool isIn = false;
        switch (peek().kind) {
            case TokenKind::Less: op = BinaryOp::LessThan; break;
            case TokenKind::Greater: op = BinaryOp::GreaterThan; break;
            case TokenKind::LessEqual: op = BinaryOp::LessThanEqual; break;
            case TokenKind::GreaterEqual: op = BinaryOp::GreaterThanEqual; break;
            case TokenKind::KwIn:
                if (noIn_) return left;
                op = BinaryOp::In;
                isIn = true;
                break;
            case TokenKind::KwInstanceof:
                op = BinaryOp::Instanceof;
                break;
            default: return left;
        }
        (void)isIn;
        (void)advance();
        ExprPtr right = parseRange();
        auto expr = std::make_unique<BinaryExpr>(start.span);
        expr->op = op;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
}

ExprPtr ParserImpl::parseRange() {
    const Token start = peek();
    ExprPtr left = parseShift();
    if (!left) return nullptr;
    if (check(TokenKind::DotDot)) {
        (void)advance();
        ExprPtr right = parseShift();
        auto expr = std::make_unique<RangeExpr>(start.span);
        expr->start = std::move(left);
        expr->end = std::move(right);
        expr->span = spanFrom(start);
        return expr;
    }
    return left;
}

ExprPtr ParserImpl::parseShift() {
    const Token start = peek();
    ExprPtr left = parseAdditive();
    if (!left) return nullptr;
    for (;;) {
        // `>>>=` split as `>>>` + `=`: yield to parseAssignment rather than
        // consuming `>>>` as a shift.
        if (check(TokenKind::UnsignedShiftRight) &&
            peek(1).kind == TokenKind::Assign &&
            peek().span.end.offset == peek(1).span.begin.offset) {
            return left;
        }
        BinaryOp op;
        switch (peek().kind) {
            case TokenKind::ShiftLeft: op = BinaryOp::ShiftLeft; break;
            case TokenKind::ShiftRight: op = BinaryOp::ShiftRight; break;
            case TokenKind::UnsignedShiftRight: op = BinaryOp::UnsignedShiftRight; break;
            default: return left;
        }
        (void)advance();
        ExprPtr right = parseAdditive();
        auto expr = std::make_unique<BinaryExpr>(start.span);
        expr->op = op;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
}

ExprPtr ParserImpl::parseAdditive() {
    const Token start = peek();
    ExprPtr left = parseMultiplicative();
    if (!left) return nullptr;
    for (;;) {
        BinaryOp op;
        switch (peek().kind) {
            case TokenKind::Plus: op = BinaryOp::Add; break;
            case TokenKind::Minus: op = BinaryOp::Subtract; break;
            default: return left;
        }
        (void)advance();
        ExprPtr right = parseMultiplicative();
        auto expr = std::make_unique<BinaryExpr>(start.span);
        expr->op = op;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
}

ExprPtr ParserImpl::parseMultiplicative() {
    const Token start = peek();
    ExprPtr left = parsePower();
    if (!left) return nullptr;
    for (;;) {
        BinaryOp op;
        switch (peek().kind) {
            case TokenKind::Star: op = BinaryOp::Multiply; break;
            case TokenKind::Slash: op = BinaryOp::Divide; break;
            case TokenKind::Percent: op = BinaryOp::Modulo; break;
            default: return left;
        }
        (void)advance();
        ExprPtr right = parsePower();
        auto expr = std::make_unique<BinaryExpr>(start.span);
        expr->op = op;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        left = std::move(expr);
    }
}

ExprPtr ParserImpl::parsePower() {
    const Token start = peek();
    ExprPtr left = parseUnary();
    if (!left) return nullptr;
    if (check(TokenKind::StarStar)) {
        (void)advance();
        ExprPtr right = parsePower();  // right-associative
        auto expr = std::make_unique<BinaryExpr>(start.span);
        expr->op = BinaryOp::Power;
        expr->left = std::move(left);
        expr->right = std::move(right);
        expr->span = spanFrom(start);
        return expr;
    }
    return left;
}

ExprPtr ParserImpl::parseUnary() {
    const Token start = peek();
    UnaryOp unary;
    bool isUnary = true;
    switch (start.kind) {
        case TokenKind::Bang: unary = UnaryOp::Not; break;
        case TokenKind::Minus: unary = UnaryOp::Negate; break;
        case TokenKind::Plus: unary = UnaryOp::Plus; break;
        case TokenKind::Tilde: unary = UnaryOp::BitNot; break;
        case TokenKind::KwTypeof: unary = UnaryOp::Typeof; break;
        case TokenKind::KwDelete: unary = UnaryOp::Delete; break;
        case TokenKind::KwVoid: unary = UnaryOp::Void; break;
        default: isUnary = false; break;
    }
    if (isUnary) {
        (void)advance();
        ExprPtr operand = parseUnary();
        auto expr = std::make_unique<UnaryExpr>(start.span);
        expr->op = unary;
        expr->operand = std::move(operand);
        expr->prefix = true;
        expr->span = spanFrom(start);
        return expr;
    }

    if (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus)) {
        Token op = advance();
        UpdateOp update = (op.kind == TokenKind::PlusPlus) ? UpdateOp::Increment
                                                            : UpdateOp::Decrement;
        ExprPtr operand = parseUnary();
        auto expr = std::make_unique<UpdateExpr>(start.span);
        expr->op = update;
        expr->operand = std::move(operand);
        expr->prefix = true;
        expr->span = spanFrom(start);
        return expr;
    }

    if (check(TokenKind::KwAwait)) {
        (void)advance();
        ExprPtr arg = parseUnary();
        auto expr = std::make_unique<AwaitExpr>(start.span);
        expr->argument = std::move(arg);
        expr->span = spanFrom(start);
        return expr;
    }

    return parsePostfix();
}

ExprPtr ParserImpl::parsePostfix() {
    const Token start = peek();
    ExprPtr expr = parseCallMember();
    if (!expr) return nullptr;
    if ((check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus)) &&
        !peek().precededByNewline) {
        Token op = advance();
        auto update = std::make_unique<UpdateExpr>(start.span);
        update->op = (op.kind == TokenKind::PlusPlus) ? UpdateOp::Increment
                                                       : UpdateOp::Decrement;
        update->operand = std::move(expr);
        update->prefix = false;
        update->span = spanFrom(start);
        return update;
    }
    return expr;
}

}  // namespace cp3
