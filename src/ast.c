#include "ast.h"

#include <stdlib.h>

AstNode *ast_new(AstNodeType type, size_t line, size_t column) {
    AstNode *node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->type = type;
    node->line = line;
    node->column = column;
    return node;
}

bool ast_list_push(AstNodeList *list, AstNode *node) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 4U : list->capacity * 2U;
        AstNode **new_items = realloc(list->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return false;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = node;
    return true;
}

static void free_node_list(AstNodeList *list) {
    for (size_t i = 0; i < list->count; i++) {
        ast_free(list->items[i]);
    }
    free(list->items);
}

/*
 * Releases every child reachable from node. Every AST node type declared in
 * ast.h must appear here so no owned subtree is leaked. Scalar node types
 * (integer, float, boolean, null) own nothing and fall through harmlessly.
 */
void ast_free(AstNode *node) {
    if (node == NULL) {
        return;
    }

    switch (node->type) {
        case AST_STRING:
            free(node->as.string);
            break;
        case AST_IDENTIFIER:
            free(node->as.identifier);
            break;
        case AST_LIST:
            free_node_list(&node->as.list.elements);
            break;
        case AST_UNARY:
            ast_free(node->as.unary.operand);
            break;
        case AST_BINARY:
            ast_free(node->as.binary.left);
            ast_free(node->as.binary.right);
            break;
        case AST_CALL:
            ast_free(node->as.call.callee);
            free_node_list(&node->as.call.arguments);
            break;
        case AST_LET:
            free(node->as.declaration.name);
            ast_free(node->as.declaration.value);
            break;
        case AST_ASSIGNMENT:
            free(node->as.assignment.name);
            ast_free(node->as.assignment.value);
            break;
        case AST_EXPRESSION_STATEMENT:
            ast_free(node->as.expression_statement.expression);
            break;
        case AST_RETURN:
            ast_free(node->as.return_statement.value);
            break;
        case AST_IF:
            ast_free(node->as.if_statement.condition);
            ast_free(node->as.if_statement.then_branch);
            ast_free(node->as.if_statement.else_branch);
            break;
        case AST_FUNCTION:
            free(node->as.function.name);
            free_node_list(&node->as.function.parameters);
            ast_free(node->as.function.body);
            break;
        case AST_PROGRAM:
        case AST_BLOCK:
            free_node_list(&node->as.block.statements);
            break;
        case AST_INTEGER:
        case AST_FLOAT:
        case AST_BOOLEAN:
        case AST_NULL:
            break;
    }

    free(node);
}