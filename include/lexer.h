#ifndef FEMLANG_LEXER_H
#define FEMLANG_LEXER_H

#include "token.h"
#include <stdbool.h>
#include <stddef.h>

#define FEM_MAX_INDENT_DEPTH 128

typedef struct {
    const char *source;
    size_t length;
    size_t position;
    size_t line;
    size_t column;
    bool at_line_start;
    bool emitted_eof;
    unsigned indent_depth;
    unsigned indent_stack[FEM_MAX_INDENT_DEPTH];
    unsigned pending_dedents;
} Lexer;

void lexer_init(Lexer *lexer, const char *source, size_t length);
Token lexer_next(Lexer *lexer);

#endif
