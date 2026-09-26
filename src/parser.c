#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AstNode *expression(Parser *parser);
static AstNode *statement(Parser *parser);

/*
 * Advances the parser: the current token is remembered as "previous" and the
 * next token is fetched from the lexer. All token consumption goes through
 * this helper.
 */
static void advance(Parser *parser) {
    parser->previous = parser->current;
    parser->current = lexer_next(&parser->lexer);
}

static bool check(const Parser *parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser *parser, TokenType type) {
    if (!check(parser, type)) {
        return false;
    }
    advance(parser);
    return true;
}

static void error_at(Parser *parser, const Token *token, const char *message) {
    if (parser->had_error) {
        return;
    }
    parser->had_error = true;
    snprintf(parser->error_message, sizeof(parser->error_message),
             "line %zu, column %zu: %s", token->line, token->column, message);
}

static void error_here(Parser *parser, const char *message) {
    error_at(parser, &parser->current, message);
}

static char *copy_identifier(const Token *token) {
    char *copy = malloc(token->length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, token->start, token->length);
    copy[token->length] = '\0';
    return copy;
}

/*
 * Decodes a string literal token into a malloc'd C string. The outer quotes
 * are removed and simple escape sequences (\n, \t, \r, \\, \") are turned
 * into their single-character equivalents. Unknown escapes keep the escaped
 * character. On allocation failure returns NULL.
 */
static char *copy_string_literal(const Token *token) {
    const char *source = token->start + 1;
    size_t content_length = token->length >= 2 ? token->length - 2 : 0;

    char *out = malloc(content_length + 1U);
    if (out == NULL) {
        return NULL;
    }

    size_t out_index = 0;
    for (size_t i = 0; i < content_length; i++) {
        char c = source[i];
        if (c == '\\' && i + 1 < content_length) {
            i++;
            switch (source[i]) {
                case 'n':
                    out[out_index++] = '\n';
                    break;
                case 't':
                    out[out_index++] = '\t';
                    break;
                case 'r':
                    out[out_index++] = '\r';
                    break;
                case '\\':
                    out[out_index++] = '\\';
                    break;
                case '"':
                    out[out_index++] = '"';
                    break;
                default:
                    out[out_index++] = source[i];
                    break;
            }
        } else {
            out[out_index++] = c;
        }
    }
    out[out_index] = '\0';
    return out;
}

