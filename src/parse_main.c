#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

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
            puts("PROGRAM");
            for (size_t i = 0; i < node->as.program.statements.count; i++) {
                print_ast(node->as.program.statements.items[i], depth + 1);
            }
            break;
        case AST_INTEGER:
            printf("INTEGER %lld\n", (long long)node->as.integer);
            break;
        case AST_FLOAT:
            printf("FLOAT %f\n", node->as.floating);
            break;
        case AST_STRING:
            printf("STRING %s\n", node->as.string);
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
        case AST_LET:
            printf("%s %s\n", node->as.declaration.mutable ? "MUT" : "LET", node->as.declaration.name);
            print_ast(node->as.declaration.value, depth + 1);
            break;
        case AST_RETURN:
            puts("RETURN");
            print_ast(node->as.return_statement.value, depth + 1);
            break;
        case AST_EXPRESSION_STATEMENT:
            puts("EXPRESSION_STATEMENT");
            print_ast(node->as.expression_statement.expression, depth + 1);
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
        default:
            puts("UNSUPPORTED");
            break;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <source-file>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror(argv[1]);
        return 1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 1;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }

    char *source = malloc((size_t)size + 1U);
    if (source == NULL) {
        fclose(file);
        return 1;
    }
    size_t read_count = fread(source, 1, (size_t)size, file);
    fclose(file);
    if (read_count != (size_t)size) {
        free(source);
        return 1;
    }
    source[size] = '\0';

    Parser parser;
    parser_init(&parser, source, (size_t)size);
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
