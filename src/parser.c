#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void error_at_current(Parser *parser, const char *message) {
    if (parser->had_error) {
        return;
    }

    parser->had_error = true;
    snprintf(parser->error_message, sizeof(parser->error_message),
        "line %zu, column %zu: %s",
        parser->current.line, parser->current.column, message);
}

static char *copy_token(Token token) {
    char *text = malloc(token.length + 1);
    if (text == NULL) {
        return NULL;
    }
    memcpy(text, token.start, token.length);
    text[token.length] = '\0';
    return text;
}

static AstNode *expression(Parser *parser);

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
        if (node != NULL) {
            node->as.string = copy_token(token);
            if (node->as.string == NULL) {
                ast_free(node);
                return NULL;
            }
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
        if (node != NULL) {
            node->as.identifier = copy_token(token);
            if (node->as.identifier == NULL) {
                ast_free(node);
                return NULL;
            }
        }
        return node;
    }

    if (match(parser, TOKEN_LEFT_PAREN)) {
        AstNode *node = expression(parser);
        if (!match(parser, TOKEN_RIGHT_PAREN)) {
            error_at_current(parser, "expected ')' after expression");
        }
        return node;
    }

    error_at_current(parser, "expected an expression");
    return NULL;
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

    return primary(parser);
}

static AstNode *factor(Parser *parser) {
    AstNode *left = unary(parser);
    while (left != NULL && (check(parser, TOKEN_STAR) ||
        check(parser, TOKEN_SLASH) || check(parser, TOKEN_PERCENT))) {
        advance(parser);
        Token operator_token = parser->previous;
        AstNode *right = unary(parser);
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

static AstNode *term(Parser *parser) {
    AstNode *left = factor(parser);
    while (left != NULL && (check(parser, TOKEN_PLUS) || check(parser, TOKEN_MINUS))) {
        advance(parser);
        Token operator_token = parser->previous;
        AstNode *right = factor(parser);
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

static AstNode *comparison(Parser *parser) {
    AstNode *left = term(parser);
    while (left != NULL && (check(parser, TOKEN_EQUAL_EQUAL) ||
        check(parser, TOKEN_BANG_EQUAL) || check(parser, TOKEN_LESS) ||
        check(parser, TOKEN_LESS_EQUAL) || check(parser, TOKEN_GREATER) ||
        check(parser, TOKEN_GREATER_EQUAL))) {
        advance(parser);
        Token operator_token = parser->previous;
        AstNode *right = term(parser);
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

static AstNode *expression(Parser *parser) {
    return comparison(parser);
}

static AstNode *statement(Parser *parser) {
    while (match(parser, TOKEN_NEWLINE)) {
    }

    if (check(parser, TOKEN_EOF)) {
        return NULL;
    }

    if (match(parser, TOKEN_LET) || match(parser, TOKEN_MUT)) {
        Token keyword = parser->previous;
        bool mutable = keyword.type == TOKEN_MUT;
        if (!check(parser, TOKEN_IDENTIFIER)) {
            error_at_current(parser, "expected variable name");
            return NULL;
        }
        Token name = parser->current;
        advance(parser);
        if (!match(parser, TOKEN_ASSIGN)) {
            error_at_current(parser, "expected '=' after variable name");
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
        node->as.declaration.name = copy_token(name);
        node->as.declaration.mutable = mutable;
        node->as.declaration.value = value;
        if (node->as.declaration.name == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (match(parser, TOKEN_RETURN)) {
        Token keyword = parser->previous;
        AstNode *value = check(parser, TOKEN_NEWLINE) || check(parser, TOKEN_EOF)
            ? NULL : expression(parser);
        AstNode *node = ast_new(AST_RETURN, keyword.line, keyword.column);
        if (node == NULL) {
            ast_free(value);
            return NULL;
        }
        node->as.return_statement.value = value;
        return node;
    }

    AstNode *value = expression(parser);
    if (value == NULL) {
        return NULL;
    }
    AstNode *node = ast_new(AST_EXPRESSION_STATEMENT, value->line, value->column);
    if (node == NULL) {
        ast_free(value);
        return NULL;
    }
    node->as.expression_statement.expression = value;
    return node;
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
        AstNode *node = statement(parser);
        if (node == NULL) {
            if (!parser->had_error && check(parser, TOKEN_EOF)) {
                break;
            }
            ast_free(program);
            return NULL;
        }
        if (!ast_list_push(&program->as.program.statements, node)) {
            ast_free(node);
            ast_free(program);
            return NULL;
        }
        match(parser, TOKEN_NEWLINE);
    }

    if (check(parser, TOKEN_ERROR)) {
        error_at_current(parser, "lexer error while parsing");
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
