#ifndef FEMLANG_LEXER_H
#define FEMLANG_LEXER_H

#include "token.h"

#include <stddef.h>

typedef struct {
    const char *source;
    size_t length;
    size_t position;
    size_t line;
    size_t column;
} Lexer;

void lexer_init(Lexer *lexer, const char *source, size_t length);
Token lexer_next(Lexer *lexer);

#endif
