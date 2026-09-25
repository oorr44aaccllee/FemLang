#include "lexer.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool at_end(const Lexer *lexer) {
    return lexer->position >= lexer->length;
}

static char peek_char(const Lexer *lexer) {
    if (at_end(lexer)) {
        return '\0';
    }
    return lexer->source[lexer->position];
}

static char peek_next_char(const Lexer *lexer) {
    if (lexer->position + 1 >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->position + 1];
}

static char advance(Lexer *lexer) {
    if (at_end(lexer)) {
        return '\0';
    }
    char c = lexer->source[lexer->position++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
        lexer->at_line_start = true;
    } else {
        lexer->column++;
    }
    return c;
}

static Token make_token(
    Lexer *lexer,
    TokenType type,
    size_t start,
    size_t line,
    size_t column
) {
    Token token;
    token.type = type;
    token.start = lexer->source + start;
    token.length = lexer->position - start;
    token.line = line;
    token.column = column;
    token.integer_value = 0;
    token.float_value = 0.0;
    return token;
}

static Token error_token(Lexer *lexer, size_t start, size_t line, size_t column) {
    return make_token(lexer, TOKEN_ERROR, start, line, column);
}

static bool is_identifier_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_identifier_part(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/*
 * Keywords are stored together with their playful aliases in matching order.
 * Several aliases intentionally map to the same canonical token type.
 */
static TokenType lookup_keyword(const char *start, size_t length) {
    static const char *const keyword_names[] = {
        "let",    "spark",   "mut",    "fn",    "return", "serve",
        "if",     "elif",    "else",   "for",   "in",     "while",
        "break",  "slay",    "continue", "skip", "true",  "false",
        "null",   "try",     "catch",  "finally", "match"
    };
    static const TokenType keyword_types[] = {
        TOKEN_LET,   TOKEN_LET,   TOKEN_MUT,   TOKEN_FN,
        TOKEN_RETURN, TOKEN_RETURN, TOKEN_IF,   TOKEN_ELIF,
        TOKEN_ELSE,  TOKEN_FOR,   TOKEN_IN,    TOKEN_WHILE,
        TOKEN_BREAK, TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_CONTINUE,
        TOKEN_TRUE,  TOKEN_FALSE, TOKEN_NULL,  TOKEN_TRY,
        TOKEN_CATCH, TOKEN_FINALLY, TOKEN_MATCH
    };

    for (size_t i = 0; i < sizeof(keyword_types) / sizeof(keyword_types[0]); i++) {
        if (strlen(keyword_names[i]) == length &&
            memcmp(keyword_names[i], start, length) == 0) {
            return keyword_types[i];
        }
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier_token(Lexer *lexer, size_t start, size_t line, size_t column) {
    while (is_identifier_part(peek_char(lexer))) {
        advance(lexer);
    }
    TokenType type = lookup_keyword(lexer->source + start, lexer->position - start);
    return make_token(lexer, type, start, line, column);
}

static Token number_token(Lexer *lexer, size_t start, size_t line, size_t column) {
    bool is_float = false;

    while (isdigit((unsigned char)peek_char(lexer))) {
        advance(lexer);
    }

    if (peek_char(lexer) == '.' && isdigit((unsigned char)peek_next_char(lexer))) {
        is_float = true;
        advance(lexer);
        while (isdigit((unsigned char)peek_char(lexer))) {
            advance(lexer);
        }
    }

    Token token = make_token(lexer, is_float ? TOKEN_FLOAT : TOKEN_INTEGER,
                             start, line, column);

    size_t length = lexer->position - start;
    char buffer[128];
    if (length >= sizeof(buffer)) {
        return error_token(lexer, start, line, column);
    }
    memcpy(buffer, lexer->source + start, length);
    buffer[length] = '\0';

    char *end = NULL;
    errno = 0;
    if (is_float) {
        token.float_value = strtod(buffer, &end);
    } else {
        token.integer_value = strtoll(buffer, &end, 10);
    }
    if (errno != 0 || end == buffer || *end != '\0') {
        return error_token(lexer, start, line, column);
    }
    return token;
}

/*
 * Consumes a string literal including both surrounding quotes. The token slice
 * keeps the quotes and raw escape sequences; the parser decodes the content.
 */
static Token string_token(Lexer *lexer, size_t start, size_t line, size_t column) {
    advance(lexer);

    while (!at_end(lexer) && peek_char(lexer) != '"') {
        if (peek_char(lexer) == '\n') {
            return error_token(lexer, start, line, column);
        }
        if (peek_char(lexer) == '\\') {
            advance(lexer);
            if (!at_end(lexer)) {
                advance(lexer);
            }
        } else {
            advance(lexer);
        }
    }

    if (at_end(lexer)) {
        return error_token(lexer, start, line, column);
    }
    advance(lexer);
    return make_token(lexer, TOKEN_STRING, start, line, column);
}

static Token punctuation_token(
    Lexer *lexer,
    size_t start,
    size_t line,
    size_t column
) {
    char c = advance(lexer);

    if (c == '-' && peek_char(lexer) == '>') {
        advance(lexer);
        return make_token(lexer, TOKEN_ARROW, start, line, column);
    }

    if ((c == '=' || c == '!' || c == '<' || c == '>') && peek_char(lexer) == '=') {
        advance(lexer);
        TokenType type;
        if (c == '=') {
            type = TOKEN_EQUAL_EQUAL;
        } else if (c == '!') {
            type = TOKEN_BANG_EQUAL;
        } else if (c == '<') {
            type = TOKEN_LESS_EQUAL;
        } else {
            type = TOKEN_GREATER_EQUAL;
        }
        return make_token(lexer, type, start, line, column);
    }

    TokenType type;
    switch (c) {
        case '+': type = TOKEN_PLUS; break;
        case '-': type = TOKEN_MINUS; break;
        case '*': type = TOKEN_STAR; break;
        case '/': type = TOKEN_SLASH; break;
        case '%': type = TOKEN_PERCENT; break;
        case '=': type = TOKEN_ASSIGN; break;
        case '!': type = TOKEN_BANG; break;
        case '<': type = TOKEN_LESS; break;
        case '>': type = TOKEN_GREATER; break;
        case '(': type = TOKEN_LEFT_PAREN; break;
        case ')': type = TOKEN_RIGHT_PAREN; break;
        case '[': type = TOKEN_LEFT_BRACKET; break;
        case ']': type = TOKEN_RIGHT_BRACKET; break;
        case '{': type = TOKEN_LEFT_BRACE; break;
        case '}': type = TOKEN_RIGHT_BRACE; break;
        case ',': type = TOKEN_COMMA; break;
        case '.': type = TOKEN_DOT; break;
        case ':': type = TOKEN_COLON; break;
        default: return error_token(lexer, start, line, column);
    }
    return make_token(lexer, type, start, line, column);
}

/*
 * Called only while at_line_start is true. Consumes the leading whitespace and
 * decides whether the line begins a block (INDENT), ends one (DEDENTs), or
 * continues at the current level. Blank and comment-only lines never change
 * the indentation stack and are skipped entirely.
 */
static Token indentation_token(Lexer *lexer) {
    size_t start = lexer->position;
    size_t line = lexer->line;
    size_t column = lexer->column;

    unsigned int spaces = 0;
    while (peek_char(lexer) == ' ' || peek_char(lexer) == '\t') {
        spaces += advance(lexer) == '\t' ? 4U : 1U;
    }

    if (peek_char(lexer) == '#' || peek_char(lexer) == '\n' ||
        peek_char(lexer) == '\r' || at_end(lexer)) {
        while (!at_end(lexer) && peek_char(lexer) != '\n') {
            advance(lexer);
        }
        lexer->at_line_start = false;
        return lexer_next(lexer);
    }

    unsigned int current_indent = lexer->indent_stack[lexer->indent_depth];
    lexer->at_line_start = false;

    if (spaces > current_indent) {
        if (lexer->indent_depth + 1 >= FEM_MAX_INDENT_DEPTH) {
            return error_token(lexer, start, line, column);
        }
        lexer->indent_stack[++lexer->indent_depth] = spaces;
        return make_token(lexer, TOKEN_INDENT, start, line, column);
    }

    if (spaces < current_indent) {
        while (lexer->indent_depth > 0 &&
               spaces < lexer->indent_stack[lexer->indent_depth]) {
            lexer->indent_depth--;
            lexer->pending_dedents++;
        }
        if (spaces != lexer->indent_stack[lexer->indent_depth]) {
            return error_token(lexer, start, line, column);
        }
        if (lexer->pending_dedents > 0) {
            lexer->pending_dedents--;
            return make_token(lexer, TOKEN_DEDENT, start, line, column);
        }
    }

    return lexer_next(lexer);
}

void lexer_init(Lexer *lexer, const char *source, size_t length) {
    memset(lexer, 0, sizeof(*lexer));
    lexer->source = source;
    lexer->length = length;
    lexer->line = 1;
    lexer->column = 1;
    lexer->at_line_start = true;
}

Token lexer_next(Lexer *lexer) {
    if (lexer->pending_dedents > 0) {
        lexer->pending_dedents--;
        return make_token(lexer, TOKEN_DEDENT, lexer->position,
                          lexer->line, lexer->column);
    }

    if (lexer->at_line_start) {
        return indentation_token(lexer);
    }

    while (!at_end(lexer)) {
        char c = peek_char(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lexer);
            continue;
        }
        if (c == '#') {
            while (!at_end(lexer) && peek_char(lexer) != '\n') {
                advance(lexer);
            }
            continue;
        }
        break;
    }

    if (at_end(lexer)) {
        if (lexer->indent_depth > 0 && !lexer->emitted_eof) {
            lexer->emitted_eof = true;
            lexer->pending_dedents = lexer->indent_depth;
            lexer->indent_depth = 0;
            return lexer_next(lexer);
        }
        lexer->emitted_eof = true;
        return make_token(lexer, TOKEN_EOF, lexer->position,
                          lexer->line, lexer->column);
    }

    size_t start = lexer->position;
    size_t line = lexer->line;
    size_t column = lexer->column;
    char c = peek_char(lexer);

    if (c == '\n') {
        advance(lexer);
        return make_token(lexer, TOKEN_NEWLINE, start, line, column);
    }
    if (is_identifier_start(c)) {
        return identifier_token(lexer, start, line, column);
    }
    if (isdigit((unsigned char)c)) {
        return number_token(lexer, start, line, column);
    }
    if (c == '"') {
        return string_token(lexer, start, line, column);
    }
    return punctuation_token(lexer, start, line, column);
}