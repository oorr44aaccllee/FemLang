#ifndef FEMLANG_LEXER_H
#define FEMLANG_LEXER_H
#include "token.h"
#include <stdbool.h>
#include <stddef.h>
#define FEM_MAX_INDENT_DEPTH 128
typedef struct { const char*source;size_t length,position,line,column;bool at_line_start,emitted_eof;unsigned indent_depth,indent_stack[FEM_MAX_INDENT_DEPTH],pending_dedents; } Lexer;
void lexer_init(Lexer*,const char*,size_t);Token lexer_next(Lexer*);
#endif
