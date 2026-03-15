#include "lexer.h"

#include <cctype>
#include <string>

/// @brief Construct lexer, read first character
/// @requires in is valid readable stream
Lexer::Lexer(std::istream &in) : input_(in) { advance(); }

/// @brief Advance to next character
/// @ensures cur_ is next char or EOF
void Lexer::advance() {
    int ch = input_.get();
    cur_ = (input_.good()) ? ch : EOF;
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

/// @brief Get next token
/// @ensures result is valid Token
Token Lexer::next() {
    skip_ws();

    if (cur_ == EOF) {
        return {TokenKind::Eof, "", 0};
    }

    // Multi-char and single-char tokens
    switch (cur_) {
    case '+': advance(); return {TokenKind::Plus, "+", 0};
    case '-': advance(); return {TokenKind::Minus, "-", 0};
    case '(': advance(); return {TokenKind::LParen, "(", 0};
    case ')': advance(); return {TokenKind::RParen, ")", 0};
    case '{': advance(); return {TokenKind::LBrace, "{", 0};
    case '}': advance(); return {TokenKind::RBrace, "}", 0};
    case ';': advance(); return {TokenKind::Semicolon, ";", 0};
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
        return {TokenKind::Ident, id, 0};
    }

    std::string err(1, static_cast<char>(cur_));
    advance();
    return {TokenKind::Error, err, 0};
}
