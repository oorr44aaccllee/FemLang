#include "ast.h"
#include <stdlib.h>

AstNode *ast_new(AstNodeType type, size_t line, size_t column) {
    AstNode *node = calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->type = type; node->line = line; node->column = column;
    return node;
}

bool ast_list_push(AstNodeList *list, AstNode *node) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2U : 4U;
        AstNode **items = realloc(list->items, capacity * sizeof(*items));
        if (!items) return false;
        list->items = items; list->capacity = capacity;
    }
    list->items[list->count++] = node;
    return true;
}

static void free_list(AstNodeList *list) {
    for (size_t i = 0; i < list->count; i++) ast_free(list->items[i]);
    free(list->items); list->items = NULL; list->count = 0; list->capacity = 0;
}

void ast_free(AstNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_STRING: free(node->as.string); break;
        case AST_IDENTIFIER: free(node->as.identifier); break;
        case AST_LIST: free_list(&node->as.list.elements); break;
        case AST_UNARY: ast_free(node->as.unary.operand); break;
        case AST_BINARY: ast_free(node->as.binary.left); ast_free(node->as.binary.right); break;
        case AST_CALL: ast_free(node->as.call.callee); free_list(&node->as.call.arguments); break;
        case AST_LET: free(node->as.declaration.name); ast_free(node->as.declaration.value); break;
        case AST_EXPRESSION_STATEMENT: ast_free(node->as.expression_statement.expression); break;
        case AST_RETURN: ast_free(node->as.return_statement.value); break;
        case AST_IF:
            ast_free(node->as.if_statement.condition);
            ast_free(node->as.if_statement.then_branch);
            ast_free(node->as.if_statement.else_branch);
            break;
        case AST_PROGRAM:
        case AST_BLOCK: free_list(&node->as.block.statements); break;
        default: break;
    }
    free(node);
}