static AstNode *primary(Parser *parser) {
    Token token = parser->current;

    if (match(parser, TOKEN_INTEGER)) {
        AstNode *node = ast_new(AST_INTEGER, token.line, token.column);
        if (node != NULL) {
            node->as.integer = token.integer_value;
        }
        return node;
    }

    if (match(parser, TOKEN_FLOAT)) {
        AstNode *node = ast_new(AST_FLOAT, token.line, token.column);
        if (node != NULL) {
            node->as.floating = token.float_value;
        }
        return node;
    }

    if (match(parser, TOKEN_STRING)) {
        AstNode *node = ast_new(AST_STRING, token.line, token.column);
        if (node == NULL) {
            return NULL;
        }
        node->as.string = copy_string_literal(&token);
        if (node->as.string == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (match(parser, TOKEN_TRUE) || match(parser, TOKEN_FALSE)) {
        AstNode *node = ast_new(AST_BOOLEAN, token.line, token.column);
        if (node != NULL) {
            node->as.boolean = token.type == TOKEN_TRUE;
        }
        return node;
    }

    if (match(parser, TOKEN_NULL)) {
        return ast_new(AST_NULL, token.line, token.column);
    }

    if (match(parser, TOKEN_IDENTIFIER)) {
        AstNode *node = ast_new(AST_IDENTIFIER, token.line, token.column);
        if (node == NULL) {
            return NULL;
        }
        node->as.identifier = copy_identifier(&token);
        if (node->as.identifier == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (match(parser, TOKEN_LEFT_PAREN)) {
        AstNode *node = expression(parser);
        if (!match(parser, TOKEN_RIGHT_PAREN)) {
            error_here(parser, "expected ')' after expression");
        }
        return node;
    }

    error_here(parser, "expected an expression");
    return NULL;
}

/*
 * Parses a parenthesized argument list (the opening '(' has already been
 * consumed) into an AstNodeList. Empty lists are allowed; a trailing comma is
 * rejected by the surrounding expression grammar. On success the closing ')'
 * has been consumed.
 */
static bool parse_arguments(Parser *parser, AstNodeList *arguments) {
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        for (;;) {
            AstNode *argument = expression(parser);
            if (argument == NULL) {
                return false;
            }
            if (!ast_list_push(arguments, argument)) {
                ast_free(argument);
                return false;
            }
            if (!match(parser, TOKEN_COMMA)) {
                break;
            }
        }
    }

    if (!match(parser, TOKEN_RIGHT_PAREN)) {
        error_here(parser, "expected ')' after arguments");
        return false;
    }
    return true;
}

/*
 * A call expression: a primary followed by any number of parenthesized
 * argument lists. Postfix calls bind tighter than unary and binary operators,
 * so `f(1) + 2` is (f(1)) + 2.
 */
static AstNode *call(Parser *parser) {
    AstNode *callee = primary(parser);

    while (callee != NULL && check(parser, TOKEN_LEFT_PAREN)) {
        Token paren = parser->current;
        advance(parser);

        AstNode *node = ast_new(AST_CALL, paren.line, paren.column);
        if (node == NULL) {
            ast_free(callee);
            return NULL;
        }
        node->as.call.callee = callee;

        if (!parse_arguments(parser, &node->as.call.arguments)) {
            ast_free(node);
            return NULL;
        }
        callee = node;
    }

    return callee;
}

/*
 * Continuation of a call when the callee is already available. Used by
 * expression statements: the leading identifier was consumed to disambiguate
 * assignment, and any following '(' must still be folded into a call before
 * binary operators continue.
 */
static AstNode *call_tail(Parser *parser, AstNode *callee) {
    while (callee != NULL && check(parser, TOKEN_LEFT_PAREN)) {
        Token paren = parser->current;
        advance(parser);

        AstNode *node = ast_new(AST_CALL, paren.line, paren.column);
        if (node == NULL) {
            ast_free(callee);
            return NULL;
        }
        node->as.call.callee = callee;

        if (!parse_arguments(parser, &node->as.call.arguments)) {
            ast_free(node);
            return NULL;
        }
        callee = node;
    }

    return callee;
}

static AstNode *unary(Parser *parser) {
    if (match(parser, TOKEN_BANG) || match(parser, TOKEN_MINUS)) {
        Token operator_token = parser->previous;
        AstNode *operand = unary(parser);
        if (operand == NULL) {
            return NULL;
        }
        AstNode *node = ast_new(AST_UNARY, operator_token.line, operator_token.column);
        if (node == NULL) {
            ast_free(operand);
            return NULL;
        }
        node->as.unary.operator_type = operator_token.type;
        node->as.unary.operand = operand;
        return node;
    }
    return call(parser);
}

/*
 * Pratt-style left-associative operator level. Parses the left operand with
 * next(), then keeps folding any of the given operators with fresh right
 * operands parsed by next().
 */
static AstNode *level(
    Parser *parser,
    AstNode *(*next_operand)(Parser *),
    const TokenType *operators,
    size_t count
) {
    AstNode *left = next_operand(parser);

    while (left != NULL) {
        bool found = false;
        for (size_t i = 0; i < count; i++) {
            if (check(parser, operators[i])) {
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }

        advance(parser);
        Token operator_token = parser->previous;
        AstNode *right = next_operand(parser);
        if (right == NULL) {
            ast_free(left);
            return NULL;
        }

        AstNode *node = ast_new(AST_BINARY, operator_token.line, operator_token.column);
        if (node == NULL) {
            ast_free(left);
            ast_free(right);
            return NULL;
        }
        node->as.binary.operator_type = operator_token.type;
        node->as.binary.left = left;
        node->as.binary.right = right;
        left = node;
    }

    return left;
}

/*
 * Continuation of a Pratt level when the left operand is already available.
 * Used for expression statements that began with an identifier: the identifier
 * was consumed to test for '=', and if no '=' followed we continue parsing
 * operators from that point with correct precedence.
 */
static AstNode *level_tail(
    Parser *parser,
    AstNode *left,
    AstNode *(*next_operand)(Parser *),
    const TokenType *operators,
    size_t count
) {
    while (left != NULL) {
        bool found = false;
        for (size_t i = 0; i < count; i++) {
            if (check(parser, operators[i])) {
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }

        advance(parser);
        Token operator_token = parser->previous;
        AstNode *right = next_operand(parser);
        if (right == NULL) {
            ast_free(left);
            return NULL;
        }

        AstNode *node = ast_new(AST_BINARY, operator_token.line, operator_token.column);
        if (node == NULL) {
            ast_free(left);
            ast_free(right);
            return NULL;
        }
        node->as.binary.operator_type = operator_token.type;
        node->as.binary.left = left;
        node->as.binary.right = right;
        left = node;
    }

    return left;
}

static const TokenType MULTIPLICATIVE_OPERATORS[] = {
    TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT
};
static const TokenType ADDITIVE_OPERATORS[] = {
    TOKEN_PLUS, TOKEN_MINUS
};
static const TokenType COMPARISON_OPERATORS[] = {
    TOKEN_EQUAL_EQUAL, TOKEN_BANG_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL
};

static AstNode *factor(Parser *parser) {
    return level(parser, unary, MULTIPLICATIVE_OPERATORS,
                 sizeof(MULTIPLICATIVE_OPERATORS) / sizeof(MULTIPLICATIVE_OPERATORS[0]));
}

static AstNode *term(Parser *parser) {
    return level(parser, factor, ADDITIVE_OPERATORS,
                 sizeof(ADDITIVE_OPERATORS) / sizeof(ADDITIVE_OPERATORS[0]));
}

static AstNode *comparison(Parser *parser) {
    return level(parser, term, COMPARISON_OPERATORS,
                 sizeof(COMPARISON_OPERATORS) / sizeof(COMPARISON_OPERATORS[0]));
}

static AstNode *expression(Parser *parser) {
    return comparison(parser);
}

/*
 * Parses an indented block after ':' until the matching DEDENT token. Blank
 * lines inside the block are skipped.
 */
static AstNode *block(Parser *parser, size_t line, size_t column) {
    if (!match(parser, TOKEN_NEWLINE)) {
        error_here(parser, "expected newline after ':'");
        return NULL;
    }
    if (!match(parser, TOKEN_INDENT)) {
        error_here(parser, "expected indented block");
        return NULL;
    }

    AstNode *block_node = ast_new(AST_BLOCK, line, column);
    if (block_node == NULL) {
        return NULL;
    }

    while (!check(parser, TOKEN_DEDENT) &&
           !check(parser, TOKEN_EOF) &&
           !parser->had_error) {
        if (match(parser, TOKEN_NEWLINE)) {
            continue;
        }

        AstNode *stmt = statement(parser);
        if (stmt == NULL) {
            ast_free(block_node);
            return NULL;
        }
        if (!ast_list_push(&block_node->as.block.statements, stmt)) {
            ast_free(stmt);
            ast_free(block_node);
            return NULL;
        }
        match(parser, TOKEN_NEWLINE);
    }

    if (!match(parser, TOKEN_DEDENT)) {
        error_here(parser, "expected end of block");
        ast_free(block_node);
        return NULL;
    }
    return block_node;
}

static AstNode *if_statement(Parser *parser) {
    Token keyword = parser->previous;

    AstNode *condition = expression(parser);
    if (condition == NULL) {
        return NULL;
    }
    if (!match(parser, TOKEN_COLON)) {
        error_here(parser, "expected ':' after condition");
        ast_free(condition);
        return NULL;
    }

    AstNode *then_branch = block(parser, keyword.line, keyword.column);
    if (then_branch == NULL) {
        ast_free(condition);
        return NULL;
    }

    AstNode *else_branch = NULL;
    if (match(parser, TOKEN_ELSE)) {
        if (!match(parser, TOKEN_COLON)) {
            error_here(parser, "expected ':' after else");
            ast_free(condition);
            ast_free(then_branch);
            return NULL;
        }
        else_branch = block(parser, keyword.line, keyword.column);
        if (else_branch == NULL) {
            ast_free(condition);
            ast_free(then_branch);
            return NULL;
        }
    }

    AstNode *node = ast_new(AST_IF, keyword.line, keyword.column);
    if (node == NULL) {
        ast_free(condition);
        ast_free(then_branch);
        ast_free(else_branch);
        return NULL;
    }
    node->as.if_statement.condition = condition;
    node->as.if_statement.then_branch = then_branch;
    node->as.if_statement.else_branch = else_branch;
    return node;
}

static AstNode *parse_declaration(Parser *parser) {
    Token keyword = parser->previous;
    bool mutable_binding = keyword.type == TOKEN_MUT;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        error_here(parser, "expected variable name");
        return NULL;
    }
    Token name = parser->current;
    advance(parser);

    if (!match(parser, TOKEN_ASSIGN)) {
        error_here(parser, "expected '=' after variable name");
        return NULL;
    }

    AstNode *value = expression(parser);
    if (value == NULL) {
        return NULL;
    }

    AstNode *node = ast_new(AST_LET, keyword.line, keyword.column);
    if (node == NULL) {
        ast_free(value);
        return NULL;
    }
    node->as.declaration.name = copy_identifier(&name);
    node->as.declaration.mutable = mutable_binding;
    node->as.declaration.value = value;
    if (node->as.declaration.name == NULL) {
        ast_free(node);
        return NULL;
    }
    return node;
}

static AstNode *parse_assignment(Parser *parser, const Token *name) {
    AstNode *value = expression(parser);
    if (value == NULL) {
        return NULL;
    }

    AstNode *node = ast_new(AST_ASSIGNMENT, name->line, name->column);
    if (node == NULL) {
        ast_free(value);
        return NULL;
    }
    node->as.assignment.name = copy_identifier(name);
    node->as.assignment.value = value;
    if (node->as.assignment.name == NULL) {
        ast_free(node);
        return NULL;
    }
    return node;
}

/*
 * Expression statements that begin with an identifier are ambiguous with
 * assignments: the identifier is consumed first so that a following '=' can be
 * detected. Without '=', parsing resumes at the correct precedence level by
 * folding the remaining operators into the already-built identifier.
 */
static AstNode *expression_statement(Parser *parser) {
    if (check(parser, TOKEN_IDENTIFIER)) {
        Token name = parser->current;
        advance(parser);

        if (check(parser, TOKEN_ASSIGN)) {
            advance(parser);
            return parse_assignment(parser, &name);
        }

        AstNode *left = ast_new(AST_IDENTIFIER, name.line, name.column);
        if (left == NULL) {
            return NULL;
        }
        left->as.identifier = copy_identifier(&name);
        if (left->as.identifier == NULL) {
            ast_free(left);
            return NULL;
        }

        left = call_tail(parser, left);
        if (left == NULL) {
            return NULL;
        }

        left = level_tail(parser, left, unary,
                          MULTIPLICATIVE_OPERATORS,
                          sizeof(MULTIPLICATIVE_OPERATORS) / sizeof(MULTIPLICATIVE_OPERATORS[0]));
        if (left == NULL) {
            return NULL;
        }
        left = level_tail(parser, left, factor,
                          ADDITIVE_OPERATORS,
                          sizeof(ADDITIVE_OPERATORS) / sizeof(ADDITIVE_OPERATORS[0]));
        if (left == NULL) {
            return NULL;
        }
        left = level_tail(parser, left, term,
                          COMPARISON_OPERATORS,
                          sizeof(COMPARISON_OPERATORS) / sizeof(COMPARISON_OPERATORS[0]));
        if (left == NULL) {
            return NULL;
        }

        AstNode *stmt = ast_new(AST_EXPRESSION_STATEMENT, left->line, left->column);
        if (stmt == NULL) {
            ast_free(left);
            return NULL;
        }
        stmt->as.expression_statement.expression = left;
        return stmt;
    }

    AstNode *value = expression(parser);
    if (value == NULL) {
        return NULL;
    }
    AstNode *stmt = ast_new(AST_EXPRESSION_STATEMENT, value->line, value->column);
    if (stmt == NULL) {
        ast_free(value);
        return NULL;
    }
    stmt->as.expression_statement.expression = value;
    return stmt;
}

static AstNode *parse_return(Parser *parser) {
    Token keyword = parser->previous;

    AstNode *value = NULL;
    if (!check(parser, TOKEN_NEWLINE) && !check(parser, TOKEN_EOF)) {
        value = expression(parser);
    }

    AstNode *node = ast_new(AST_RETURN, keyword.line, keyword.column);
    if (node == NULL) {
        ast_free(value);
        return NULL;
    }
    node->as.return_statement.value = value;
    return node;
}

/*
 * Parses a function definition:
 *
 *     fn name(param, other, ...):
 *         body statements
 *
 * The opening '(' is required (even for zero parameters); the parameter list
 * holds AST_IDENTIFIER nodes and the body is a block. The function name is
 * stored separately because the runtime error messages need it.
 */
static AstNode *parse_function(Parser *parser) {
    Token keyword = parser->previous;

    if (!check(parser, TOKEN_IDENTIFIER)) {
        error_here(parser, "expected function name");
        return NULL;
    }
    Token name = parser->current;
    advance(parser);

    if (!match(parser, TOKEN_LEFT_PAREN)) {
        error_here(parser, "expected '(' after function name");
        return NULL;
    }

    AstNode *node = ast_new(AST_FUNCTION, keyword.line, keyword.column);
    if (node == NULL) {
        return NULL;
    }
    node->as.function.name = copy_identifier(&name);
    if (node->as.function.name == NULL) {
        ast_free(node);
        return NULL;
    }

    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        for (;;) {
            if (!check(parser, TOKEN_IDENTIFIER)) {
                error_here(parser, "expected parameter name");
                ast_free(node);
                return NULL;
            }
            Token param = parser->current;
            advance(parser);
            AstNode *param_node = ast_new(AST_IDENTIFIER, param.line, param.column);
            if (param_node == NULL) {
                ast_free(node);
                return NULL;
            }
            param_node->as.identifier = copy_identifier(&param);
            if (param_node->as.identifier == NULL) {
                ast_free(param_node);
                ast_free(node);
                return NULL;
            }
            if (!ast_list_push(&node->as.function.parameters, param_node)) {
                ast_free(param_node);
                ast_free(node);
                return NULL;
            }
            if (!match(parser, TOKEN_COMMA)) {
                break;
            }
        }
    }

    if (!match(parser, TOKEN_RIGHT_PAREN)) {
        error_here(parser, "expected ')' after parameters");
        ast_free(node);
        return NULL;
    }

    if (!match(parser, TOKEN_COLON)) {
        error_here(parser, "expected ':' after parameters");
        ast_free(node);
        return NULL;
    }

    node->as.function.body = block(parser, keyword.line, keyword.column);
    if (node->as.function.body == NULL) {
        ast_free(node);
        return NULL;
    }
    return node;
}

static AstNode *statement(Parser *parser) {
    while (match(parser, TOKEN_NEWLINE)) {
        /* skip blank lines */
    }

    if (check(parser, TOKEN_EOF) || check(parser, TOKEN_DEDENT)) {
        return NULL;
    }

    if (match(parser, TOKEN_IF)) {
        return if_statement(parser);
    }

    if (match(parser, TOKEN_LET) || match(parser, TOKEN_MUT)) {
        return parse_declaration(parser);
    }

    if (match(parser, TOKEN_RETURN)) {
        return parse_return(parser);
    }

    if (match(parser, TOKEN_FN)) {
        return parse_function(parser);
    }

    return expression_statement(parser);
}

void parser_init(Parser *parser, const char *source, size_t length) {
    memset(parser, 0, sizeof(*parser));
    lexer_init(&parser->lexer, source, length);
    parser->current = lexer_next(&parser->lexer);
}

AstNode *parser_parse(Parser *parser) {
    AstNode *program = ast_new(AST_PROGRAM, 1, 1);
    if (program == NULL) {
        return NULL;
    }

    while (!check(parser, TOKEN_EOF) && !parser->had_error) {
        if (match(parser, TOKEN_NEWLINE)) {
            continue;
        }

        AstNode *stmt = statement(parser);
        if (stmt == NULL) {
            if (!parser->had_error && check(parser, TOKEN_EOF)) {
                break;
            }
            ast_free(program);
            return NULL;
        }

        if (!ast_list_push(&program->as.block.statements, stmt)) {
            ast_free(stmt);
            ast_free(program);
            return NULL;
        }
        match(parser, TOKEN_NEWLINE);
    }

    if (check(parser, TOKEN_ERROR)) {
        error_here(parser, "lexer error");
    }
    if (parser->had_error) {
        ast_free(program);
        return NULL;
    }
    return program;
}

const char *parser_error(const Parser *parser) {
    return parser->error_message;
}