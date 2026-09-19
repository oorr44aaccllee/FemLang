#include "evaluator.h"

#include <stdio.h>
#include <stdlib.h>

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

    Environment *env = env_new();
    if (env == NULL) {
        ast_free(program);
        free(source);
        return 1;
    }

    Value result = eval_ast(env, program);
    print_value(&result);
    putchar('\n');

    value_free(&result);
    env_free(env);
    ast_free(program);
    free(source);
    return 0;
}
