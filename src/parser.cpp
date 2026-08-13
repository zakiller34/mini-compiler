#include "parser.h"

#include <memory>
#include <string>
#include <vector>

namespace mc {

Parser::Parser(Lexer &lex, bool dyn)
    : lex_(lex), cur_(lex_.next()), dyn_(dyn) {}

/// @brief Consume current token, advance to next
/// @modifies cur_
void Parser::advance() { cur_ = lex_.next(); }

/// @brief Assert current token matches kind, then advance
/// @requires cur_.kind == kind (throws ParseError otherwise)
void Parser::expect(TokenKind kind, const std::string &msg) {
    if (cur_.kind != kind) {
        throw ParseError("expected " + msg + ", got '" + cur_.text + "'");
    }
    advance();
}

/// @brief Parse type annotation
/// @ensures result is a valid TypePtr
TypePtr Parser::parse_type() {
    if (cur_.kind == TokenKind::Int_kw) {
        advance();
        return int_type();
    }
    if (cur_.kind == TokenKind::Bool_kw) {
        advance();
        return bool_type();
    }
    if (cur_.kind == TokenKind::Void) {
        advance();
        return void_type();
    }
    if (cur_.kind == TokenKind::Any_kw) {
        advance();
        return any_type();
    }
    if (cur_.kind == TokenKind::LParen) {
        advance();
        return parse_fun_type();
    }
    throw ParseError("expected type, got '" + cur_.text + "'");
}

/// @brief Parse `T1, ... ')' '->' R` (the leading '(' is already consumed)
TypePtr Parser::parse_fun_type() {
    std::vector<TypePtr> param_types;
    if (cur_.kind != TokenKind::RParen) {
        param_types.push_back(parse_type());
        // invariant: param_types has parsed types so far
        // decreases: remaining tokens until ')'
        while (cur_.kind == TokenKind::Comma) {
            advance();
            param_types.push_back(parse_type());
        }
    }
    expect(TokenKind::RParen, "')'");
    expect(TokenKind::Arrow, "'->'");
    auto ret = parse_type();
    return fun_type(std::move(param_types), std::move(ret));
}

/// @brief Parse a `':' type` annotation, or yield `Any` in --dyn mode
/// @ensures dyn mode consumes nothing and rejects a stray ':'
TypePtr Parser::parse_annotation() {
    if (!dyn_) {
        expect(TokenKind::Colon, "':'");
        return parse_type();
    }
    if (cur_.kind == TokenKind::Colon) {
        throw ParseError("type annotations are not allowed in --dyn mode");
    }
    return any_type();
}

/// @brief Parse comma-separated `IDENT ':' type` list (parens consumed by caller)
/// @ensures result is params in source order
std::vector<std::pair<std::string, TypePtr>> Parser::parse_params() {
    std::vector<std::pair<std::string, TypePtr>> params;
    if (cur_.kind == TokenKind::RParen) {
        return params;
    }
    // invariant: params has parsed params so far
    // decreases: remaining tokens until ')'
    while (true) {
        if (cur_.kind != TokenKind::Ident) {
            throw ParseError("expected parameter name");
        }
        std::string pname = cur_.text;
        advance();
        auto ptype = parse_annotation();
        params.emplace_back(std::move(pname), std::move(ptype));
        if (cur_.kind != TokenKind::Comma) {
            break;
        }
        advance();
    }
    return params;
}

/// @brief Parse function definition: fn name(params) : ret_type { body }
DefNode Parser::parse_def() {
    expect(TokenKind::Fn, "'fn'");
    if (cur_.kind != TokenKind::Ident) {
        throw ParseError("expected function name after fn");
    }
    std::string name = cur_.text;
    advance();
    expect(TokenKind::LParen, "'('");
    auto params = parse_params();
    expect(TokenKind::RParen, "')'");
    auto ret_type = parse_annotation();
    expect(TokenKind::LBrace, "'{'");
    auto body = parse_expr();
    expect(TokenKind::RBrace, "'}'");
    return DefNode{std::move(name), std::move(params),
                   std::move(ret_type), std::move(body)};
}

/// @brief Parse full program: def* expr EOF
/// @ensures result->body is the parsed AST, result->defs has fn defs
std::unique_ptr<Program> Parser::parse_program() {
    std::vector<DefNode> defs;
    // invariant: defs has all parsed fn defs so far
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::Fn) {
        defs.push_back(parse_def());
    }
    auto body = parse_expr();
    if (cur_.kind != TokenKind::Eof) {
        throw ParseError("expected EOF, got '" + cur_.text + "'");
    }
    return std::make_unique<Program>(std::move(defs), std::move(body));
}

