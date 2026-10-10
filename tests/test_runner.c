#include "builtins.h"
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
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

/*
 * Lexer helpers
 */

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenStream;

static void lex_source(const char *source, TokenStream *stream) {
    stream->items = NULL;
    stream->count = 0;
    stream->capacity = 0;

    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));

    for (;;) {
        Token token = lexer_next(&lexer);
        if (stream->count == stream->capacity) {
            size_t new_capacity = stream->capacity == 0 ? 16U : stream->capacity * 2U;
            Token *new_items = realloc(stream->items, new_capacity * sizeof(Token));
            if (new_items == NULL) {
                fputs("FAIL: test lexer ran out of memory\n", stderr);
                free(stream->items);
                stream->items = NULL;
                stream->count = 0;
                stream->capacity = 0;
                return;
            }
            stream->items = new_items;
            stream->capacity = new_capacity;
        }
        stream->items[stream->count++] = token;
        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) {
            break;
        }
    }
}

static void expect_tokens(
    const char *source,
    const TokenType *expected,
    size_t expected_count
) {
    TokenStream stream;
    lex_source(source, &stream);
    if (stream.count == 0) {
        CHECK(stream.count == expected_count);
        return;
    }

    size_t common = stream.count < expected_count ? stream.count : expected_count;
    for (size_t i = 0; i < common; i++) {
        if (stream.items[i].type != expected[i]) {
            fprintf(stderr,
                    "FAIL: token %zu: expected %s, got %s in source:\n%s\n",
                    i, token_type_name(expected[i]),
                    token_type_name(stream.items[i].type), source);
            failures++;
            break;
        }
    }
    if (stream.count != expected_count) {
        fprintf(stderr,
                "FAIL: token count %zu, expected %zu in source:\n%s\n",
                stream.count, expected_count, source);
        failures++;
    }
    free(stream.items);
}

static void expect_stream_contains_error(const char *source) {
    TokenStream stream;
    lex_source(source, &stream);
    bool found = false;
    for (size_t i = 0; i < stream.count; i++) {
        if (stream.items[i].type == TOKEN_ERROR) {
            found = true;
            break;
        }
    }
    CHECK(found);
    if (!found) {
        for (size_t i = 0; i < stream.count; i++) {
            fprintf(stderr, "  token %zu: %s\n", i,
                    token_type_name(stream.items[i].type));
        }
    }
    free(stream.items);
}

/*
 * Parser/evaluator helpers
 */

typedef struct {
    Value result;
    char error[256];
    int parsed;
} RunResult;

typedef void (*EnvSetupFn)(Environment *env);

static RunResult run_source_with_setup(const char *source, EnvSetupFn setup) {
    RunResult out;
    out.error[0] = '\0';
    out.parsed = 0;

    Parser parser;
    parser_init(&parser, source, strlen(source));
    AstNode *program = parser_parse(&parser);
    if (program == NULL) {
        out.result = value_null();
        snprintf(out.error, sizeof(out.error), "%s", parser_error(&parser));
        return out;
    }
    out.parsed = 1;

    Environment *env = env_new();
    if (env == NULL) {
        out.result = value_null();
        snprintf(out.error, sizeof(out.error), "out of memory");
        ast_free(program);
        return out;
    }

    if (setup != NULL) {
        setup(env);
    }

    out.result = eval_ast(env, program);
    const char *runtime_error = env_error(env);
    if (runtime_error != NULL) {
        snprintf(out.error, sizeof(out.error), "%s", runtime_error);
    }

    env_free(env);
    ast_free(program);
    return out;
}

static RunResult run_source(const char *source) {
    return run_source_with_setup(source, NULL);
}

static AstNode *parse_program(const char *source, Parser *out_parser) {
    parser_init(out_parser, source, strlen(source));
    return parser_parse(out_parser);
}

static void check_int_result(const char *source, int64_t expected) {
    RunResult run = run_source(source);
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_INT);
    CHECK(run.result.as.integer == expected);
    value_free(&run.result);
}

static void check_string_result(const char *source, const char *expected) {
    RunResult run = run_source(source);
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_STRING);
    CHECK(expected != NULL && strcmp(run.result.as.string, expected) == 0);
    value_free(&run.result);
}

static void check_bool_result(const char *source, bool expected) {
    RunResult run = run_source(source);
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_BOOL);
    CHECK(run.result.as.boolean == expected);
    value_free(&run.result);
}

static void check_float_result(const char *source, double expected) {
    RunResult run = run_source(source);
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_FLOAT);
    if (run.result.type == VALUE_FLOAT) {
        double delta = run.result.as.floating - expected;
        if (delta < 0.0) {
            delta = -delta;
        }
        CHECK(delta < 1e-9);
    }
    value_free(&run.result);
}

/*
 * Test natives registered into the environment's registry so call expressions
 * can be exercised end to end.
 */

static Value native_add(size_t argument_count, const Value *arguments) {
    if (argument_count != 2 ||
        arguments[0].type != VALUE_INT ||
        arguments[1].type != VALUE_INT) {
        return value_null();
    }
    return value_int(arguments[0].as.integer + arguments[1].as.integer);
}

static Value native_double(size_t argument_count, const Value *arguments) {
    if (argument_count != 1 || arguments[0].type != VALUE_INT) {
        return value_null();
    }
    return value_int(arguments[0].as.integer * 2);
}

static Value native_first_string(size_t argument_count, const Value *arguments) {
    if (argument_count != 1 || arguments[0].type != VALUE_STRING) {
        return value_null();
    }
    return value_string_copy(arguments[0].as.string);
}

static Value native_boom(size_t argument_count, const Value *arguments) {
    (void)argument_count;
    (void)arguments;
    return value_error("boom");
}

static void register_test_natives(Environment *env) {
    FemNativeRegistry *natives = env_native_registry(env);
    CHECK(native_register(natives, "add", native_add));
    CHECK(native_register(natives, "double", native_double));
    CHECK(native_register(natives, "name", native_first_string));
    CHECK(native_register(natives, "boom", native_boom));
}

static void register_stdlib(Environment *env) {
    fem_stdlib_register(env_native_registry(env));
}

static RunResult run_source_with_natives(const char *source) {
    return run_source_with_setup(source, register_test_natives);
}

static RunResult run_source_with_stdlib(const char *source) {
    return run_source_with_setup(source, register_stdlib);
}

/*
 * Standard library result helpers. Declared here so evaluator tests that run
 * whole programs through the stdlib can use them before their definitions in
 * the stdlib section below.
 */
static void check_stdlib_int(const char *source, int64_t expected);
static void check_stdlib_float(const char *source, double expected);
static void check_stdlib_string(const char *source, const char *expected);
static void check_stdlib_error(const char *source);
static void check_stdlib_null(const char *source);

/*
 * Lexer tests
 */

