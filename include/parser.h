#ifndef FEMLANG_PARSER_H
#define FEMLANG_PARSER_H

#include "ast.h"
#include "lexer.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    bool had_error;
    char error_message[256];
} Parser;

void parser_init(Parser *parser, const char *source, size_t length);
AstNode *parser_parse(Parser *parser);
const char *parser_error(const Parser *parser);

#endif
