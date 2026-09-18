#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_token(Token token) {
    printf("%s at line %zu, column %zu", token_type_name(token.type), token.line, token.column);

    if (token.type == TOKEN_IDENTIFIER || token.type == TOKEN_STRING) {
        printf(" :: ");
        size_t length = token.length;
        if (length > 32) {
            length = 32;
        }
        printf("\"%.*s\"", (int)length, token.start);
    } else if (token.type == TOKEN_INTEGER) {
        printf(" :: %lld", (long long)token.integer_value);
    } else if (token.type == TOKEN_FLOAT) {
        printf(" :: %f", token.float_value);
    }

    printf("\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source-file>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        fprintf(stderr, "Unable to open file: %s\n", argv[1]);
        return 1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Unable to seek file: %s\n", argv[1]);
        fclose(file);
        return 1;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        fprintf(stderr, "Unable to determine file size: %s\n", argv[1]);
        fclose(file);
        return 1;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Unable to reset file position: %s\n", argv[1]);
        fclose(file);
        return 1;
    }

    char *source = malloc((size_t)file_size + 1U);
    if (source == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        fclose(file);
        return 1;
    }

    size_t bytes_read = fread(source, 1, (size_t)file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Unable to read full source file.\n");
        free(source);
        return 1;
    }

    source[file_size] = '\0';

    Lexer lexer;
    lexer_init(&lexer, source, (size_t)file_size);

    for (;;) {
        Token token = lexer_next(&lexer);
        print_token(token);

        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) {
            break;
        }
    }

    free(source);
    return 0;
}