static void test_lexer_keywords(void) {
    const TokenType expected[] = {
        TOKEN_LET, TOKEN_MUT, TOKEN_FN, TOKEN_RETURN, TOKEN_IF,
        TOKEN_ELIF, TOKEN_ELSE, TOKEN_FOR, TOKEN_IN, TOKEN_WHILE,
        TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_TRUE, TOKEN_FALSE, TOKEN_NULL,
        TOKEN_TRY, TOKEN_CATCH, TOKEN_FINALLY, TOKEN_MATCH, TOKEN_EOF
    };
    expect_tokens(
        "let mut fn return if elif else for in while break continue "
        "true false null try catch finally match",
        expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_playful_aliases(void) {
    const TokenType expected[] = {
        TOKEN_LET, TOKEN_LET, TOKEN_RETURN, TOKEN_RETURN,
        TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_EOF
    };
    expect_tokens("spark let serve return slay skip",
                  expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_identifiers(void) {
    const TokenType expected[] = {
        TOKEN_IDENTIFIER, TOKEN_IDENTIFIER, TOKEN_IDENTIFIER, TOKEN_EOF
    };
    expect_tokens("foo _bar camelCase2", expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_numbers(void) {
    const TokenType expected[] = {
        TOKEN_INTEGER, TOKEN_NEWLINE, TOKEN_FLOAT, TOKEN_NEWLINE,
        TOKEN_FLOAT, TOKEN_NEWLINE, TOKEN_EOF
    };
    expect_tokens("42\n3.14\n0.5\n", expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_strings(void) {
    const TokenType expected[] = {
        TOKEN_STRING, TOKEN_STRING, TOKEN_STRING, TOKEN_EOF
    };
    expect_tokens("\"hello\" \"\" \"a\\\"b\"", expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_unterminated_string(void) {
    expect_stream_contains_error("\"hello");
}

static void test_lexer_operators(void) {
    const TokenType expected[] = {
        TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
        TOKEN_ASSIGN, TOKEN_EQUAL_EQUAL, TOKEN_BANG, TOKEN_BANG_EQUAL,
        TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_GREATER, TOKEN_GREATER_EQUAL,
        TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
        TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
        TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
        TOKEN_COMMA, TOKEN_DOT, TOKEN_DOT_DOT, TOKEN_COLON, TOKEN_ARROW,
        TOKEN_EOF
    };
    expect_tokens(
        "+ - * / % = == ! != < <= > >= ( ) [ ] { } , . .. : ->",
        expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_newlines_and_eof(void) {
    const TokenType expected[] = {
        TOKEN_INTEGER, TOKEN_NEWLINE, TOKEN_INTEGER, TOKEN_NEWLINE, TOKEN_EOF
    };
    expect_tokens("1\n2\n", expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_no_final_newline(void) {
    const TokenType expected[] = {
        TOKEN_LET, TOKEN_IDENTIFIER, TOKEN_ASSIGN, TOKEN_INTEGER, TOKEN_EOF
    };
    expect_tokens("let x = 1", expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_indentation_and_dedentation(void) {
    const TokenType expected[] = {
        TOKEN_IF, TOKEN_TRUE, TOKEN_COLON, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_INTEGER, TOKEN_NEWLINE,
        TOKEN_DEDENT, TOKEN_INTEGER, TOKEN_NEWLINE, TOKEN_EOF
    };
    expect_tokens("if true:\n    1\n2\n", expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_nested_indentation(void) {
    const TokenType expected[] = {
        TOKEN_IF, TOKEN_IDENTIFIER, TOKEN_COLON, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_IF, TOKEN_IDENTIFIER, TOKEN_COLON, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_INTEGER, TOKEN_NEWLINE,
        TOKEN_DEDENT, TOKEN_INTEGER, TOKEN_NEWLINE,
        TOKEN_DEDENT, TOKEN_EOF
    };
    expect_tokens("if a:\n    if b:\n        1\n    2\n", expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_dedent_at_file_end(void) {
    const TokenType expected[] = {
        TOKEN_IF, TOKEN_IDENTIFIER, TOKEN_COLON, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_INTEGER, TOKEN_DEDENT, TOKEN_EOF
    };
    expect_tokens("if a:\n    1", expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_blank_and_comment_lines(void) {
    const TokenType expected[] = {
        TOKEN_INTEGER, TOKEN_NEWLINE,
        TOKEN_NEWLINE,
        TOKEN_NEWLINE,
        TOKEN_INTEGER, TOKEN_NEWLINE, TOKEN_EOF
    };
    expect_tokens("1\n\n# comment\n2\n", expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_lexer_inconsistent_indentation(void) {
    expect_stream_contains_error("if true:\n    1\n  2\n");
}

static void test_lexer_comment_after_code(void) {
    const TokenType expected[] = {
        TOKEN_INTEGER, TOKEN_NEWLINE, TOKEN_INTEGER, TOKEN_NEWLINE, TOKEN_EOF
    };
    expect_tokens("1 # trailing comment\n2\n", expected,
                  sizeof(expected) / sizeof(expected[0]));
}

/*
 * Parser tests
 */

static void test_parser_immutable_declaration(void) {
    Parser parser;
    AstNode *program = parse_program("let fixed = 1\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    CHECK(program->type == AST_PROGRAM);
    CHECK(program->as.block.statements.count == 1);
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_LET);
    CHECK(strcmp(stmt->as.declaration.name, "fixed") == 0);
    CHECK(stmt->as.declaration.mutable == false);
    CHECK(stmt->as.declaration.value != NULL);
    CHECK(stmt->as.declaration.value->type == AST_INTEGER);
    ast_free(program);
}

static void test_parser_mutable_declaration(void) {
    Parser parser;
    AstNode *program = parse_program("mut score = 5\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_LET);
    CHECK(strcmp(stmt->as.declaration.name, "score") == 0);
    CHECK(stmt->as.declaration.mutable == true);
    ast_free(program);
}

static void test_parser_assignment(void) {
    Parser parser;
    AstNode *program = parse_program("score = 3\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_ASSIGNMENT);
    CHECK(strcmp(stmt->as.assignment.name, "score") == 0);
    CHECK(stmt->as.assignment.value != NULL);
    CHECK(stmt->as.assignment.value->type == AST_INTEGER);
    ast_free(program);
}

static void test_parser_string_literal_content(void) {
    Parser parser;
    AstNode *program = parse_program("\"Alex\"\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_EXPRESSION_STATEMENT);
    AstNode *expr = stmt->as.expression_statement.expression;
    CHECK(expr->type == AST_STRING);
    CHECK(strcmp(expr->as.string, "Alex") == 0);
    ast_free(program);
}

static void test_parser_string_escapes(void) {
    Parser parser;
    AstNode *program = parse_program("\"a\\nb\\\"c\"\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    AstNode *expr = stmt->as.expression_statement.expression;
    CHECK(expr->type == AST_STRING);
    CHECK(strcmp(expr->as.string, "a\nb\"c") == 0);
    ast_free(program);
}

static void test_parser_arithmetic_precedence(void) {
    Parser parser;
    AstNode *program = parse_program("2 + 3 * 4\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_EXPRESSION_STATEMENT);
    AstNode *expr = stmt->as.expression_statement.expression;
    CHECK(expr->type == AST_BINARY);
    CHECK(expr->as.binary.operator_type == TOKEN_PLUS);
    CHECK(expr->as.binary.left->type == AST_INTEGER);
    CHECK(expr->as.binary.right->type == AST_BINARY);
    CHECK(expr->as.binary.right->as.binary.operator_type == TOKEN_STAR);
    ast_free(program);
}

static void test_parser_identifier_expression_statement(void) {
    Parser parser;
    AstNode *program = parse_program("x + 1\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_EXPRESSION_STATEMENT);
    AstNode *expr = stmt->as.expression_statement.expression;
    CHECK(expr->type == AST_BINARY);
    CHECK(expr->as.binary.operator_type == TOKEN_PLUS);
    CHECK(expr->as.binary.left->type == AST_IDENTIFIER);
    ast_free(program);
}

static void test_parser_identifier_precedence(void) {
    /* a * b + c must parse as (a * b) + c, not a * (b + c). */
    Parser parser;
    AstNode *program = parse_program("a * b + c\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *expr = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(expr->type == AST_BINARY);
    CHECK(expr->as.binary.operator_type == TOKEN_PLUS);
    CHECK(expr->as.binary.left->type == AST_BINARY);
    CHECK(expr->as.binary.left->as.binary.operator_type == TOKEN_STAR);
    CHECK(expr->as.binary.right->type == AST_IDENTIFIER);
    ast_free(program);
}

static void test_parser_assignment_precedence(void) {
    Parser parser;
    AstNode *program = parse_program("score = 1 + 2 * 3\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_ASSIGNMENT);
    AstNode *value = stmt->as.assignment.value;
    CHECK(value->type == AST_BINARY);
    CHECK(value->as.binary.operator_type == TOKEN_PLUS);
    CHECK(value->as.binary.right->as.binary.operator_type == TOKEN_STAR);
    ast_free(program);
}

static void test_parser_if_else(void) {
    Parser parser;
    AstNode *program = parse_program("if true:\n    1\nelse:\n    2\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_IF);
    CHECK(stmt->as.if_statement.condition != NULL);
    CHECK(stmt->as.if_statement.condition->type == AST_BOOLEAN);
    CHECK(stmt->as.if_statement.then_branch != NULL);
    CHECK(stmt->as.if_statement.then_branch->type == AST_BLOCK);
    CHECK(stmt->as.if_statement.then_branch->as.block.statements.count == 1);
    CHECK(stmt->as.if_statement.else_branch != NULL);
    CHECK(stmt->as.if_statement.else_branch->type == AST_BLOCK);
    ast_free(program);
}

static void test_parser_if_elif_else(void) {
    Parser parser;
    AstNode *program = parse_program(
        "if a:\n"
        "    1\n"
        "elif b:\n"
        "    2\n"
        "else:\n"
        "    3\n",
        &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_IF);
    CHECK(stmt->as.if_statement.condition->type == AST_IDENTIFIER);

    AstNode *elif_branch = stmt->as.if_statement.else_branch;
    CHECK(elif_branch != NULL);
    CHECK(elif_branch->type == AST_IF);
    CHECK(elif_branch->as.if_statement.condition->type == AST_IDENTIFIER);
    CHECK(strcmp(elif_branch->as.if_statement.condition->as.identifier, "b") == 0);
    CHECK(elif_branch->as.if_statement.else_branch != NULL);
    CHECK(elif_branch->as.if_statement.else_branch->type == AST_BLOCK);
    ast_free(program);
}

static void test_parser_if_elif_chain(void) {
    Parser parser;
    AstNode *program = parse_program(
        "if a:\n"
        "    1\n"
        "elif b:\n"
        "    2\n"
        "elif c:\n"
        "    3\n"
        "else:\n"
        "    4\n",
        &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    AstNode *first_elif = stmt->as.if_statement.else_branch;
    CHECK(first_elif != NULL);
    CHECK(first_elif->type == AST_IF);
    AstNode *second_elif = first_elif->as.if_statement.else_branch;
    CHECK(second_elif != NULL);
    CHECK(second_elif->type == AST_IF);
    CHECK(strcmp(second_elif->as.if_statement.condition->as.identifier, "c") == 0);
    CHECK(second_elif->as.if_statement.else_branch->type == AST_BLOCK);
    ast_free(program);
}

static void test_parser_if_elif_no_else(void) {
    Parser parser;
    AstNode *program = parse_program(
        "if a:\n"
        "    1\n"
        "elif b:\n"
        "    2\n",
        &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    AstNode *elif_branch = stmt->as.if_statement.else_branch;
    CHECK(elif_branch->type == AST_IF);
    CHECK(elif_branch->as.if_statement.else_branch == NULL);
    ast_free(program);
}

static void test_parser_stray_elif_else(void) {
    Parser parser;
    AstNode *program = parse_program("elif true:\n    1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
    CHECK(strstr(parser.error_message, "elif without a matching if") != NULL);

    program = parse_program("else:\n    1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
    CHECK(strstr(parser.error_message, "else without a matching if") != NULL);
}

static void test_parser_block_statements(void) {
    Parser parser;
    AstNode *program = parse_program("if a:\n    b\n    c\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_IF);
    AstNode *block = stmt->as.if_statement.then_branch;
    CHECK(block->as.block.statements.count == 2);
    ast_free(program);
}

static void test_parser_invalid_declaration(void) {
    Parser parser;
    AstNode *program = parse_program("let = 5\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
}

static void test_parser_invalid_assignment(void) {
    Parser parser;
    AstNode *program = parse_program("x = \n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
}

static void test_parser_declaration_without_value(void) {
    Parser parser;
    AstNode *program = parse_program("let x\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
}

static void test_parser_call_empty(void) {
    Parser parser;
    AstNode *program = parse_program("f()\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *expr = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(expr->type == AST_CALL);
    CHECK(expr->as.call.callee->type == AST_IDENTIFIER);
    CHECK(strcmp(expr->as.call.callee->as.identifier, "f") == 0);
    CHECK(expr->as.call.arguments.count == 0);
    ast_free(program);
}

static void test_parser_call_arguments(void) {
    Parser parser;
    AstNode *program = parse_program("f(1, 2)\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *expr = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(expr->type == AST_CALL);
    CHECK(expr->as.call.arguments.count == 2);
    CHECK(expr->as.call.arguments.items[0]->type == AST_INTEGER);
    CHECK(expr->as.call.arguments.items[1]->type == AST_INTEGER);
    ast_free(program);
}

static void test_parser_call_argument_expression(void) {
    Parser parser;
    AstNode *program = parse_program("f(1 + 2, 3)\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *expr = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(expr->type == AST_CALL);
    CHECK(expr->as.call.arguments.count == 2);
    CHECK(expr->as.call.arguments.items[0]->type == AST_BINARY);
    CHECK(expr->as.call.arguments.items[1]->type == AST_INTEGER);
    ast_free(program);
}

static void test_parser_call_precedence(void) {
    Parser parser;
    AstNode *program = parse_program("add(1, 2) * 3\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *expr = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(expr->type == AST_BINARY);
    CHECK(expr->as.binary.operator_type == TOKEN_STAR);
    CHECK(expr->as.binary.left->type == AST_CALL);
    CHECK(expr->as.binary.left->as.call.arguments.count == 2);
    CHECK(expr->as.binary.right->type == AST_INTEGER);
    ast_free(program);
}

static void test_parser_nested_calls(void) {
    Parser parser;
    AstNode *program = parse_program("f(g(2))(3)\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *expr = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(expr->type == AST_CALL);
    CHECK(expr->as.call.arguments.count == 1);
    CHECK(expr->as.call.arguments.items[0]->type == AST_INTEGER);
    CHECK(expr->as.call.callee->type == AST_CALL);
    CHECK(expr->as.call.callee->as.call.arguments.count == 1);
    CHECK(expr->as.call.callee->as.call.arguments.items[0]->type == AST_CALL);
    ast_free(program);
}

static void test_parser_call_in_declaration(void) {
    Parser parser;
    AstNode *program = parse_program("let x = f(2)\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_LET);
    CHECK(stmt->as.declaration.value->type == AST_CALL);
    CHECK(stmt->as.declaration.value->as.call.arguments.count == 1);
    ast_free(program);
}

static void test_parser_call_missing_paren(void) {
    Parser parser;
    AstNode *program = parse_program("f(1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
}

static void test_parser_function_definition(void) {
    Parser parser;
    AstNode *program = parse_program(
        "fn add(a, b, c):\n"
        "    return a + b\n",
        &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_FUNCTION);
    CHECK(strcmp(stmt->as.function.name, "add") == 0);
    CHECK(stmt->as.function.parameters.count == 3);
    if (stmt->as.function.parameters.count == 3) {
        CHECK(strcmp(stmt->as.function.parameters.items[0]->as.identifier, "a") == 0);
        CHECK(strcmp(stmt->as.function.parameters.items[1]->as.identifier, "b") == 0);
        CHECK(strcmp(stmt->as.function.parameters.items[2]->as.identifier, "c") == 0);
    }
    CHECK(stmt->as.function.body->type == AST_BLOCK);
    CHECK(stmt->as.function.body->as.block.statements.count == 1);
    ast_free(program);
}

static void test_parser_function_no_parameters(void) {
    Parser parser;
    AstNode *program = parse_program(
        "fn hi():\n"
        "    return 1\n",
        &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_FUNCTION);
    CHECK(stmt->as.function.parameters.count == 0);
    ast_free(program);
}

static void test_parser_function_errors(void) {
    Parser parser;
    AstNode *program = parse_program("fn :\n    return 1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);

    program = parse_program("fn f\n    return 1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);

    program = parse_program("fn f(1):\n    return 1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);

    program = parse_program("fn f():\n    return 1\n", &parser);
    CHECK(program != NULL);
    ast_free(program);
}

static void test_parser_function_missing_block(void) {
    Parser parser;
    AstNode *program = parse_program("fn f():\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
}

static void test_parser_while_loop(void) {
    Parser parser;
    AstNode *program = parse_program("while x > 0:\n    y\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_WHILE);
    CHECK(stmt->as.while_statement.condition != NULL);
    CHECK(stmt->as.while_statement.condition->type == AST_BINARY);
    CHECK(stmt->as.while_statement.body != NULL);
    CHECK(stmt->as.while_statement.body->type == AST_BLOCK);
    CHECK(stmt->as.while_statement.body->as.block.statements.count == 1);
    ast_free(program);
}

static void test_parser_for_loop(void) {
    Parser parser;
    AstNode *program = parse_program(
        "for i in 1..10:\n"
        "    print(i)\n",
        &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_FOR);
    CHECK(strcmp(stmt->as.for_statement.variable, "i") == 0);
    CHECK(stmt->as.for_statement.low->type == AST_INTEGER);
    CHECK(stmt->as.for_statement.low->as.integer == 1);
    CHECK(stmt->as.for_statement.high->type == AST_INTEGER);
    CHECK(stmt->as.for_statement.high->as.integer == 10);
    CHECK(stmt->as.for_statement.body->type == AST_BLOCK);
    ast_free(program);
}

static void test_parser_for_loop_identifiers(void) {
    Parser parser;
    AstNode *program = parse_program(
        "for x in low..high:\n"
        "    x\n",
        &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_FOR);
    CHECK(strcmp(stmt->as.for_statement.variable, "x") == 0);
    CHECK(stmt->as.for_statement.low->type == AST_IDENTIFIER);
    CHECK(strcmp(stmt->as.for_statement.low->as.identifier, "low") == 0);
    CHECK(stmt->as.for_statement.high->type == AST_IDENTIFIER);
    CHECK(strcmp(stmt->as.for_statement.high->as.identifier, "high") == 0);
    ast_free(program);
}

static void test_parser_break_continue(void) {
    Parser parser;
    AstNode *program = parse_program(
        "while true:\n"
        "    break\n"
        "    continue\n",
        &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_WHILE);
    AstNode *body = stmt->as.while_statement.body;
    CHECK(body->as.block.statements.count == 2);
    CHECK(body->as.block.statements.items[0]->type == AST_BREAK);
    CHECK(body->as.block.statements.items[1]->type == AST_CONTINUE);
    ast_free(program);
}

static void test_parser_loop_errors(void) {
    Parser parser;
    AstNode *program = parse_program("while true:\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);

    program = parse_program("while x\n    y\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
    CHECK(strstr(parser.error_message, "expected ':' after condition") != NULL);

    program = parse_program("for :\n    1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
    CHECK(strstr(parser.error_message, "expected loop variable name") != NULL);

    program = parse_program("for i:\n    1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
    CHECK(strstr(parser.error_message, "expected 'in' after loop variable") != NULL);

    program = parse_program("for i in 0..\n    1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);

    program = parse_program("for i in 1..2\n    1\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
    CHECK(strstr(parser.error_message, "expected ':' after loop header") != NULL);
}

static void test_parser_list_literal(void) {
    Parser parser;
    AstNode *program = parse_program("[1, 2, 3]\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_EXPRESSION_STATEMENT);
    AstNode *list = stmt->as.expression_statement.expression;
    CHECK(list->type == AST_LIST);
    CHECK(list->as.list.elements.count == 3);
    CHECK(list->as.list.elements.items[0]->type == AST_INTEGER);
    CHECK(list->as.list.elements.items[0]->as.integer == 1);
    CHECK(list->as.list.elements.items[2]->as.integer == 3);
    ast_free(program);

    program = parse_program("[]\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    list = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(list->type == AST_LIST);
    CHECK(list->as.list.elements.count == 0);
    ast_free(program);

    /* Nested lists and a trailing comma are both accepted. */
    program = parse_program("[1, [2, 3], ]\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    list = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(list->type == AST_LIST);
    CHECK(list->as.list.elements.count == 2);
    CHECK(list->as.list.elements.items[1]->type == AST_LIST);
    CHECK(list->as.list.elements.items[1]->as.list.elements.count == 2);
    ast_free(program);

    program = parse_program("[1, 2\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
    CHECK(strstr(parser.error_message, "expected ']' after list elements") != NULL);
}

static void test_parser_index_expression(void) {
    Parser parser;
    AstNode *program = parse_program("xs[0]\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    AstNode *index = stmt->as.expression_statement.expression;
    CHECK(index->type == AST_INDEX);
    CHECK(index->as.index_expression.object->type == AST_IDENTIFIER);
    CHECK(index->as.index_expression.index->type == AST_INTEGER);
    CHECK(index->as.index_expression.index->as.integer == 0);
    ast_free(program);

    /* Indexing binds tighter than '+' and chains left-to-right. */
    program = parse_program("xs[1] + 2\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *binary = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(binary->type == AST_BINARY);
    CHECK(binary->as.binary.operator_type == TOKEN_PLUS);
    CHECK(binary->as.binary.left->type == AST_INDEX);
    CHECK(binary->as.binary.right->type == AST_INTEGER);
    ast_free(program);

    /* A call followed by an index parses as (call)[index]. */
    program = parse_program("f(x)[0]\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    index = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(index->type == AST_INDEX);
    CHECK(index->as.index_expression.object->type == AST_CALL);
    ast_free(program);

    /* Nested indexing chains. */
    program = parse_program("m[0][1]\n", &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    index = program->as.block.statements.items[0]->as.expression_statement.expression;
    CHECK(index->type == AST_INDEX);
    CHECK(index->as.index_expression.index->as.integer == 1);
    CHECK(index->as.index_expression.object->type == AST_INDEX);
    ast_free(program);

    program = parse_program("xs[0\n", &parser);
    CHECK(program == NULL);
    CHECK(parser.had_error);
    CHECK(strstr(parser.error_message, "expected ']' after index") != NULL);
}

static void test_parser_for_list_mode(void) {
    Parser parser;
    AstNode *program = parse_program(
        "for x in items:\n"
        "    x\n",
        &parser);
    CHECK(program != NULL);
    if (program == NULL) {
        return;
    }
    AstNode *stmt = program->as.block.statements.items[0];
    CHECK(stmt->type == AST_FOR);
    CHECK(strcmp(stmt->as.for_statement.variable, "x") == 0);
    CHECK(stmt->as.for_statement.low->type == AST_IDENTIFIER);
    CHECK(strcmp(stmt->as.for_statement.low->as.identifier, "items") == 0);
    CHECK(stmt->as.for_statement.high == NULL);
    CHECK(stmt->as.for_statement.body->type == AST_BLOCK);
    ast_free(program);
}

/*
 * Evaluator tests
 */

static void test_eval_let_declaration(void) {
    check_int_result("let x = 42\nx\n", 42);
}

static void test_eval_mut_declaration(void) {
    check_int_result("mut x = 7\nx + 1\n", 8);
}

static void test_eval_successful_reassignment(void) {
    check_int_result("mut score = 1\nscore = score + 2\nscore\n", 3);
}

static void test_eval_arithmetic_after_reassignment(void) {
    check_int_result("mut x = 1\nx = x + 2\nx = x * 3\nx\n", 9);
}

static void test_eval_immutable_reassignment_fails(void) {
    RunResult run = run_source("let fixed = 1\nfixed = 2\nfixed\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    /* The environment-level operation must also be rejected directly. */
    Environment *env = env_new();
    Value one = value_int(1);
    Value two = value_int(2);
    CHECK(env_define(env, "fixed", &one, false));
    CHECK(env_assign(env, "fixed", &two) == false);
    CHECK(env_error(env) != NULL);
    Value *lookup = env_lookup(env, "fixed");
    CHECK(lookup != NULL && lookup->type == VALUE_INT && lookup->as.integer == 1);
    env_free(env);
    value_free(&one);
    value_free(&two);
}

static void test_eval_undefined_assignment_fails(void) {
    RunResult run = run_source("missing = 42\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    /* No implicit binding may be created by the failed assignment. */
    run = run_source("missing\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);
}

static void test_env_undefined_assign_no_implicit(void) {
    Environment *env = env_new();
    Value forty_two = value_int(42);
    CHECK(env_assign(env, "missing", &forty_two) == false);
    CHECK(env_lookup(env, "missing") == NULL);
    CHECK(env_error(env) != NULL);
    env_free(env);
    value_free(&forty_two);
}

static void test_eval_repeated_string_reassignment(void) {
    check_string_result("mut value = \"a\"\nvalue = \"b\"\nvalue = \"c\"\nvalue\n",
                        "c");
}

static void test_eval_string_reassignment(void) {
    check_string_result("mut name = \"Alex\"\nname = \"Taylor\"\nname\n",
                        "Taylor");
}

static void test_eval_string_comparison(void) {
    RunResult run = run_source("let a = \"Alex\"\nlet b = \"Alex\"\na == b\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_BOOL);
    CHECK(run.result.as.boolean == true);
    value_free(&run.result);
}

static void test_eval_value_comparisons(void) {
    check_bool_result("1 == 1\n", true);
    check_bool_result("1 != 2\n", true);
    check_bool_result("2 == 1\n", false);
    check_bool_result("1.5 == 1.5\n", true);
    check_bool_result("true == true\n", true);
    check_bool_result("null == null\n", true);
    check_bool_result("3 != 3\n", false);
    check_bool_result("1 == 2\n", false);
}

static void test_eval_string_concatenation(void) {
    RunResult run = run_source("let a = \"he\"\nlet b = \"llo\"\na + b\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_STRING);
    CHECK(strcmp(run.result.as.string, "hello") == 0);
    value_free(&run.result);
}

static void test_eval_string_ownership(void) {
    /* Two bindings must not share a mutable string buffer. */
    check_string_result("mut a = \"x\"\nlet b = a\na = \"y\"\nb\n", "x");
}

static void test_eval_if_branches(void) {
    check_int_result("if true:\n    1\nelse:\n    2\n", 1);
    check_int_result("if false:\n    1\nelse:\n    2\n", 2);
}

static void test_eval_if_elif(void) {
    check_int_result(
        "if 1 > 2:\n"
        "    0\n"
        "elif 2 > 1:\n"
        "    1\n"
        "else:\n"
        "    2\n",
        1);
    check_int_result(
        "if 1 > 2:\n"
        "    0\n"
        "elif 1 > 2:\n"
        "    1\n"
        "else:\n"
        "    2\n",
        2);
    /* First true branch wins; later branches are not reached. */
    check_int_result(
        "if true:\n"
        "    1\n"
        "elif true:\n"
        "    2\n"
        "else:\n"
        "    3\n",
        1);
}

static void test_eval_if_elif_short_circuit(void) {
    /* An elif condition whose evaluation would fail must not run when an
     * earlier branch was already taken. */
    check_int_result(
        "if true:\n"
        "    1\n"
        "elif undefined_name:\n"
        "    2\n"
        "else:\n"
        "    3\n",
        1);
    check_int_result(
        "if false:\n"
        "    1\n"
        "elif true:\n"
        "    2\n"
        "elif undefined_name:\n"
        "    3\n"
        "else:\n"
        "    4\n",
        2);
}

static void test_eval_if_elif_no_branch_taken(void) {
    RunResult run = run_source(
        "if false:\n"
        "    1\n"
        "elif false:\n"
        "    2\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_if_elif_in_function(void) {
    check_int_result(
        "fn classify(n):\n"
        "    if n < 0:\n"
        "        return -1\n"
        "    elif n == 0:\n"
        "        return 0\n"
        "    else:\n"
        "        return 1\n"
        "classify(-5)\n",
        -1);
    check_int_result(
        "fn classify(n):\n"
        "    if n < 0:\n"
        "        return -1\n"
        "    elif n == 0:\n"
        "        return 0\n"
        "    else:\n"
        "        return 1\n"
        "classify(0)\n",
        0);
    check_int_result(
        "fn classify(n):\n"
        "    if n < 0:\n"
        "        return -1\n"
        "    elif n == 0:\n"
        "        return 0\n"
        "    else:\n"
        "        return 1\n"
        "classify(42)\n",
        1);
}

static void test_eval_while_loop(void) {
    check_int_result(
        "mut n = 3\n"
        "mut out = 0\n"
        "while n > 0:\n"
        "    out = out + 100 + n\n"
        "    n = n - 1\n"
        "out\n",
        306);
    check_int_result("while false:\n    1\n0\n", 0);
}

static void test_eval_for_loop(void) {
    check_int_result(
        "mut total = 0\n"
        "for i in 1..6:\n"
        "    total = total + i\n"
        "total\n",
        15);
    check_int_result(
        "mut out = 0\n"
        "for i in 5..1:\n"
        "    out = out * 10 + i\n"
        "out\n",
        5432);
    check_int_result("for i in 1..1:\n    1\n0\n", 0);
}

static void test_eval_loop_break(void) {
    check_int_result(
        "mut total = 0\n"
        "mut i = 0\n"
        "while true:\n"
        "    i = i + 1\n"
        "    if i > 3:\n"
        "        break\n"
        "    total = total + i\n"
        "total\n",
        6);
    check_int_result(
        "mut total = 0\n"
        "for i in 1..100:\n"
        "    if i == 5:\n"
        "        break\n"
        "    total = total + i\n"
        "total\n",
        10);
}

static void test_eval_loop_continue(void) {
    check_int_result(
        "mut out = 0\n"
        "mut i = 0\n"
        "while i < 6:\n"
        "    i = i + 1\n"
        "    if i % 2 == 0:\n"
        "        continue\n"
        "    out = out + i\n"
        "out\n",
        9);
    check_int_result(
        "mut out = 0\n"
        "for i in 1..6:\n"
        "    if i % 2 == 0:\n"
        "        continue\n"
        "    out = out + i\n"
        "out\n",
        9);
}

static void test_eval_nested_loops(void) {
    check_int_result(
        "mut inner_total = 0\n"
        "for a in 1..4:\n"
        "    for b in 1..4:\n"
        "        inner_total = inner_total + 1\n"
        "inner_total\n",
        9);
    check_int_result(
        "mut count = 0\n"
        "for a in 1..3:\n"
        "    for b in 1..10:\n"
        "        if b == 2:\n"
        "            break\n"
        "        count = count + 1\n"
        "count\n",
        2);
}

static void test_eval_loop_return_in_function(void) {
    check_int_result(
        "fn find(t):\n"
        "    for i in 1..10:\n"
        "        if i * i == t:\n"
        "            return i\n"
        "    return -1\n"
        "find(49)\n",
        7);
    check_int_result(
        "fn find(t):\n"
        "    for i in 1..10:\n"
        "        if i * i == t:\n"
        "            return i\n"
        "    return -1\n"
        "find(64)\n",
        8);
    check_int_result(
        "fn find(t):\n"
        "    for i in 1..10:\n"
        "        if i * i == t:\n"
        "            return i\n"
        "    return -1\n"
        "find(120)\n",
        -1);
}

static void test_eval_loop_variable_binding(void) {
    check_int_result("for i in 1..4:\n    0\ni\n", 3);
    check_int_result(
        "mut iterations = 0\n"
        "for i in 1..4:\n"
        "    i = i + 100\n"
        "    iterations = iterations + 1\n"
        "iterations\n",
        3);
}

static void test_eval_loop_errors(void) {
    RunResult run = run_source("break\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strstr(run.error, "break outside a loop") != NULL);
    value_free(&run.result);

    run = run_source("continue\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strstr(run.error, "continue outside a loop") != NULL);
    value_free(&run.result);

    run = run_source("fn f():\n    break\nf()\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strstr(run.error, "break outside a loop") != NULL);
    value_free(&run.result);

    run = run_source("for x in 1.5..3:\n    1\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strstr(run.error, "for range requires two integers") != NULL);
    value_free(&run.result);

    run = run_source("for x in \"a\"..\"b\":\n    1\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strstr(run.error, "for range requires two integers") != NULL);
    value_free(&run.result);
}

static void test_eval_list_literal(void) {
    RunResult run = run_source("[1, 2, 3]\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_LIST);
    CHECK(run.result.as.list.count == 3);
    CHECK(run.result.as.list.items[0].as.integer == 1);
    CHECK(run.result.as.list.items[1].as.integer == 2);
    CHECK(run.result.as.list.items[2].as.integer == 3);
    value_free(&run.result);

    run = run_source("[]\n");
    CHECK(run.parsed == 1);
    CHECK(run.result.type == VALUE_LIST);
    CHECK(run.result.as.list.count == 0);
    value_free(&run.result);

    run = run_source("[1, \"two\", true, null]\n");
    CHECK(run.result.type == VALUE_LIST);
    CHECK(run.result.as.list.count == 4);
    CHECK(run.result.as.list.items[0].type == VALUE_INT);
    CHECK(run.result.as.list.items[1].type == VALUE_STRING);
    CHECK(strcmp(run.result.as.list.items[1].as.string, "two") == 0);
    CHECK(run.result.as.list.items[2].type == VALUE_BOOL);
    CHECK(run.result.as.list.items[3].type == VALUE_NULL);
    value_free(&run.result);

    /* Nested literals evaluate depth-first into independent lists. */
    run = run_source("[[1, 2], [3]]\n");
    CHECK(run.result.type == VALUE_LIST);
    CHECK(run.result.as.list.count == 2);
    CHECK(run.result.as.list.items[0].type == VALUE_LIST);
    CHECK(run.result.as.list.items[0].as.list.count == 2);
    CHECK(run.result.as.list.items[1].as.list.items[0].as.integer == 3);
    value_free(&run.result);
}

static void test_eval_list_index(void) {
    check_int_result("[10, 20, 30][1]\n", 20);
    check_int_result("let xs = [4, 5, 6]\nxs[0]\n", 4);
    check_int_result("let xs = [4, 5, 6]\nxs[1] + xs[2]\n", 11);
    check_int_result("let m = [[1, 2], [3, 4]]\nm[1][0]\n", 3);
    check_int_result("let m = [[1, 2], [3, 4]]\nm[0][1] + m[1][1]\n", 6);
    check_stdlib_string("let xs = [\"a\", \"b\"]\nxs[1]\n", "b");
}

static void test_eval_string_index(void) {
    check_stdlib_string("\"hello\"[0]\n", "h");
    check_stdlib_string("\"hello\"[4]\n", "o");
    check_stdlib_int("len(\"abc\"[1])\n", 1);
}

static void test_eval_index_errors(void) {
    check_stdlib_error("[1, 2][2]\n");
    check_stdlib_error("[1, 2][-1]\n");
    check_stdlib_error("[1, 2][1.5]\n");
    check_stdlib_error("[1, 2][\"x\"]\n");
    check_stdlib_error("42[0]\n");
    check_stdlib_error("null[0]\n");
    check_stdlib_error("\"hi\"[5]\n");
    check_stdlib_error("\"hi\"[-1]\n");
}

static void test_eval_for_in_list(void) {
    check_int_result(
        "mut total = 0\n"
        "for x in [1, 2, 3, 4]:\n"
        "    total = total + x\n"
        "total\n",
        10);
    check_int_result(
        "mut total = 0\n"
        "for x in [1, 2, 3, 4]:\n"
        "    if x == 3:\n"
        "        break\n"
        "    total = total + x\n"
        "total\n",
        3);
    check_int_result(
        "mut total = 0\n"
        "for x in [1, 2, 3, 4]:\n"
        "    if x % 2 == 0:\n"
        "        continue\n"
        "    total = total + x\n"
        "total\n",
        4);
    check_int_result("for x in [7, 8, 9]:\n    0\nx\n", 9);
    check_int_result("mut n = 0\nfor x in []:\n    n = n + 1\nn\n", 0);
    check_int_result(
        "mut total = 0\n"
        "for row in [[1, 2], [3, 4]]:\n"
        "    for cell in row:\n"
        "        total = total + cell\n"
        "total\n",
        10);
    check_stdlib_error("for x in 5:\n    1\n");
    check_stdlib_error("for x in \"ab\":\n    1\n");
}

static void test_eval_list_equality(void) {
    RunResult run = run_source("[1, 2] == [1, 2]\n");
    CHECK(run.result.type == VALUE_BOOL);
    CHECK(run.result.as.boolean == true);
    value_free(&run.result);

    run = run_source("[1, 2] == [1, 3]\n");
    CHECK(run.result.type == VALUE_BOOL);
    CHECK(run.result.as.boolean == false);
    value_free(&run.result);

    run = run_source("[1, 2] != [1, 2]\n");
    CHECK(run.result.type == VALUE_BOOL);
    CHECK(run.result.as.boolean == false);
    value_free(&run.result);

    run = run_source("[[1], [2]] == [[1], [2]]\n");
    CHECK(run.result.type == VALUE_BOOL);
    CHECK(run.result.as.boolean == true);
    value_free(&run.result);

    run = run_source("[1, 2] == [1, 2, 3]\n");
    CHECK(run.result.as.boolean == false);
    value_free(&run.result);

    /* Different element types are never equal. */
    run = run_source("[1] == 1\n");
    CHECK(run.result.as.boolean == false);
    value_free(&run.result);
}

static void test_eval_list_truthiness(void) {
    check_int_result("mut r = 0\nif []:\n    r = 1\nelse:\n    r = 2\nr\n", 2);
    check_int_result("mut r = 0\nif [0]:\n    r = 1\nelse:\n    r = 2\nr\n", 1);
    check_int_result("mut r = 0\nif [null]:\n    r = 1\nelse:\n    r = 2\nr\n", 1);
}

static void test_eval_integer_operations(void) {
    check_int_result("1 + 2 * 3\n", 7);
    check_int_result("10 - 4\n", 6);
    check_int_result("7 % 3\n", 1);
    check_int_result("-5\n", -5);
    check_int_result("2 * 3 + 4 * 5\n", 26);
}

static void test_eval_division_by_zero_fails(void) {
    RunResult run = run_source("1 / 0\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_undefined_lookup_fails(void) {
    RunResult run = run_source("nope\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_call_native(void) {
    RunResult run = run_source_with_natives("add(2, 3)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_INT);
    CHECK(run.result.as.integer == 5);
    value_free(&run.result);
}

static void test_eval_call_nested(void) {
    RunResult run = run_source_with_natives("add(add(1, 2), 3)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_INT);
    CHECK(run.result.as.integer == 6);
    value_free(&run.result);
}

static void test_eval_call_in_expression(void) {
    RunResult run = run_source_with_natives("add(2, 3) + 1\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_INT);
    CHECK(run.result.as.integer == 6);
    value_free(&run.result);
}

static void test_eval_call_in_declaration(void) {
    RunResult run = run_source_with_natives("let x = double(3)\nx\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_INT);
    CHECK(run.result.as.integer == 6);
    value_free(&run.result);
}

static void test_eval_call_value_holding_native(void) {
    RunResult run = run_source_with_natives("let f = add\nf(10, 1)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_INT);
    CHECK(run.result.as.integer == 11);
    value_free(&run.result);
}

static void test_eval_call_empty_arguments(void) {
    RunResult run = run_source_with_natives("add()\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_call_string_argument(void) {
    RunResult run = run_source_with_natives("name(\"Alex\")\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_STRING);
    CHECK(strcmp(run.result.as.string, "Alex") == 0);
    value_free(&run.result);
}

static void test_eval_call_undefined_callee(void) {
    RunResult run = run_source_with_natives("missing(1)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_call_non_function(void) {
    RunResult run = run_source_with_natives("let x = 5\nx(1)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_call_argument_error_stops(void) {
    RunResult run = run_source_with_natives("add(1, nope)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_native_error_reports(void) {
    RunResult run = run_source_with_natives("boom()\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strcmp(run.error, "boom") == 0);
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    run = run_source_with_natives("boom() + 1\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strcmp(run.error, "boom") == 0);
    value_free(&run.result);
}

/*
 * Standard library helpers and tests. These run with fem_stdlib_register so
 * the builtin natives can be exercised through a whole program.
 */

static void check_stdlib_int(const char *source, int64_t expected) {
    RunResult run = run_source_with_stdlib(source);
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_INT);
    CHECK(run.result.as.integer == expected);
    value_free(&run.result);
}

static void check_stdlib_float(const char *source, double expected) {
    RunResult run = run_source_with_stdlib(source);
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_FLOAT);
    if (run.result.type == VALUE_FLOAT) {
        double delta = run.result.as.floating - expected;
        if (delta < 0.0) {
            delta = -delta;
        }
        CHECK(delta < 1e-9);
    }
    value_free(&run.result);
}

static void check_stdlib_string(const char *source, const char *expected) {
    RunResult run = run_source_with_stdlib(source);
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_STRING);
    CHECK(expected != NULL && strcmp(run.result.as.string, expected) == 0);
    value_free(&run.result);
}

static void check_stdlib_error(const char *source) {
    RunResult run = run_source_with_stdlib(source);
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void check_stdlib_null(const char *source) {
    RunResult run = run_source_with_stdlib(source);
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_stdlib_len(void) {
    check_stdlib_int("len(\"hello\")\n", 5);
    check_stdlib_int("len(\"\")\n", 0);
    check_stdlib_int("let s = \"abc\"\nlen(s)\n", 3);
    check_stdlib_int("len(str(12345))\n", 5);
    check_stdlib_int("len([1, 2, 3])\n", 3);
    check_stdlib_int("len([])\n", 0);
    check_stdlib_int("len([[1, 2], 3])\n", 2);
    check_stdlib_error("len(42)\n");
    check_stdlib_error("len(print)\n");
    check_stdlib_error("len()\n");
    check_stdlib_error("len(\"a\", \"b\")\n");
}

static void test_stdlib_int_conversions(void) {
    check_stdlib_int("int(3.9)\n", 3);
    check_stdlib_int("int(-3.9)\n", -3);
    check_stdlib_int("int(5.0)\n", 5);
    check_stdlib_int("int(-2.0)\n", -2);
    check_stdlib_int("int(true)\n", 1);
    check_stdlib_int("int(false)\n", 0);
    check_stdlib_int("int(7)\n", 7);
    check_stdlib_error("int(\"3\")\n");
    check_stdlib_error("int(null)\n");
    check_stdlib_error("int(1, 2)\n");
    check_stdlib_error("int()\n");
}

static void test_stdlib_float_conversions(void) {
    check_stdlib_float("float(3)\n", 3.0);
    check_stdlib_float("float(false)\n", 0.0);
    check_stdlib_float("float(true)\n", 1.0);
    check_stdlib_float("float(2.5)\n", 2.5);
    check_stdlib_error("float(\"x\")\n");
    check_stdlib_error("float(null)\n");
    check_stdlib_error("float(1, 2)\n");
}

static void test_stdlib_str(void) {
    check_stdlib_string("str(42)\n", "42");
    check_stdlib_string("str(-7)\n", "-7");
    check_stdlib_string("str(3.5)\n", "3.5");
    check_stdlib_string("str(true)\n", "true");
    check_stdlib_string("str(false)\n", "false");
    check_stdlib_string("str(null)\n", "null");
    check_stdlib_string("str(\"x\")\n", "x");
    check_stdlib_string("str(1 + 2)\n", "3");
}

static void test_eval_function_basic(void) {
    check_int_result(
        "fn add(a, b):\n"
        "    return a + b\n"
        "add(2, 3)\n",
        5);
    check_int_result(
        "fn add(a, b):\n"
        "    return a + b\n"
        "add(20, 22)\n",
        42);
}

static void test_eval_function_local_scope(void) {
    /* Parameters and locals must be isolated from the enclosing scope. */
    check_int_result(
        "let x = 100\n"
        "fn f(x):\n"
        "    let y = 7\n"
        "    return x + y\n"
        "f(1)\n",
        8);
    /* A local declaration shadows an outer one within the function body. */
    check_int_result(
        "let x = 100\n"
        "fn f(x):\n"
        "    let x = 5\n"
        "    return x\n"
        "f(1)\n",
        5);
    /* The outer binding is untouched by anything a function does. */
    check_int_result(
        "let x = 100\n"
        "fn f(x):\n"
        "    let x = 3\n"
        "    return x\n"
        "f(99)\n"
        "x\n",
        100);
}

static void test_eval_function_no_return(void) {
    /* A function that never returns produces null when called. */
    RunResult run = run_source(
        "fn f():\n"
        "    1 + 2\n"
        "f()\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    /* A bare `return` yields null too. */
    check_int_result(
        "fn f():\n"
        "    return\n"
        "let r = f()\n"
        "if r == null:\n"
        "    1\n"
        "else:\n"
        "    2\n",
        1);
}

static void test_eval_function_return_in_if(void) {
    check_int_result(
        "fn sign(n):\n"
        "    if n < 0:\n"
        "        return -1\n"
        "    else:\n"
        "        return 1\n"
        "sign(-5)\n",
        -1);
    check_int_result(
        "fn sign(n):\n"
        "    if n < 0:\n"
        "        return -1\n"
        "    else:\n"
        "        return 1\n"
        "sign(9)\n",
        1);
    /* Return alone (no else) leaves a null when the branch is skipped. */
    RunResult run = run_source(
        "fn f(n):\n"
        "    if n < 0:\n"
        "        return -1\n"
        "f(5)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_function_call_in_expression(void) {
    check_int_result(
        "fn f():\n"
        "    return 3\n"
        "f() * 2\n",
        6);
    check_int_result(
        "fn f(x):\n"
        "    return x + 1\n"
        "f(f(1))\n",
        3);
    check_int_result(
        "fn f():\n"
        "    return 2\n"
        "let a = f() + f()\n"
        "a + f()\n",
        6);
}

static void test_eval_function_calls_native(void) {
    RunResult run = run_source_with_natives(
        "fn f():\n"
        "    return add(2, 3)\n"
        "f()\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] == '\0');
    CHECK(run.result.type == VALUE_INT);
    CHECK(run.result.as.integer == 5);
    value_free(&run.result);

    /* A native handled its own error: message must surface. */
    run = run_source_with_natives(
        "fn f():\n"
        "    return boom()\n"
        "f()\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strcmp(run.error, "boom") == 0);
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_function_recursion(void) {
    check_int_result(
        "fn fib(n):\n"
        "    if n < 2:\n"
        "        return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "fib(1)\n",
        1);
    check_int_result(
        "fn fib(n):\n"
        "    if n < 2:\n"
        "        return n\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "fib(10)\n",
        55);
    check_int_result(
        "fn count_up(n):\n"
        "    if n == 0:\n"
        "        return 0\n"
        "    return count_up(n - 1) + 1\n"
        "count_up(200)\n",
        200);
    /* Mutual recursion between two functions. */
    check_int_result(
        "fn even(n):\n"
        "    if n == 0:\n"
        "        return true\n"
        "    return odd(n - 1)\n"
        "fn odd(n):\n"
        "    if n == 0:\n"
        "        return false\n"
        "    return even(n - 1)\n"
        "let a = even(10)\n"
        "let b = odd(7)\n"
        "if a == b:\n"
        "    1\n"
        "else:\n"
        "    0\n",
        1);
}

static void test_eval_function_recursion_guard(void) {
    RunResult run = run_source(
        "fn forever():\n"
        "    return forever()\n"
        "forever()\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strcmp(run.error, "recursion limit exceeded") == 0);
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    /* A guarded runaway call must not leave the program error-stuck. */
    run = run_source(
        "fn forever():\n"
        "    return forever()\n"
        "let x = forever()\n"
        "let x = 1\n"
        "x\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);
}

static void test_eval_function_arity_error(void) {
    RunResult run = run_source(
        "fn take_two(a, b):\n"
        "    return a + b\n"
        "take_two(1)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strcmp(run.error,
                  "function 'take_two' expects 2 arguments, got 1") == 0);
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    run = run_source(
        "fn take_two(a, b):\n"
        "    return a + b\n"
        "take_two(1, 2, 3)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);
}

static void test_eval_return_outside_function(void) {
    RunResult run = run_source("return 1\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(strcmp(run.error, "return outside a function") == 0);
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    /* Return inside an if at the top level must still be rejected. */
    run = run_source("if true:\n    return 1\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);
}

static void test_eval_function_rebinding_error(void) {
    /* Function bindings are immutable. */
    RunResult run = run_source(
        "fn f():\n"
        "    return 1\n"
        "f = 2\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);

    /* Parameters are immutable too. */
    run = run_source(
        "fn f(x):\n"
        "    x = 2\n"
        "    return x\n"
        "f(1)\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);
}

static void test_eval_function_closure_capture(void) {
    /* A function reads a variable from the enclosing scope at call time. */
    check_int_result(
        "let base = 10\n"
        "fn scale(x):\n"
        "    return base * x\n"
        "scale(3)\n",
        30);
    /* Re-declaring `base` rebinds the shared slot (capture by variable, like
     * Python), so the closure observes the new value. */
    check_int_result(
        "let base = 10\n"
        "fn scale(x):\n"
        "    return base * x\n"
        "let base = 2\n"
        "scale(3)\n",
        6);
}

static void test_eval_function_closure_mutation(void) {
    /* The closure shares the enclosing frame, so mutations persist. */
    check_int_result(
        "mut total = 0\n"
        "fn bump():\n"
        "    total = total + 1\n"
        "    return total\n"
        "bump()\n"
        "bump()\n"
        "bump()\n",
        3);
}

static void test_eval_function_escaping_closure(void) {
    /* A closure surviving its outer call keeps its captured variables. */
    check_int_result(
        "fn make():\n"
        "    let x = 5\n"
        "    fn get():\n"
        "        return x\n"
        "    return get\n"
        "let g = make()\n"
        "g()\n",
        5);
    check_int_result(
        "fn make():\n"
        "    let x = 5\n"
        "    fn get():\n"
        "        return x\n"
        "    return get\n"
        "let g = make()\n"
        "let h = make()\n"
        "g() + h()\n",
        10);
}

static void test_eval_function_making_counter(void) {
    /* Each escaping closure owns its own captured mutable state. */
    check_int_result(
        "fn make_counter(start):\n"
        "    mut count = start\n"
        "    fn step():\n"
        "        count = count + 1\n"
        "        return count\n"
        "    return step\n"
        "let a = make_counter(10)\n"
        "let b = make_counter(100)\n"
        "let a1 = a()\n"
        "let a2 = a()\n"
        "let b1 = b()\n"
        "let b2 = b()\n"
        "let pair = a1 * 1000 + a2 * 100 + b1 * 10 + b2\n"
        "pair\n",
        13312);
}

static void test_eval_function_higher_order(void) {
    check_int_result(
        "fn apply_twice(f, x):\n"
        "    return f(f(x))\n"
        "fn double_value(x):\n"
        "    return x * 2\n"
        "apply_twice(double_value, 5)\n",
        20);
    check_int_result(
        "fn make_adder(n):\n"
        "    fn add(x):\n"
        "        return x + n\n"
        "    return add\n"
        "let add5 = make_adder(5)\n"
        "let add10 = make_adder(10)\n"
        "add5(add10(1))\n",
        16);
    check_int_result(
        "fn compose(f, g):\n"
        "    fn h(x):\n"
        "        return f(g(x))\n"
        "    return h\n"
        "fn inc(x):\n"
        "    return x + 1\n"
        "fn double_value(x):\n"
        "    return x * 2\n"
        "let f = compose(inc, double_value)\n"
        "f(4)\n",
        9);
}

static void test_eval_function_shadowing_via_let(void) {
    /* Re-declaring the same function name is a fresh immutable binding. */
    check_int_result(
        "fn f():\n"
        "    return 1\n"
        "fn f(x):\n"
        "    return x\n"
        "f(5)\n",
        5);
}

static void test_eval_function_value_flow(void) {
    /* Functions flow through variables, calls, and native arguments. */
    check_int_result(
        "fn f():\n"
        "    return 7\n"
        "let g = f\n"
        "g()\n",
        7);
    check_int_result(
        "fn f():\n"
        "    return 7\n"
        "let pick = f\n"
        "let g = pick\n"
        "g()\n",
        7);
    check_int_result(
        "fn add(a, b):\n"
        "    return a + b\n"
        "add(add(1, 2), add(3, 4))\n",
        10);
}

static void test_stdlib_type(void) {
    check_stdlib_string("type(1)\n", "int");
    check_stdlib_string("type(1.5)\n", "float");
    check_stdlib_string("type(true)\n", "bool");
    check_stdlib_string("type(\"s\")\n", "string");
    check_stdlib_string("type([1, 2])\n", "list");
    check_stdlib_string("type([])\n", "list");
    check_stdlib_string("type(null)\n", "null");
    check_stdlib_string("type(print)\n", "native");
    check_stdlib_string("fn double_value(x):\n    return x * 2\ntype(double_value)\n", "fn");
    check_stdlib_string("fn double_value(x):\n    return x * 2\nstr(double_value)\n", "<fn 'double_value'>");
    check_stdlib_null("fn double_value(x):\n    return x * 2\nprint(double_value)\n");
    check_stdlib_error("type()\n");
    check_stdlib_error("type(1, 2)\n");
}

/*
 * Value ownership tests for user-defined functions: value_function()
 * deep-copies the name and parameter strings, borrows the body, and retains
 * the closure. Clones duplicate name and parameters (independent storage)
 * while sharing the body and re-retaining the closure.
 */

static void test_value_function_ownership(void) {
    Environment *env = env_new();
    CHECK(env != NULL);
    if (env == NULL) {
        return;
    }
    AstNode *body = ast_new(AST_INTEGER, 1, 1);
    CHECK(body != NULL);
    if (body == NULL) {
        env_free(env);
        return;
    }
    body->as.integer = 7;

    char *parameters[3] = {"alpha", "beta", "gamma"};
    Value first = value_function("compute", parameters, 3, body, env);
    CHECK(first.type == VALUE_FN);
    if (first.type == VALUE_FN) {
        CHECK(strcmp(first.as.fn->name, "compute") == 0);
        CHECK(first.as.fn->parameter_count == 3);
        CHECK(strcmp(first.as.fn->parameters[2], "gamma") == 0);
        CHECK(first.as.fn->parameters[0] != parameters[0]);
        CHECK(first.as.fn->body == body);
        CHECK(first.as.fn->closure == env);

        Value clone = value_clone(&first);
        CHECK(clone.type == VALUE_FN);
        if (clone.type == VALUE_FN) {
            CHECK(clone.as.fn != first.as.fn);
            CHECK(strcmp(clone.as.fn->name, "compute") == 0);
            CHECK(clone.as.fn->parameter_count == 3);
            CHECK(clone.as.fn->parameters[0] != first.as.fn->parameters[0]);
            CHECK(strcmp(clone.as.fn->parameters[2], "gamma") == 0);
            CHECK(clone.as.fn->body == body);
            CHECK(clone.as.fn->closure == first.as.fn->closure);
            value_free(&clone);
        }

        /* Freeing the original leaves the clone's storage untouched. */
        Value string_from_fn = value_to_string(&first);
        CHECK(string_from_fn.type == VALUE_STRING);
        CHECK(strcmp(string_from_fn.as.string, "<fn 'compute'>") == 0);
        value_free(&string_from_fn);

        value_free(&first);
    }

    /* Invalid inputs fall back to null with no allocation. */
    Value invalid = value_function(NULL, parameters, 1, body, env);
    CHECK(invalid.type == VALUE_NULL);
    invalid = value_function("f", parameters, 1, body, NULL);
    CHECK(invalid.type == VALUE_NULL);

    env_free(env);
    ast_free(body);
}

static void test_stdlib_print_ok(void) {
    check_stdlib_null("print(1)\n");
    check_stdlib_null("print(\"a\", \"b\", 3)\n");
    check_stdlib_null("print()\n");
    check_stdlib_null("print(len(\"abc\"))\n");
}

static void test_eval_ordered_comparisons(void) {
    check_bool_result("1 < 2\n", true);
    check_bool_result("2 < 2\n", false);
    check_bool_result("2 <= 2\n", true);
    check_bool_result("3 > 2\n", true);
    check_bool_result("3 >= 4\n", false);
    check_bool_result("3 >= 3\n", true);
    check_bool_result("1.5 < 2.5\n", true);
    check_bool_result("2.0 >= 2.0\n", true);
    check_bool_result("2 * 2 < 5\n", true);
    check_bool_result("1 == 1.0\n", false);
}

static void test_eval_comparison_type_errors(void) {
    RunResult run = run_source("\"a\" < \"b\"\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    run = run_source("1 < \"a\"\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);

    run = run_source("true > false\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);
}

static void test_eval_comparison_in_condition(void) {
    check_int_result("if 1 < 2:\n    1\nelse:\n    2\n", 1);
    check_int_result("if 3 >= 4:\n    1\nelse:\n    0\n", 0);
}

static void test_eval_float_remainder(void) {
    check_float_result("7.5 % 2.0\n", 1.5);
    check_float_result("-7.5 % 2.0\n", -1.5);
    check_float_result("100.0 % 30.0\n", 10.0);
    check_float_result("5.0 % 2.5\n", 0.0);
}

static void test_eval_float_division_by_zero(void) {
    RunResult run = run_source("1.0 / 0.0\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    run = run_source("1.5 % 0.0\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);
}

static void test_eval_mixed_type_errors(void) {
    RunResult run = run_source("1 + \"a\"\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    CHECK(run.result.type == VALUE_NULL);
    value_free(&run.result);

    run = run_source("\"a\" - 1\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);

    run = run_source("true + 1\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);

    run = run_source("1 + 1.5\n");
    CHECK(run.parsed == 1);
    CHECK(run.error[0] != '\0');
    value_free(&run.result);
}

/*
 * Native registry tests
 */

static void test_native_register_and_lookup(void) {
    FemNativeRegistry registry;
    native_registry_init(&registry);

    CHECK(native_register(&registry, "add", native_add));
    CHECK(native_lookup(&registry, "add") == native_add);
    CHECK(native_lookup(&registry, "missing") == NULL);
    CHECK(native_lookup(&registry, NULL) == NULL);
    CHECK(native_lookup(NULL, "add") == NULL);

    /* Null names and null function pointers are rejected. */
    CHECK(native_register(&registry, NULL, native_add) == false);
    CHECK(native_register(&registry, "nope", NULL) == false);
    CHECK(native_register(NULL, "add", native_add) == false);

    native_registry_free(&registry);
}

static void test_native_replace_existing_name(void) {
    FemNativeRegistry registry;
    native_registry_init(&registry);

    CHECK(native_register(&registry, "add", native_add));
    CHECK(native_register(&registry, "add", native_double));
    CHECK(registry.count == 1);
    CHECK(native_lookup(&registry, "add") == native_double);

    native_registry_free(&registry);
}

static void test_native_registry_growth(void) {
    FemNativeRegistry registry;
    native_registry_init(&registry);

    for (int i = 0; i < 20; i++) {
        char name[32];
        snprintf(name, sizeof(name), "fn%d", i);
        CHECK(native_register(&registry, name, native_add));
    }
    CHECK(registry.count == 20);
    for (int i = 0; i < 20; i++) {
        char name[32];
        snprintf(name, sizeof(name), "fn%d", i);
        CHECK(native_lookup(&registry, name) == native_add);
    }

    native_registry_free(&registry);
}

static void test_native_callback_invocation(void) {
    FemNativeRegistry registry;
    native_registry_init(&registry);
    CHECK(native_register(&registry, "add", native_add));

    FemNativeFunction add = native_lookup(&registry, "add");
    CHECK(add != NULL);

    Value arguments[] = {value_int(2), value_int(3)};
    CHECK(add != NULL);
    if (add != NULL) {
        Value result = add(2, arguments);
        CHECK(result.type == VALUE_INT);
        CHECK(result.as.integer == 5);
        value_free(&result);
    }

    native_registry_free(&registry);
}

static void test_native_cleanup_idempotent(void) {
    FemNativeRegistry registry;
    native_registry_init(&registry);
    CHECK(native_register(&registry, "add", native_add));
    native_registry_free(&registry);
    native_registry_free(&registry);
    CHECK(registry.count == 0);
}

/*
 * Value ownership tests
 */

static void test_value_ownership(void) {
    Value string = value_string_copy("hello");
    CHECK(string.type == VALUE_STRING);

    Value clone = value_clone(&string);
    CHECK(clone.type == VALUE_STRING);
    CHECK(strcmp(clone.as.string, "hello") == 0);
    CHECK(clone.as.string != string.as.string);

    value_free(&clone);
    CHECK(clone.type == VALUE_NULL);
    value_free(&string);
    CHECK(string.type == VALUE_NULL);

    Value null_value = value_null();
    value_free(&null_value);
    value_free(&null_value);

    Value int_value = value_int(5);
    value_free(&int_value);
    CHECK(int_value.type == VALUE_NULL);
}

static void test_value_list_ownership(void) {
    Value items[2];
    items[0] = value_string_copy("a");
    items[1] = value_int(7);

    Value list = value_list(items, 2);
    CHECK(list.type == VALUE_LIST);
    CHECK(list.as.list.count == 2);
    CHECK(list.as.list.items[0].type == VALUE_STRING);
    CHECK(strcmp(list.as.list.items[0].as.string, "a") == 0);
    CHECK(list.as.list.items[0].as.string != items[0].as.string);
    CHECK(list.as.list.items[1].as.integer == 7);

    Value clone = value_clone(&list);
    CHECK(clone.type == VALUE_LIST);
    CHECK(clone.as.list.count == 2);
    CHECK(clone.as.list.items != list.as.list.items);
    CHECK(clone.as.list.items[0].as.string != list.as.list.items[0].as.string);
    CHECK(strcmp(clone.as.list.items[0].as.string, "a") == 0);

    value_free(&clone);
    CHECK(clone.type == VALUE_NULL);
    value_free(&list);
    CHECK(list.type == VALUE_NULL);

    value_free(&items[0]);
    value_free(&items[1]);

    /* An empty list owns no element storage but is still a valid list. */
    Value empty = value_list(NULL, 0);
    CHECK(empty.type == VALUE_LIST);
    CHECK(empty.as.list.count == 0);
    value_free(&empty);
    CHECK(empty.type == VALUE_NULL);
}

static void test_value_native_ownership(void) {
    Value native = value_native(native_add);
    CHECK(native.type == VALUE_NATIVE);
    CHECK(native.as.function == native_add);

    Value clone = value_clone(&native);
    CHECK(clone.type == VALUE_NATIVE);
    CHECK(clone.as.function == native_add);

    value_free(&clone);
    CHECK(clone.type == VALUE_NULL);
    value_free(&native);
    CHECK(native.type == VALUE_NULL);
}

static void test_value_error_ownership(void) {
    Value error_value = value_error("boom");
    CHECK(error_value.type == VALUE_ERROR);
    CHECK(strcmp(error_value.as.string, "boom") == 0);
    CHECK(error_value.as.string != NULL);

    Value clone = value_clone(&error_value);
    CHECK(clone.type == VALUE_ERROR);
    CHECK(strcmp(clone.as.string, "boom") == 0);
    CHECK(clone.as.string != error_value.as.string);

    value_free(&clone);
    CHECK(clone.type == VALUE_NULL);
    value_free(&error_value);
    CHECK(error_value.type == VALUE_NULL);
}

static void test_value_to_string(void) {
    Value null_value = value_null();
    Value text = value_to_string(&null_value);
    CHECK(text.type == VALUE_STRING);
    CHECK(strcmp(text.as.string, "null") == 0);
    value_free(&text);

    Value bool_value = value_bool(true);
    text = value_to_string(&bool_value);
    CHECK(strcmp(text.as.string, "true") == 0);
    value_free(&text);

    Value int_value = value_int(-42);
    text = value_to_string(&int_value);
    CHECK(strcmp(text.as.string, "-42") == 0);
    value_free(&text);

    Value float_value = value_float(3.5);
    text = value_to_string(&float_value);
    CHECK(strcmp(text.as.string, "3.5") == 0);
    value_free(&text);

    Value native_value = value_native(native_add);
    text = value_to_string(&native_value);
    CHECK(strcmp(text.as.string, "<native function>") == 0);
    value_free(&text);

    Value error_value = value_error("boom");
    text = value_to_string(&error_value);
    CHECK(strcmp(text.as.string, "boom") == 0);
    value_free(&text);

    Value string_value = value_string_copy("hi");
    text = value_to_string(&string_value);
    CHECK(text.type == VALUE_STRING);
    CHECK(text.as.string != string_value.as.string);
    CHECK(strcmp(text.as.string, "hi") == 0);
    value_free(&text);
    value_free(&string_value);

    /* Lists render with raw element display; nested lists recurse. */
    Value list_items[3];
    list_items[0] = value_int(1);
    list_items[1] = value_string_copy("a");
    list_items[2] = value_bool(true);
    Value list_value = value_list(list_items, 3);
    text = value_to_string(&list_value);
    CHECK(text.type == VALUE_STRING);
    CHECK(strcmp(text.as.string, "[1, a, true]") == 0);
    value_free(&text);

    Value nested[1];
    nested[0] = value_clone(&list_value);
    Value nested_value = value_list(nested, 1);
    text = value_to_string(&nested_value);
    CHECK(text.type == VALUE_STRING);
    CHECK(strcmp(text.as.string, "[[1, a, true]]") == 0);
    value_free(&text);
    value_free(&nested_value);
    value_free(&nested[0]);
    value_free(&list_value);
    value_free(&list_items[1]);

    value_free(&null_value);
    value_free(&bool_value);
    value_free(&int_value);
    value_free(&float_value);
    value_free(&native_value);
    value_free(&error_value);

    text = value_to_string(NULL);
    CHECK(text.type == VALUE_STRING);
    CHECK(strcmp(text.as.string, "null") == 0);
    value_free(&text);

    /* sizeof buffer check via double at the precision boundary */
    Value huge = value_float(1.7976931348623157e+308);
    text = value_to_string(&huge);
    CHECK(text.type == VALUE_STRING);
    value_free(&text);
    value_free(&huge);
}

int main(void) {
    /* Lexer */
    test_lexer_keywords();
    test_lexer_playful_aliases();
    test_lexer_identifiers();
    test_lexer_numbers();
    test_lexer_strings();
    test_lexer_unterminated_string();
    test_lexer_operators();
    test_lexer_newlines_and_eof();
    test_lexer_no_final_newline();
    test_lexer_indentation_and_dedentation();
    test_lexer_nested_indentation();
    test_lexer_dedent_at_file_end();
    test_lexer_blank_and_comment_lines();
    test_lexer_inconsistent_indentation();
    test_lexer_comment_after_code();

    /* Parser */
    test_parser_immutable_declaration();
    test_parser_mutable_declaration();
    test_parser_assignment();
    test_parser_string_literal_content();
    test_parser_string_escapes();
    test_parser_arithmetic_precedence();
    test_parser_identifier_expression_statement();
    test_parser_identifier_precedence();
    test_parser_assignment_precedence();
    test_parser_if_else();
    test_parser_if_elif_else();
    test_parser_if_elif_chain();
    test_parser_if_elif_no_else();
    test_parser_stray_elif_else();
    test_parser_block_statements();
    test_parser_invalid_declaration();
    test_parser_invalid_assignment();
    test_parser_declaration_without_value();
    test_parser_call_empty();
    test_parser_call_arguments();
    test_parser_call_argument_expression();
    test_parser_call_precedence();
    test_parser_nested_calls();
    test_parser_call_in_declaration();
    test_parser_call_missing_paren();
    test_parser_function_definition();
    test_parser_function_no_parameters();
    test_parser_function_errors();
    test_parser_function_missing_block();
    test_parser_while_loop();
    test_parser_for_loop();
    test_parser_for_loop_identifiers();
    test_parser_break_continue();
    test_parser_loop_errors();
    test_parser_list_literal();
    test_parser_index_expression();
    test_parser_for_list_mode();

    /* Evaluator */
    test_eval_let_declaration();
    test_eval_mut_declaration();
    test_eval_successful_reassignment();
    test_eval_arithmetic_after_reassignment();
    test_eval_immutable_reassignment_fails();
    test_eval_undefined_assignment_fails();
    test_env_undefined_assign_no_implicit();
    test_eval_repeated_string_reassignment();
    test_eval_string_reassignment();
    test_eval_string_comparison();
    test_eval_value_comparisons();
    test_eval_string_concatenation();
    test_eval_string_ownership();
    test_eval_if_branches();
    test_eval_if_elif();
    test_eval_if_elif_short_circuit();
    test_eval_if_elif_no_branch_taken();
    test_eval_if_elif_in_function();
    test_eval_while_loop();
    test_eval_for_loop();
    test_eval_loop_break();
    test_eval_loop_continue();
    test_eval_nested_loops();
    test_eval_loop_return_in_function();
    test_eval_loop_variable_binding();
    test_eval_loop_errors();
    test_eval_list_literal();
    test_eval_list_index();
    test_eval_string_index();
    test_eval_index_errors();
    test_eval_for_in_list();
    test_eval_list_equality();
    test_eval_list_truthiness();
    test_eval_integer_operations();
    test_eval_division_by_zero_fails();
    test_eval_undefined_lookup_fails();
    test_eval_call_native();
    test_eval_call_nested();
    test_eval_call_in_expression();
    test_eval_call_in_declaration();
    test_eval_call_value_holding_native();
    test_eval_call_empty_arguments();
    test_eval_call_string_argument();
    test_eval_call_undefined_callee();
    test_eval_call_non_function();
    test_eval_call_argument_error_stops();
    test_eval_native_error_reports();
    test_eval_ordered_comparisons();
    test_eval_comparison_type_errors();
    test_eval_comparison_in_condition();
    test_eval_float_remainder();
    test_eval_float_division_by_zero();
    test_eval_mixed_type_errors();
    test_eval_function_basic();
    test_eval_function_local_scope();
    test_eval_function_no_return();
    test_eval_function_return_in_if();
    test_eval_function_call_in_expression();
    test_eval_function_calls_native();
    test_eval_function_recursion();
    test_eval_function_recursion_guard();
    test_eval_function_arity_error();
    test_eval_return_outside_function();
    test_eval_function_rebinding_error();
    test_eval_function_closure_capture();
    test_eval_function_closure_mutation();
    test_eval_function_escaping_closure();
    test_eval_function_making_counter();
    test_eval_function_higher_order();
    test_eval_function_shadowing_via_let();
    test_eval_function_value_flow();

    /* Standard library */
    test_stdlib_len();
    test_stdlib_int_conversions();
    test_stdlib_float_conversions();
    test_stdlib_str();
    test_stdlib_type();
    test_stdlib_print_ok();

    /* Native registry */
    test_native_register_and_lookup();
    test_native_replace_existing_name();
    test_native_registry_growth();
    test_native_callback_invocation();
    test_native_cleanup_idempotent();

    /* Value ownership */
    test_value_ownership();
    test_value_list_ownership();
    test_value_native_ownership();
    test_value_error_ownership();
    test_value_function_ownership();
    test_value_to_string();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }

    puts("All FemLang tests passed.");
    return EXIT_SUCCESS;
}