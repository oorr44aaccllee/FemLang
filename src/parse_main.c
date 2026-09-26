#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Development tool: parses a file and prints the resulting AST. Not part of
 * the normal build; kept for interactive debugging.
 */

static void print_indent(size_t depth) {
    for (size_t i = 0; i < depth; i++) {
        fputs("  ", stdout);
    }
}

static void print_ast(const AstNode *node, size_t depth) {
    if (node == NULL) {
        print_indent(depth);
        puts("(empty)");
        return;
    }

    print_indent(depth);
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            puts(node->type == AST_PROGRAM ? "PROGRAM" : "BLOCK");
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                print_ast(node->as.block.statements.items[i], depth + 1);
            }
            break;
        case AST_INTEGER:
            printf("INTEGER %lld\n", (long long)node->as.integer);
            break;
        case AST_FLOAT:
            printf("FLOAT %g\n", node->as.floating);
            break;
        case AST_STRING:
            printf("STRING \"%s\"\n", node->as.string);
            break;
        case AST_BOOLEAN:
            printf("BOOLEAN %s\n", node->as.boolean ? "true" : "false");
            break;
        case AST_NULL:
            puts("NULL");
            break;
        case AST_IDENTIFIER:
            printf("IDENTIFIER %s\n", node->as.identifier);
            break;
        case AST_LIST:
            puts("LIST");
            for (size_t i = 0; i < node->as.list.elements.count; i++) {
                print_ast(node->as.list.elements.items[i], depth + 1);
            }
            break;
        case AST_UNARY:
            printf("UNARY %s\n", token_type_name(node->as.unary.operator_type));
            print_ast(node->as.unary.operand, depth + 1);
            break;
        case AST_BINARY:
            printf("BINARY %s\n", token_type_name(node->as.binary.operator_type));
            print_ast(node->as.binary.left, depth + 1);
            print_ast(node->as.binary.right, depth + 1);
            break;
        case AST_CALL:
            puts("CALL");
            print_ast(node->as.call.callee, depth + 1);
            for (size_t i = 0; i < node->as.call.arguments.count; i++) {
                print_ast(node->as.call.arguments.items[i], depth + 1);
            }
            break;
        case AST_LET:
            printf("%s %s =\n", node->as.declaration.mutable ? "MUT" : "LET",
                   node->as.declaration.name);
            print_ast(node->as.declaration.value, depth + 1);
            break;
        case AST_ASSIGNMENT:
            printf("ASSIGN %s =\n", node->as.assignment.name);
            print_ast(node->as.assignment.value, depth + 1);
            break;
        case AST_EXPRESSION_STATEMENT:
            puts("EXPRESSION_STATEMENT");
            print_ast(node->as.expression_statement.expression, depth + 1);
            break;
        case AST_FUNCTION:
            printf("FUNCTION %s(", node->as.function.name);
            for (size_t i = 0; i < node->as.function.parameters.count; i++) {
                if (i > 0) {
                    fputs(", ", stdout);
                }
                printf("%s", node->as.function.parameters.items[i]->as.identifier);
            }
            puts(")");
            print_ast(node->as.function.body, depth + 1);
            break;
        case AST_RETURN:
            puts("RETURN");
            print_ast(node->as.return_statement.value, depth + 1);
            break;
        case AST_IF:
            puts("IF");
            print_ast(node->as.if_statement.condition, depth + 1);
            print_ast(node->as.if_statement.then_branch, depth + 1);
            print_ast(node->as.if_statement.else_branch, depth + 1);
            break;
    }
}

static char *read_source_file(const char *path, size_t *length_out) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *source = malloc((size_t)size + 1U);
    if (source == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read_count = fread(source, 1, (size_t)size, file);
    fclose(file);
    if (read_count != (size_t)size) {
        free(source);
        return NULL;
    }
    source[size] = '\0';
    *length_out = (size_t)size;
    return source;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <source-file>\n", argv[0]);
        return 1;
    }

    size_t source_length = 0;
    char *source = read_source_file(argv[1], &source_length);
    if (source == NULL) {
        return 1;
    }

    Parser parser;
    parser_init(&parser, source, source_length);
    AstNode *program = parser_parse(&parser);
    if (program == NULL) {
        fprintf(stderr, "Parse error: %s\n", parser_error(&parser));
        free(source);
        return 1;
    }

    print_ast(program, 0);
    ast_free(program);
    free(source);
    return 0;
}