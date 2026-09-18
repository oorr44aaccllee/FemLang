#include "lexer.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool lexer_at_end(const Lexer *lexer) {
    return lexer->position >= lexer->length;
}

static char lexer_peek(const Lexer *lexer) {
    if (lexer_at_end(lexer)) {
        return '\0';
    }

    return lexer->source[lexer->position];
}

static char lexer_peek_next(const Lexer *lexer) {
    if (lexer->position + 1 >= lexer->length) {
        return '\0';
    }

    return lexer->source[lexer->position + 1];
}

static char lexer_advance(Lexer *lexer) {
    if (lexer_at_end(lexer)) {
        return '\0';
    }

    char c = lexer->source[lexer->position++];

    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
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

static bool is_identifier_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_identifier_part(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static TokenType identifier_type(const char *start, size_t length) {
    const char *keywords[] = {
        "let", "mut", "fn", "return", "if", "elif", "else", "for", "in",
        "while", "break", "continue", "true", "false", "null", "try",
        "catch", "finally", "match"
    };

    const TokenType keyword_types[] = {
        TOKEN_LET, TOKEN_MUT, TOKEN_FN, TOKEN_RETURN, TOKEN_IF, TOKEN_ELIF,
        TOKEN_ELSE, TOKEN_FOR, TOKEN_IN, TOKEN_WHILE, TOKEN_BREAK,
        TOKEN_CONTINUE, TOKEN_TRUE, TOKEN_FALSE, TOKEN_NULL, TOKEN_TRY,
        TOKEN_CATCH, TOKEN_FINALLY, TOKEN_MATCH
    };

    const size_t keyword_count = sizeof(keywords) / sizeof(keywords[0]);

    for (size_t i = 0; i < keyword_count; i++) {
        if (strlen(keywords[i]) == length &&
            memcmp(start, keywords[i], length) == 0) {
            return keyword_types[i];
        }
    }

    return TOKEN_IDENTIFIER;
}

static Token lexer_identifier(Lexer *lexer, size_t start, size_t line, size_t column) {
    while (is_identifier_part(lexer_peek(lexer))) {
        lexer_advance(lexer);
    }

    const size_t length = lexer->position - start;
    Token token = make_token(lexer,
        identifier_type(lexer->source + start, length),
        start,
        line,
        column);
    return token;
}

static Token lexer_number(Lexer *lexer, size_t start, size_t line, size_t column) {
    bool is_float = false;

    while (isdigit((unsigned char)lexer_peek(lexer))) {
        lexer_advance(lexer);
    }

    if (lexer_peek(lexer) == '.' && isdigit((unsigned char)lexer_peek_next(lexer))) {
        is_float = true;
        lexer_advance(lexer);

        while (isdigit((unsigned char)lexer_peek(lexer))) {
            lexer_advance(lexer);
        }
    }

    Token token = make_token(lexer,
        is_float ? TOKEN_FLOAT : TOKEN_INTEGER,
        start,
        line,
        column);

    const size_t length = lexer->position - start;
    char buffer[128];

    if (length >= sizeof(buffer)) {
        token.type = TOKEN_ERROR;
        return token;
    }

    memcpy(buffer, lexer->source + start, length);
    buffer[length] = '\0';

    char *end = NULL;

    if (is_float) {
        errno = 0;
        token.float_value = strtod(buffer, &end);
        if (errno != 0 || end == buffer || *end != '\0') {
            token.type = TOKEN_ERROR;
        }
    } else {
        errno = 0;
        token.integer_value = strtoll(buffer, &end, 10);
        if (errno != 0 || end == buffer || *end != '\0') {
            token.type = TOKEN_ERROR;
        }
    }

    return token;
}

static Token lexer_string(Lexer *lexer, size_t start, size_t line, size_t column) {
    lexer_advance(lexer);

    while (!lexer_at_end(lexer) && lexer_peek(lexer) != '"') {
        if (lexer_peek(lexer) == '\\') {
            lexer_advance(lexer);
            if (!lexer_at_end(lexer)) {
                lexer_advance(lexer);
            }
            continue;
        }

        if (lexer_peek(lexer) == '\n') {
            token_type_name(TOKEN_ERROR);
            Token token = make_token(lexer, TOKEN_ERROR, start, line, column);
            return token;
        }

        lexer_advance(lexer);
    }

    if (lexer_at_end(lexer)) {
        Token token = make_token(lexer, TOKEN_ERROR, start, line, column);
        return token;
    }

    lexer_advance(lexer);

    return make_token(lexer, TOKEN_STRING, start, line, column);
}

static Token lexer_operator(Lexer *lexer, size_t start, size_t line, size_t column) {
    char c = lexer_advance(lexer);

    switch (c) {
        case '+':
            return make_token(lexer, TOKEN_PLUS, start, line, column);
        case '-':
            if (lexer_peek(lexer) == '>') {
                lexer_advance(lexer);
                return make_token(lexer, TOKEN_ARROW, start, line, column);
            }
            return make_token(lexer, TOKEN_MINUS, start, line, column);
        case '*':
            return make_token(lexer, TOKEN_STAR, start, line, column);
        case '/':
            return make_token(lexer, TOKEN_SLASH, start, line, column);
        case '%':
            return make_token(lexer, TOKEN_PERCENT, start, line, column);
        case '=':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                return make_token(lexer, TOKEN_EQUAL_EQUAL, start, line, column);
            }
            return make_token(lexer, TOKEN_ASSIGN, start, line, column);
        case '!':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                return make_token(lexer, TOKEN_BANG_EQUAL, start, line, column);
            }
            return make_token(lexer, TOKEN_BANG, start, line, column);
        case '<':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                return make_token(lexer, TOKEN_LESS_EQUAL, start, line, column);
            }
            return make_token(lexer, TOKEN_LESS, start, line, column);
        case '>':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                return make_token(lexer, TOKEN_GREATER_EQUAL, start, line, column);
            }
            return make_token(lexer, TOKEN_GREATER, start, line, column);
        case '(':
            return make_token(lexer, TOKEN_LEFT_PAREN, start, line, column);
        case ')':
            return make_token(lexer, TOKEN_RIGHT_PAREN, start, line, column);
        case '[':
            return make_token(lexer, TOKEN_LEFT_BRACKET, start, line, column);
        case ']':
            return make_token(lexer, TOKEN_RIGHT_BRACKET, start, line, column);
        case '{':
            return make_token(lexer, TOKEN_LEFT_BRACE, start, line, column);
        case '}':
            return make_token(lexer, TOKEN_RIGHT_BRACE, start, line, column);
        case ',':
            return make_token(lexer, TOKEN_COMMA, start, line, column);
        case '.':
            return make_token(lexer, TOKEN_DOT, start, line, column);
        case ':':
            return make_token(lexer, TOKEN_COLON, start, line, column);
        default:
            return make_token(lexer, TOKEN_ERROR, start, line, column);
    }
}

