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
        TOKEN_COMMA, TOKEN_DOT, TOKEN_COLON, TOKEN_ARROW, TOKEN_EOF
    };
    expect_tokens(
        "+ - * / % = == ! != < <= > >= ( ) [ ] { } , . : ->",
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

static void test_stdlib_type(void) {
    check_stdlib_string("type(1)\n", "int");
    check_stdlib_string("type(1.5)\n", "float");
    check_stdlib_string("type(true)\n", "bool");
    check_stdlib_string("type(\"s\")\n", "string");
    check_stdlib_string("type(null)\n", "null");
    check_stdlib_string("type(print)\n", "native");
    check_stdlib_error("type()\n");
    check_stdlib_error("type(1, 2)\n");
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
    test_value_native_ownership();
    test_value_error_ownership();
    test_value_to_string();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }

    puts("All FemLang tests passed.");
    return EXIT_SUCCESS;
}