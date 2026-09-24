#include "lexer.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool at_end(const Lexer *l) { return l->position >= l->length; }
static char peek(const Lexer *l) { return at_end(l) ? '\0' : l->source[l->position]; }
static char peek_next(const Lexer *l) { return l->position + 1 >= l->length ? '\0' : l->source[l->position + 1]; }
static char advance(Lexer *l) {
    if (at_end(l)) return '\0';
    char c = l->source[l->position++];
    if (c == '\n') { l->line++; l->column = 1; l->at_line_start = true; }
    else l->column++;
    return c;
}
static Token token_make(Lexer *l, TokenType type, size_t start, size_t line, size_t column) {
    Token t = { type, l->source + start, l->position - start, line, column, 0, 0.0 };
    return t;
}
static Token error_token(Lexer *l, size_t start, size_t line, size_t column) { return token_make(l, TOKEN_ERROR, start, line, column); }
static bool id_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static bool id_part(char c) { return isalnum((unsigned char)c) || c == '_'; }

static TokenType keyword(const char *s, size_t n) {
    static const char *names[] = { "let", "spark", "mut", "fn", "return", "serve", "if", "elif", "else", "for", "in", "while", "break", "slay", "continue", "skip", "true", "false", "null", "try", "catch", "finally", "match" };
    static const TokenType types[] = { TOKEN_LET, TOKEN_LET, TOKEN_MUT, TOKEN_FN, TOKEN_RETURN, TOKEN_RETURN, TOKEN_IF, TOKEN_ELIF, TOKEN_ELSE, TOKEN_FOR, TOKEN_IN, TOKEN_WHILE, TOKEN_BREAK, TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_CONTINUE, TOKEN_TRUE, TOKEN_FALSE, TOKEN_NULL, TOKEN_TRY, TOKEN_CATCH, TOKEN_FINALLY, TOKEN_MATCH };
    size_t count = sizeof(names) / sizeof(names[0]);
    for (size_t i = 0; i < count; i++) if (strlen(names[i]) == n && memcmp(s, names[i], n) == 0) return types[i];
    return TOKEN_IDENTIFIER;
}
static Token identifier(Lexer *l, size_t start, size_t line, size_t column) {
    while (id_part(peek(l))) advance(l);
    return token_make(l, keyword(l->source + start, l->position - start), start, line, column);
}
static Token number(Lexer *l, size_t start, size_t line, size_t column) {
    bool floating = false;
    while (isdigit((unsigned char)peek(l))) advance(l);
    if (peek(l) == '.' && isdigit((unsigned char)peek_next(l))) {
        floating = true; advance(l);
        while (isdigit((unsigned char)peek(l))) advance(l);
    }
    Token t = token_make(l, floating ? TOKEN_FLOAT : TOKEN_INTEGER, start, line, column);
    size_t n = l->position - start;
    char buffer[128];
    if (n >= sizeof(buffer)) { t.type = TOKEN_ERROR; return t; }
    memcpy(buffer, l->source + start, n); buffer[n] = '\0';
    char *end = NULL;
    errno = 0;
    if (floating) t.float_value = strtod(buffer, &end);
    else t.integer_value = strtoll(buffer, &end, 10);
    if (errno != 0 || end == buffer || *end != '\0') t.type = TOKEN_ERROR;
    return t;
}
static Token string_token(Lexer *l, size_t start, size_t line, size_t column) {
    advance(l);
    while (!at_end(l) && peek(l) != '"') {
        if (peek(l) == '\n') return error_token(l, start, line, column);
        if (peek(l) == '\\') { advance(l); if (!at_end(l)) advance(l); }
        else advance(l);
    }
    if (at_end(l)) return error_token(l, start, line, column);
    advance(l);
    return token_make(l, TOKEN_STRING, start, line, column);
}
static Token punctuation(Lexer *l, size_t start, size_t line, size_t column) {
    char c = advance(l);
    if (c == '-' && peek(l) == '>') { advance(l); return token_make(l, TOKEN_ARROW, start, line, column); }
    if ((c == '=' || c == '!' || c == '<' || c == '>') && peek(l) == '=') {
        advance(l);
        TokenType type = c == '=' ? TOKEN_EQUAL_EQUAL : c == '!' ? TOKEN_BANG_EQUAL : c == '<' ? TOKEN_LESS_EQUAL : TOKEN_GREATER_EQUAL;
        return token_make(l, type, start, line, column);
    }
    TokenType type;
    switch (c) {
        case '+': type = TOKEN_PLUS; break; case '-': type = TOKEN_MINUS; break;
        case '*': type = TOKEN_STAR; break; case '/': type = TOKEN_SLASH; break;
        case '%': type = TOKEN_PERCENT; break; case '=': type = TOKEN_ASSIGN; break;
        case '!': type = TOKEN_BANG; break; case '<': type = TOKEN_LESS; break;
        case '>': type = TOKEN_GREATER; break; case '(': type = TOKEN_LEFT_PAREN; break;
        case ')': type = TOKEN_RIGHT_PAREN; break; case '[': type = TOKEN_LEFT_BRACKET; break;
        case ']': type = TOKEN_RIGHT_BRACKET; break; case '{': type = TOKEN_LEFT_BRACE; break;
        case '}': type = TOKEN_RIGHT_BRACE; break; case ',': type = TOKEN_COMMA; break;
        case '.': type = TOKEN_DOT; break; case ':': type = TOKEN_COLON; break;
        default: return error_token(l, start, line, column);
    }
    return token_make(l, type, start, line, column);
}