void lexer_init(Lexer *lexer, const char *source, size_t length) {
    lexer->source = source;
    lexer->length = length;
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
}

Token lexer_next(Lexer *lexer) {
    while (!lexer_at_end(lexer)) {
        char c = lexer_peek(lexer);

        if (c == '\n') {
            size_t start = lexer->position;
            size_t line = lexer->line;
            size_t column = lexer->column;
            lexer_advance(lexer);
            return make_token(lexer, TOKEN_NEWLINE, start, line, column);
        }

        if (c == ' ' || c == '\t' || c == '\r') {
            lexer_advance(lexer);
            continue;
        }

        if (c == '#') {
            while (!lexer_at_end(lexer) && lexer_peek(lexer) != '\n') {
                lexer_advance(lexer);
            }
            continue;
        }

        if (is_identifier_start(c)) {
            size_t start = lexer->position;
            size_t line = lexer->line;
            size_t column = lexer->column;
            return lexer_identifier(lexer, start, line, column);
        }

        if (isdigit((unsigned char)c)) {
            size_t start = lexer->position;
            size_t line = lexer->line;
            size_t column = lexer->column;
            return lexer_number(lexer, start, line, column);
        }

        if (c == '"') {
            size_t start = lexer->position;
            size_t line = lexer->line;
            size_t column = lexer->column;
            return lexer_string(lexer, start, line, column);
        }

        size_t start = lexer->position;
        size_t line = lexer->line;
        size_t column = lexer->column;
        return lexer_operator(lexer, start, line, column);
    }

    Token token;
    token.type = TOKEN_EOF;
    token.start = lexer->source + lexer->position;
    token.length = 0;
    token.line = lexer->line;
    token.column = lexer->column;
    token.integer_value = 0;
    token.float_value = 0.0;

    return token;
}
