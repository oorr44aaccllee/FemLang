#include "ast.h"
#include "evaluator.h"
#include "lexer.h"
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

static void test_lexer_basics(void) {
    const char *source =
        "let answer = 42\n"
        "if answer >= 40:\n"
        "    answer + 1\n";

    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));

    const TokenType expected[] = {
        TOKEN_LET,
        TOKEN_IDENTIFIER,
        TOKEN_ASSIGN,
        TOKEN_INTEGER,
        TOKEN_NEWLINE,
        TOKEN_IF,
        TOKEN_IDENTIFIER,
        TOKEN_GREATER_EQUAL,
        TOKEN_INTEGER,
        TOKEN_COLON,
        TOKEN_NEWLINE,
        TOKEN_INDENT,
        TOKEN_IDENTIFIER,
        TOKEN_PLUS,
        TOKEN_INTEGER,
        TOKEN_NEWLINE,
        TOKEN_DEDENT,
        TOKEN_EOF
    };

    size_t count = sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0; i < count; i++) {
        Token token = lexer_next(&lexer);
        CHECK(token.type == expected[i]);
    }
}

static AstNode *parse_source(const char *source, Parser *parser) {
    parser_init(parser, source, strlen(source));
    return parser_parse(parser);
}

static void test_parser_if_else(void) {
    const char *source =
        "if true:\n"
        "    1\n"
        "else:\n"
        "    2\n";

    Parser parser;
    AstNode *program = parse_source(source, &parser);

    CHECK(program != NULL);
    CHECK(!parser.had_error);

    if (program != NULL) {
        CHECK(program->type == AST_PROGRAM);
        CHECK(program->as.program.statements.count == 1);

        AstNode *conditional = program->as.program.statements.items[0];
        CHECK(conditional->type == AST_IF);
        CHECK(conditional->as.if_statement.then_branch != NULL);
        CHECK(conditional->as.if_statement.else_branch != NULL);

        if (conditional->as.if_statement.then_branch != NULL) {
            CHECK(conditional->as.if_statement.then_branch->type == AST_BLOCK);
            CHECK(conditional->as.if_statement.then_branch->as.program.statements.count == 1);
        }

        ast_free(program);
    }
}

static void test_parser_precedence(void) {
    Parser parser;
    AstNode *program = parse_source("1 + 2 * 3\n", &parser);

    CHECK(program != NULL);
    CHECK(!parser.had_error);

    if (program != NULL) {
        AstNode *statement = program->as.program.statements.items[0];
        AstNode *expression = statement->as.expression_statement.expression;

        CHECK(expression->type == AST_BINARY);
        CHECK(expression->as.binary.operator_type == TOKEN_PLUS);
        CHECK(expression->as.binary.right->type == AST_BINARY);
        CHECK(expression->as.binary.right->as.binary.operator_type == TOKEN_STAR);

        ast_free(program);
    }
}

static Value evaluate_source(const char *source, int *ok) {
    Parser parser;
    AstNode *program = parse_source(source, &parser);

    if (program == NULL || parser.had_error) {
        *ok = 0;
        ast_free(program);
        return value_null();
    }

    Environment *environment = env_new();
    if (environment == NULL) {
        *ok = 0;
        ast_free(program);
        return value_null();
    }

    Value result = eval_ast(environment, program);
    env_free(environment);
    ast_free(program);
    *ok = 1;
    return result;
}

static void test_evaluator_if_else(void) {
    int ok = 0;
    Value result = evaluate_source(
        "let score = 10\n"
        "if score >= 10:\n"
        "    42\n"
        "else:\n"
        "    0\n",
        &ok
    );

    CHECK(ok == 1);
    CHECK(result.type == VALUE_INT);
    CHECK(result.as.integer == 42);
    value_free(&result);

    result = evaluate_source(
        "if false:\n"
        "    42\n"
        "else:\n"
        "    7\n",
        &ok
    );

    CHECK(ok == 1);
    CHECK(result.type == VALUE_INT);
    CHECK(result.as.integer == 7);
    value_free(&result);
}

static void test_evaluator_arithmetic(void) {
    int ok = 0;
    Value result = evaluate_source(
        "let base = 10\n"
        "let bonus = 5\n"
        "base * 2 + bonus\n",
        &ok
    );

    CHECK(ok == 1);
    CHECK(result.type == VALUE_INT);
    CHECK(result.as.integer == 25);
    value_free(&result);
}

int main(void) {
    test_lexer_basics();
    test_parser_if_else();
    test_parser_precedence();
    test_evaluator_if_else();
    test_evaluator_arithmetic();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }

    puts("All FemLang tests passed.");
    return EXIT_SUCCESS;
}
