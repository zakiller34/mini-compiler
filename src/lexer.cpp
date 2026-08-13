#include "lexer.h"

#include <cctype>
#include <string>

namespace mc {

/// @brief Construct lexer, read first character
/// @requires in is valid readable stream
Lexer::Lexer(std::istream &in) : input_(in) { advance(); }

/// @brief Advance to next character
/// @ensures cur_ is next char or EOF
void Lexer::advance() {
    if (cur_ == '\n') {
        ++line_;
        col_ = 0;
    }
    int ch = input_.get();
    cur_ = (input_.good()) ? ch : EOF;
    ++col_;
}

/// @brief Skip whitespace and // line comments
/// @ensures cur_ is non-whitespace, non-comment char
void Lexer::skip_ws() {
    // decreases: distance to EOF
    // invariant: all skipped chars are whitespace or comment content
    while (true) {
        if (std::isspace(cur_) != 0) {
            advance();
        } else if (cur_ == '/' && input_.peek() == '/') {
            // decreases: distance to newline or EOF
            // invariant: inside a line comment
            while (cur_ != '\n' && cur_ != EOF) {
                advance();
            }
        } else {
            break;
        }
    }
}

/// @brief Scan a token and stamp it with the position of its first character
/// @ensures result.loc == token_loc()
Token Lexer::next() {
    Token t = next_impl();
    t.loc = tok_start_;
    return t;
}

// Character/keyword dispatch FSM: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
Token Lexer::next_impl() {
    skip_ws();
    tok_start_ = SourceLoc{line_, col_};

    if (cur_ == EOF) {
        return {TokenKind::Eof, "", 0};
    }

    // Multi-char and single-char tokens
    switch (cur_) {
    case '+': advance(); return {TokenKind::Plus, "+", 0};
    case '-':
        advance();
        if (cur_ == '>') { advance(); return {TokenKind::Arrow, "->", 0}; }
        return {TokenKind::Minus, "-", 0};
    case ':': advance(); return {TokenKind::Colon, ":", 0};
    case '(': advance(); return {TokenKind::LParen, "(", 0};
    case ')': advance(); return {TokenKind::RParen, ")", 0};
    case '{': advance(); return {TokenKind::LBrace, "{", 0};
    case '}': advance(); return {TokenKind::RBrace, "}", 0};
    case ';': advance(); return {TokenKind::Semicolon, ";", 0};
    case '!': advance(); return {TokenKind::Bang, "!", 0};
    case ',': advance(); return {TokenKind::Comma, ",", 0};
    case '[': advance(); return {TokenKind::LBracket, "[", 0};
    case ']': advance(); return {TokenKind::RBracket, "]", 0};
    case '=':
        advance();
        if (cur_ == '=') { advance(); return {TokenKind::EqEq, "==", 0}; }
        return {TokenKind::Equals, "=", 0};
    case '<':
        advance();
        if (cur_ == '=') { advance(); return {TokenKind::Le, "<=", 0}; }
        return {TokenKind::Lt, "<", 0};
    case '>':
        advance();
        if (cur_ == '=') { advance(); return {TokenKind::Ge, ">=", 0}; }
        return {TokenKind::Gt, ">", 0};
    default: break;
    }

    // Integer literal
    if (std::isdigit(cur_) != 0) {
        std::string num;
        // decreases: distance to non-digit char
        // invariant: num has all digit chars seen so far
        while (std::isdigit(cur_) != 0) {
            num += static_cast<char>(cur_);
            advance();
        }
        return {TokenKind::IntLit, num, std::stoll(num)};
    }

    // Identifier or keyword
    if (std::isalpha(cur_) != 0 || cur_ == '_') {
        std::string id;
        // decreases: distance to non-alnum/underscore char
        // invariant: id has all identifier chars seen so far
        while (std::isalnum(cur_) != 0 || cur_ == '_') {
            id += static_cast<char>(cur_);
            advance();
        }
        // A trailing '?' is part of the name, so `integer?` lexes as one token
        if (cur_ == '?') {
            id += '?';
            advance();
        }
        if (id == "let") { return {TokenKind::Let, id, 0}; }
        if (id == "read") { return {TokenKind::Read, id, 0}; }
        if (id == "in") { return {TokenKind::In, id, 0}; }
        if (id == "true") { return {TokenKind::True, id, 0}; }
        if (id == "false") { return {TokenKind::False, id, 0}; }
        if (id == "if") { return {TokenKind::If, id, 0}; }
        if (id == "else") { return {TokenKind::Else, id, 0}; }
        if (id == "and") { return {TokenKind::And, id, 0}; }
        if (id == "or") { return {TokenKind::Or, id, 0}; }
        if (id == "not") { return {TokenKind::Not, id, 0}; }
        if (id == "while") { return {TokenKind::While, id, 0}; }
        if (id == "void") { return {TokenKind::Void, id, 0}; }
        if (id == "begin") { return {TokenKind::Begin, id, 0}; }
        if (id == "set") { return {TokenKind::Set, id, 0}; }
        if (id == "vector") { return {TokenKind::VectorKw, id, 0}; }
        if (id == "length") { return {TokenKind::Length, id, 0}; }
        if (id == "fn") { return {TokenKind::Fn, id, 0}; }
        if (id == "Int") { return {TokenKind::Int_kw, id, 0}; }
        if (id == "Bool") { return {TokenKind::Bool_kw, id, 0}; }
        if (id == "lambda") { return {TokenKind::Lambda, id, 0}; }
        if (id == "procedure_arity") { return {TokenKind::ProcArity, id, 0}; }
        if (id == "Any") { return {TokenKind::Any_kw, id, 0}; }
        if (id == "inject") { return {TokenKind::Inject, id, 0}; }
        if (id == "project") { return {TokenKind::Project, id, 0}; }
        if (id == "integer?") { return {TokenKind::IntegerP, id, 0}; }
        if (id == "boolean?") { return {TokenKind::BooleanP, id, 0}; }
        if (id == "vector?") { return {TokenKind::VectorP, id, 0}; }
        if (id == "procedure?") { return {TokenKind::ProcedureP, id, 0}; }
        if (id == "void?") { return {TokenKind::VoidP, id, 0}; }
        return {TokenKind::Ident, id, 0};
    }

    std::string err(1, static_cast<char>(cur_));
    advance();
    return {TokenKind::Error, err, 0};
}

} // namespace mc
