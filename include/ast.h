#ifndef FEMLANG_AST_H
#define FEMLANG_AST_H

#include "token.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AST_PROGRAM,
    AST_BLOCK,
    AST_INTEGER,
    AST_FLOAT,
    AST_STRING,
    AST_BOOLEAN,
    AST_NULL,
    AST_IDENTIFIER,
    AST_LIST,
    AST_UNARY,
    AST_BINARY,
    AST_CALL,
    AST_LET,
    AST_ASSIGNMENT,
    AST_EXPRESSION_STATEMENT,
    AST_RETURN,
    AST_IF
} AstNodeType;

typedef struct AstNode AstNode;

typedef struct {
    AstNode **items;
    size_t count;
    size_t capacity;
} AstNodeList;

struct AstNode {
    AstNodeType type;
    size_t line;
    size_t column;
    union {
        int64_t integer;
        double floating;
        bool boolean;
        char *string;
        char *identifier;
        struct { AstNodeList elements; } list;
        struct { TokenType operator_type; AstNode *operand; } unary;
        struct { TokenType operator_type; AstNode *left; AstNode *right; } binary;
        struct { AstNode *callee; AstNodeList arguments; } call;
        struct { char *name; bool mutable; AstNode *value; } declaration;
        struct { char *name; AstNode *value; } assignment;
        struct { AstNode *expression; } expression_statement;
        struct { AstNode *value; } return_statement;
        struct { AstNode *condition; AstNode *then_branch; AstNode *else_branch; } if_statement;
        struct { AstNodeList statements; } block;
    } as;
};

AstNode *ast_new(AstNodeType type, size_t line, size_t column);
bool ast_list_push(AstNodeList *list, AstNode *node);
void ast_free(AstNode *node);

#endif