static Token indentation(Lexer *l) {
    size_t start = l->position, line = l->line, column = l->column;
    unsigned spaces = 0;
    while (peek(l) == ' ' || peek(l) == '\t') spaces += advance(l) == '\t' ? 4U : 1U;
    if (peek(l) == '#' || peek(l) == '\n' || at_end(l)) {
        while (!at_end(l) && peek(l) != '\n') advance(l);
        l->at_line_start = false;
        return lexer_next(l);
    }
    unsigned current = l->indent_stack[l->indent_depth];
    l->at_line_start = false;
    if (spaces > current) {
        if (l->indent_depth + 1 >= FEM_MAX_INDENT_DEPTH) return error_token(l, start, line, column);
        l->indent_stack[++l->indent_depth] = spaces;
        return token_make(l, TOKEN_INDENT, start, line, column);
    }
    if (spaces < current) {
        while (l->indent_depth > 0 && spaces < l->indent_stack[l->indent_depth]) { l->indent_depth--; l->pending_dedents++; }
        if (spaces != l->indent_stack[l->indent_depth]) return error_token(l, start, line, column);
        if (l->pending_dedents > 0) { l->pending_dedents--; return token_make(l, TOKEN_DEDENT, start, line, column); }
    }
    return lexer_next(l);
}

void lexer_init(Lexer *l, const char *source, size_t length) {
    memset(l, 0, sizeof(*l)); l->source = source; l->length = length; l->line = 1; l->column = 1; l->at_line_start = true;
}
Token lexer_next(Lexer *l) {
    if (l->pending_dedents > 0) { l->pending_dedents--; return token_make(l, TOKEN_DEDENT, l->position, l->line, l->column); }
    if (l->at_line_start) return indentation(l);
    while (!at_end(l)) {
        char c = peek(l);
        if (c == ' ' || c == '\t' || c == '\r') { advance(l); continue; }
        if (c == '#') { while (!at_end(l) && peek(l) != '\n') advance(l); continue; }
        break;
    }
    if (at_end(l)) {
        if (l->indent_depth > 0 && !l->emitted_eof) { l->emitted_eof = true; l->pending_dedents = l->indent_depth; l->indent_depth = 0; return lexer_next(l); }
        Token t = token_make(l, TOKEN_EOF, l->position, l->line, l->column); l->emitted_eof = true; return t;
    }
    size_t start = l->position, line = l->line, column = l->column; char c = peek(l);
    if (c == '\n') { advance(l); return token_make(l, TOKEN_NEWLINE, start, line, column); }
    if (id_start(c)) return identifier(l, start, line, column);
    if (isdigit((unsigned char)c)) return number(l, start, line, column);
    if (c == '"') return string_token(l, start, line, column);
    return punctuation(l, start, line, column);
}
