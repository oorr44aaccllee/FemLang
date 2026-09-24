#include "ast.h"
#include "evaluator.h"
#include "lexer.h"
#include "native.h"
#include "parser.h"
#include "token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static AstNode *parse_source(const char *source, Parser *parser) {
    parser_init(parser, source, strlen(source));
    return parser_parse(parser);
}

static Value evaluate_source(const char *source, int *ok) {
    Parser parser;
    AstNode *program = parse_source(source, &parser);
    if (program == NULL || parser.had_error) {
        ast_free(program);
        *ok = 0;
        return value_null();
    }

    Environment *environment = env_new();
    if (environment == NULL) {
        ast_free(program);
        *ok = 0;
        return value_null();
    }

    Value result = eval_ast(environment, program);
    env_free(environment);
    ast_free(program);
    *ok = 1;
    return result;
}

static void test_lexer_indent_tokens(void) {
    const char *source = "if true:\n    1\n2\n";
    const TokenType expected[] = {
        TOKEN_IF, TOKEN_TRUE, TOKEN_COLON, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_INTEGER, TOKEN_NEWLINE,
        TOKEN_DEDENT, TOKEN_INTEGER, TOKEN_NEWLINE, TOKEN_EOF
    };
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CHECK(lexer_next(&lexer).type == expected[i]);
    }
}

static void test_let_and_mut(void) {
    int ok = 0;
    Value result = evaluate_source(
        "let fixed = 1\n"
        "fixed = 2\n"
        "fixed\n",
        &ok
    );
    CHECK(ok == 1);
    CHECK(result.type == VALUE_NULL);
    value_free(&result);

    result = evaluate_source(
        "mut score = 1\n"
        "score = score + 2\n"
        "score\n",
        &ok
    );
    CHECK(ok == 1);
    CHECK(result.type == VALUE_INT);
    CHECK(result.as.integer == 3);
    value_free(&result);
}

static Value native_add(size_t argument_count, const Value *arguments) {
    if (argument_count != 2 || arguments[0].type != VALUE_INT ||
        arguments[1].type != VALUE_INT) {
        return value_null();
    }
    return value_int(arguments[0].as.integer + arguments[1].as.integer);
}

static void test_native_registry(void) {
    FemNativeRegistry registry;
    native_registry_init(&registry);

    CHECK(native_register(&registry, "add", native_add));
    CHECK(native_lookup(&registry, "missing") == NULL);

    FemNativeFunction add = native_lookup(&registry, "add");
    CHECK(add != NULL);

    if (add != NULL) {
        Value arguments[] = {value_int(2), value_int(3)};
        Value result = add(2, arguments);
        CHECK(result.type == VALUE_INT);
        CHECK(result.as.integer == 5);
        value_free(&result);
    }

    native_registry_free(&registry);
}

int main(void) {
    test_lexer_indent_tokens();
    test_let_and_mut();
    test_native_registry();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }

    puts("All FemLang tests passed.");
    return EXIT_SUCCESS;
}
