#ifndef FEMLANG_TOKEN_H
#define FEMLANG_TOKEN_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TOKEN_EOF = 0,
    TOKEN_ERROR,

    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_FLOAT,
    TOKEN_STRING,

    TOKEN_NEWLINE,
    TOKEN_INDENT,
    TOKEN_DEDENT,

    TOKEN_LET,
    TOKEN_MUT,
    TOKEN_FN,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELIF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_WHILE,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NULL,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_FINALLY,
    TOKEN_MATCH,

    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,

    TOKEN_ASSIGN,
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG,
    TOKEN_BANG_EQUAL,

    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,

    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,

    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_COLON,
    TOKEN_ARROW
} TokenType;

typedef struct {
    TokenType type;
    const char *start;
    size_t length;
    size_t line;
    size_t column;
    int64_t integer_value;
    double float_value;
} Token;

const char *token_type_name(TokenType type);

#endif