/// @brief Parse expr: let binding or or_expr
std::unique_ptr<Expr> Parser::parse_expr() {
    if (cur_.kind == TokenKind::Let) {
        advance();
        if (cur_.kind != TokenKind::Ident) {
            throw ParseError("expected identifier after let");
        }
        std::string var = cur_.text;
        advance();
        expect(TokenKind::Equals, "'='");
        auto init = parse_or_expr();
        expect(TokenKind::Semicolon, "';'");
        auto body = parse_expr();
        return std::make_unique<LetExpr>(
            std::move(var), std::move(init), std::move(body));
    }
    return parse_or_expr();
}

/// @brief Parse or_expr: and_expr (OR and_expr)*
std::unique_ptr<Expr> Parser::parse_or_expr() {
    auto lhs = parse_and_expr();
    // invariant: lhs accumulates left-assoc or chain
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::Or) {
        advance();
        auto rhs = parse_and_expr();
        lhs = std::make_unique<BinaryExpr>(
            BinaryOp::Or, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

/// @brief Parse and_expr: cmp_expr (AND cmp_expr)*
std::unique_ptr<Expr> Parser::parse_and_expr() {
    auto lhs = parse_cmp_expr();
    // invariant: lhs accumulates left-assoc and chain
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::And) {
        advance();
        auto rhs = parse_cmp_expr();
        lhs = std::make_unique<BinaryExpr>(
            BinaryOp::And, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

/// @brief Parse cmp_expr: additive ((EqEq|Lt|Le|Gt|Ge) additive)?
std::unique_ptr<Expr> Parser::parse_cmp_expr() {
    auto lhs = parse_additive();
    BinaryOp op{};
    bool has_cmp = true;
    switch (cur_.kind) {
    case TokenKind::EqEq: op = BinaryOp::Eq; break;
    case TokenKind::Lt: op = BinaryOp::Lt; break;
    case TokenKind::Le: op = BinaryOp::Le; break;
    case TokenKind::Gt: op = BinaryOp::Gt; break;
    case TokenKind::Ge: op = BinaryOp::Ge; break;
    default: has_cmp = false; break;
    }
    if (has_cmp) {
        advance();
        auto rhs = parse_additive();
        return std::make_unique<BinaryExpr>(
            op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

/// @brief Parse additive: left-assoc + and - over unary
std::unique_ptr<Expr> Parser::parse_additive() {
    auto lhs = parse_unary();
    // invariant: lhs accumulates left-assoc +/- chain
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::Plus || cur_.kind == TokenKind::Minus) {
        BinaryOp op = (cur_.kind == TokenKind::Plus)
                          ? BinaryOp::Add : BinaryOp::Sub;
        advance();
        auto rhs = parse_unary();
        lhs = std::make_unique<BinaryExpr>(
            op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

/// @brief Parse unary: prefix -/not chain (iterative)
std::unique_ptr<Expr> Parser::parse_unary() {
    std::vector<UnaryOp> ops;
    // invariant: ops has all prefix ops consumed
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::Minus || cur_.kind == TokenKind::Not) {
        ops.push_back(cur_.kind == TokenKind::Minus
                          ? UnaryOp::Neg : UnaryOp::Not);
        advance();
    }
    auto expr = parse_postfix();
    // invariant: expr wrapped with ops[i+1..] applied
    // decreases: i
    for (int i = static_cast<int>(ops.size()) - 1; i >= 0; --i) {
        expr = std::make_unique<UnaryExpr>(ops[i], std::move(expr));
    }
    return expr;
}

/// @brief Parse postfix: primary ('[' INT ']' ('=' expr)? | '(' args ')')*
std::unique_ptr<Expr> Parser::parse_postfix() {
    auto expr = parse_primary();
    // invariant: expr accumulates postfix subscript/set/application ops
    // decreases: remaining tokens
    while (cur_.kind == TokenKind::LBracket ||
           cur_.kind == TokenKind::LParen) {
        if (cur_.kind == TokenKind::LParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            if (cur_.kind != TokenKind::RParen) {
                args.push_back(parse_expr());
                // invariant: args has parsed args so far
                // decreases: remaining tokens until ')'
                while (cur_.kind == TokenKind::Comma) {
                    advance();
                    args.push_back(parse_expr());
                }
            }
            expect(TokenKind::RParen, "')'");
            expr = std::make_unique<ApplyExpr>(
                std::move(expr), std::move(args));
            continue;
        }
        advance();
        expr = parse_subscript(std::move(expr));
    }
    return expr;
}

/// @brief Parse the rest of `vec[...]` (the '[' is already consumed)
/// @requires vec != nullptr
/// @ensures static mode yields VectorRef/VectorSet with a literal index;
///          dyn mode yields AnyVectorRef/AnyVectorSet with an expression index
std::unique_ptr<Expr> Parser::parse_subscript(std::unique_ptr<Expr> vec) {
    if (dyn_) {
        auto idx = parse_expr();
        expect(TokenKind::RBracket, "']'");
        if (cur_.kind != TokenKind::Equals) {
            return std::make_unique<AnyVectorRefExpr>(
                std::move(vec), std::move(idx));
        }
        advance();
        auto val = parse_expr();
        return std::make_unique<AnyVectorSetExpr>(
            std::move(vec), std::move(idx), std::move(val));
    }
    if (cur_.kind != TokenKind::IntLit) {
        throw ParseError("expected integer index in []");
    }
    int64_t idx = cur_.int_val;
    advance();
    expect(TokenKind::RBracket, "']'");
    if (cur_.kind != TokenKind::Equals) {
        return std::make_unique<VectorRefExpr>(std::move(vec), idx);
    }
    advance();
    auto val = parse_expr();
    return std::make_unique<VectorSetExpr>(
        std::move(vec), idx, std::move(val));
}

/// @brief Parse `inject(e, T)` / `project(e, T)` (static mode only)
/// @requires cur_ is Inject or Project
/// @ensures result is InjectExpr or ProjectExpr with a flat ftype
std::unique_ptr<Expr> Parser::parse_cast() {
    bool is_inject = cur_.kind == TokenKind::Inject;
    advance();
    expect(TokenKind::LParen, "'('");
    auto inner = parse_expr();
    expect(TokenKind::Comma, "','");
    auto ftype = parse_type();
    expect(TokenKind::RParen, "')'");
    if (!is_flat_type(ftype)) {
        throw ParseError("inject/project require a flat type, got " +
                         ftype->dump());
    }
    if (is_inject) {
        return std::make_unique<InjectExpr>(std::move(inner),
                                            std::move(ftype));
    }
    return std::make_unique<ProjectExpr>(std::move(inner), std::move(ftype));
}

/// @brief Parse `pred?(e)` for a runtime type predicate
/// @requires cur_ is the predicate token
std::unique_ptr<Expr> Parser::parse_type_pred(TypePred pred) {
    advance();
    expect(TokenKind::LParen, "'('");
    auto inner = parse_expr();
    expect(TokenKind::RParen, "')'");
    return std::make_unique<TypePredExpr>(pred, std::move(inner));
}

/// @brief Parse primary: int, bool, ident, read(), if, parens
// Token dispatch FSM: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
std::unique_ptr<Expr> Parser::parse_primary() {
    switch (cur_.kind) {
    case TokenKind::IntLit: {
        int64_t val = cur_.int_val;
        advance();
        return std::make_unique<IntExpr>(val);
    }
    case TokenKind::True:
        advance();
        return std::make_unique<BoolExpr>(true);
    case TokenKind::False:
        advance();
        return std::make_unique<BoolExpr>(false);
    case TokenKind::Ident: {
        std::string name = cur_.text;
        advance();
        return std::make_unique<VarExpr>(std::move(name));
    }
    case TokenKind::Read: {
        advance();
        expect(TokenKind::LParen, "'('");
        expect(TokenKind::RParen, "')'");
        return std::make_unique<ReadExpr>();
    }
    case TokenKind::If: {
        advance();
        expect(TokenKind::LParen, "'('");
        auto cond = parse_expr();
        expect(TokenKind::RParen, "')'");
        expect(TokenKind::LBrace, "'{'");
        auto then_br = parse_expr();
        expect(TokenKind::RBrace, "'}'");
        expect(TokenKind::Else, "'else'");
        expect(TokenKind::LBrace, "'{'");
        auto else_br = parse_expr();
        expect(TokenKind::RBrace, "'}'");
        return std::make_unique<IfExpr>(
            std::move(cond), std::move(then_br), std::move(else_br));
    }
    case TokenKind::While: {
        advance();
        expect(TokenKind::LParen, "'('");
        auto cond = parse_expr();
        expect(TokenKind::RParen, "')'");
        expect(TokenKind::LBrace, "'{'");
        auto body = parse_expr();
        expect(TokenKind::RBrace, "'}'");
        return std::make_unique<WhileExpr>(
            std::move(cond), std::move(body));
    }
    case TokenKind::Begin: {
        advance();
        expect(TokenKind::LBrace, "'{'");
        std::vector<std::unique_ptr<Expr>> exprs;
        exprs.push_back(parse_expr());
        // invariant: exprs has all parsed sub-expressions so far
        // decreases: remaining tokens until '}'
        while (cur_.kind == TokenKind::Semicolon) {
            advance();
            exprs.push_back(parse_expr());
        }
        expect(TokenKind::RBrace, "'}'");
        return std::make_unique<BeginExpr>(std::move(exprs));
    }
    case TokenKind::Set: {
        advance();
        expect(TokenKind::Bang, "'!'");
        if (cur_.kind != TokenKind::Ident) {
            throw ParseError("expected identifier after set!");
        }
        std::string name = cur_.text;
        advance();
        auto expr = parse_expr();
        return std::make_unique<SetBangExpr>(
            std::move(name), std::move(expr));
    }
    case TokenKind::VectorKw: {
        advance();
        expect(TokenKind::LParen, "'('");
        std::vector<std::unique_ptr<Expr>> elems;
        elems.push_back(parse_expr());
        // invariant: elems has all parsed elements so far
        // decreases: remaining tokens until ')'
        while (cur_.kind == TokenKind::Comma) {
            advance();
            elems.push_back(parse_expr());
        }
        expect(TokenKind::RParen, "')'");
        return std::make_unique<VectorExpr>(std::move(elems));
    }
    case TokenKind::Length: {
        advance();
        expect(TokenKind::LParen, "'('");
        auto vec = parse_expr();
        expect(TokenKind::RParen, "')'");
        return std::make_unique<VectorLengthExpr>(std::move(vec));
    }
    case TokenKind::Lambda: {
        advance();
        expect(TokenKind::LParen, "'('");
        auto params = parse_params();
        expect(TokenKind::RParen, "')'");
        auto ret = parse_annotation();
        expect(TokenKind::LBrace, "'{'");
        auto body = parse_expr();
        expect(TokenKind::RBrace, "'}'");
        return std::make_unique<LambdaExpr>(
            std::move(params), std::move(ret), std::move(body));
    }
    case TokenKind::ProcArity: {
        advance();
        expect(TokenKind::LParen, "'('");
        auto inner = parse_expr();
        expect(TokenKind::RParen, "')'");
        return std::make_unique<ProcArityExpr>(std::move(inner));
    }
    case TokenKind::Void: {
        advance();
        return std::make_unique<VoidExpr>();
    }
    case TokenKind::Inject:
    case TokenKind::Project:
        return parse_cast();
    case TokenKind::IntegerP:
        return parse_type_pred(TypePred::Integer);
    case TokenKind::BooleanP:
        return parse_type_pred(TypePred::Boolean);
    case TokenKind::VectorP:
        return parse_type_pred(TypePred::Vector);
    case TokenKind::ProcedureP:
        return parse_type_pred(TypePred::Procedure);
    case TokenKind::VoidP:
        return parse_type_pred(TypePred::Void);
    case TokenKind::LParen: {
        advance();
        auto inner = parse_expr();
        expect(TokenKind::RParen, "')'");
        return std::move(inner);
    }
    default:
        throw ParseError("unexpected token: '" + cur_.text + "'");
    }
}

} // namespace mc
