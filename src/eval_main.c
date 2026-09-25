#include "evaluator.h"
#include "native.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

static Value native_print(size_t argument_count, const Value *arguments) {
    if (argument_count != 1) {
        return value_null();
    }
    const Value *argument = &arguments[0];
    if (argument->type == VALUE_STRING) {
        fputs(argument->as.string, stdout);
    } else {
        print_value(argument);
    }
    putchar('\n');
    return value_null();
}

static char *read_source_file(const char *path, size_t *length_out) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(file);
        return NULL;
    }
    long file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        perror("ftell/fseek");
        fclose(file);
        return NULL;
    }

    char *source = malloc((size_t)file_size + 1U);
    if (source == NULL) {
        fputs("Out of memory.\n", stderr);
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(source, 1, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        free(source);
        fputs("Unable to read the complete source file.\n", stderr);
        return NULL;
    }

    source[file_size] = '\0';
    *length_out = (size_t)file_size;
    return source;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <source-file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    size_t source_length = 0;
    char *source = read_source_file(argv[1], &source_length);
    if (source == NULL) {
        return EXIT_FAILURE;
    }

    Parser parser;
    parser_init(&parser, source, source_length);
    AstNode *program = parser_parse(&parser);
    if (program == NULL) {
        fprintf(stderr, "Parse error: %s\n", parser_error(&parser));
        free(source);
        return EXIT_FAILURE;
    }

    Environment *env = env_new();
    if (env == NULL) {
        fprintf(stderr, "Out of memory.\n");
        ast_free(program);
        free(source);
        return EXIT_FAILURE;
    }

    native_register(env_native_registry(env), "print", native_print);

    Value result = eval_ast(env, program);

    const char *runtime_error = env_error(env);
    if (runtime_error != NULL) {
        fprintf(stderr, "Runtime error: %s\n", runtime_error);
        value_free(&result);
        env_free(env);
        ast_free(program);
        free(source);
        return EXIT_FAILURE;
    }

    print_value(&result);
    putchar('\n');

    value_free(&result);
    env_free(env);
    ast_free(program);
    free(source);
    return EXIT_SUCCESS;
}