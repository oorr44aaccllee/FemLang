#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void advance(Parser *p) { p->previous = p->current; p->current = lexer_next(&p->lexer); }
static bool check(const Parser *p, TokenType t) { return p->current.type == t; }
static bool match(Parser *p, TokenType t) { if (!check(p, t)) return false; advance(p); return true; }
static void error_current(Parser *p, const char *message) { if (!p->had_error) { p->had_error = true; snprintf(p->error_message, sizeof(p->error_message), "line %zu, column %zu: %s", p->current.line, p->current.column, message); } }
static char *copy_token(Token t) { char *s = malloc(t.length + 1); if (!s) return NULL; memcpy(s, t.start, t.length); s[t.length] = '\0'; return s; }
static AstNode *expression(Parser *p);
static AstNode *statement(Parser *p);

static AstNode *primary(Parser *p) {
    Token t = p->current;
    if (match(p, TOKEN_INTEGER)) { AstNode *n = ast_new(AST_INTEGER, t.line, t.column); if (n) n->as.integer = t.integer_value; return n; }
    if (match(p, TOKEN_FLOAT)) { AstNode *n = ast_new(AST_FLOAT, t.line, t.column); if (n) n->as.floating = t.float_value; return n; }
    if (match(p, TOKEN_STRING)) { AstNode *n = ast_new(AST_STRING, t.line, t.column); if (n) { n->as.string = copy_token(t); if (!n->as.string) { ast_free(n); return NULL; } } return n; }
    if (match(p, TOKEN_TRUE) || match(p, TOKEN_FALSE)) { AstNode *n = ast_new(AST_BOOLEAN, t.line, t.column); if (n) n->as.boolean = t.type == TOKEN_TRUE; return n; }
    if (match(p, TOKEN_NULL)) return ast_new(AST_NULL, t.line, t.column);
    if (match(p, TOKEN_IDENTIFIER)) { AstNode *n = ast_new(AST_IDENTIFIER, t.line, t.column); if (n) { n->as.identifier = copy_token(t); if (!n->as.identifier) { ast_free(n); return NULL; } } return n; }
    if (match(p, TOKEN_LEFT_PAREN)) { AstNode *n = expression(p); if (!match(p, TOKEN_RIGHT_PAREN)) error_current(p, "expected ')' after expression"); return n; }
    error_current(p, "expected an expression"); return NULL;
}
static AstNode *unary(Parser *p) {
    if (match(p, TOKEN_BANG) || match(p, TOKEN_MINUS)) { Token op = p->previous; AstNode *operand = unary(p); if (!operand) return NULL; AstNode *n = ast_new(AST_UNARY, op.line, op.column); if (!n) { ast_free(operand); return NULL; } n->as.unary.operator_type = op.type; n->as.unary.operand = operand; return n; }
    return primary(p);
}
static AstNode *binary_level(Parser *p, AstNode *(*next)(Parser *), const TokenType *types, size_t count) {
    AstNode *left = next(p);
    while (left) {
        bool found = false; for (size_t i = 0; i < count; i++) if (check(p, types[i])) { found = true; break; }
        if (!found) break;
        advance(p); Token op = p->previous; AstNode *right = next(p); if (!right) { ast_free(left); return NULL; }
        AstNode *n = ast_new(AST_BINARY, op.line, op.column); if (!n) { ast_free(left); ast_free(right); return NULL; }
        n->as.binary.operator_type = op.type; n->as.binary.left = left; n->as.binary.right = right; left = n;
    }
    return left;
}
static AstNode *factor(Parser *p) { static const TokenType ops[] = { TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT }; return binary_level(p, unary, ops, 3); }
static AstNode *term(Parser *p) { static const TokenType ops[] = { TOKEN_PLUS, TOKEN_MINUS }; return binary_level(p, factor, ops, 2); }
static AstNode *comparison(Parser *p) { static const TokenType ops[] = { TOKEN_EQUAL_EQUAL, TOKEN_BANG_EQUAL, TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_GREATER, TOKEN_GREATER_EQUAL }; return binary_level(p, term, ops, 6); }
static AstNode *expression(Parser *p) { return comparison(p); }

static AstNode *block(Parser *p, size_t line, size_t column) {
    if (!match(p, TOKEN_NEWLINE)) { error_current(p, "expected a new line after ':'"); return NULL; }
    if (!match(p, TOKEN_INDENT)) { error_current(p, "expected an indented block"); return NULL; }
    AstNode *b = ast_new(AST_BLOCK, line, column); if (!b) return NULL;
    while (!check(p, TOKEN_DEDENT) && !check(p, TOKEN_EOF) && !p->had_error) {
        if (match(p, TOKEN_NEWLINE)) continue;
        AstNode *s = statement(p); if (!s) { ast_free(b); return NULL; }
        if (!ast_list_push(&b->as.program.statements, s)) { ast_free(s); ast_free(b); return NULL; }
        match(p, TOKEN_NEWLINE);
    }
    if (!match(p, TOKEN_DEDENT)) { error_current(p, "expected end of indented block"); ast_free(b); return NULL; }
    return b;
}
static AstNode *if_statement(Parser *p) {
    Token keyword = p->previous;
    AstNode *condition = expression(p);
    if (!condition) return NULL;
    if (!match(p, TOKEN_COLON)) { error_current(p, "expected ':' after condition"); ast_free(condition); return NULL; }
    AstNode *then_branch = block(p, keyword.line, keyword.column); if (!then_branch) { ast_free(condition); return NULL; }
    AstNode *else_branch = NULL;
    if (match(p, TOKEN_ELSE)) {
        if (!match(p, TOKEN_COLON)) { error_current(p, "expected ':' after else"); ast_free(condition); ast_free(then_branch); return NULL; }
        else_branch = block(p, keyword.line, keyword.column); if (!else_branch) { ast_free(condition); ast_free(then_branch); return NULL; }
    }
    AstNode *n = ast_new(AST_IF, keyword.line, keyword.column); if (!n) { ast_free(condition); ast_free(then_branch); ast_free(else_branch); return NULL; }
    n->as.if_statement.condition = condition; n->as.if_statement.then_branch = then_branch; n->as.if_statement.else_branch = else_branch; return n;
}
static AstNode *statement(Parser *p) {
    while (match(p, TOKEN_NEWLINE)) {}
    if (check(p, TOKEN_EOF) || check(p, TOKEN_DEDENT)) return NULL;
    if (match(p, TOKEN_IF)) return if_statement(p);
    if (match(p, TOKEN_LET) || match(p, TOKEN_MUT)) {
        Token keyword = p->previous; bool mutable = keyword.type == TOKEN_MUT;
        if (!check(p, TOKEN_IDENTIFIER)) { error_current(p, "expected variable name"); return NULL; }
        Token name = p->current; advance(p); if (!match(p, TOKEN_ASSIGN)) { error_current(p, "expected '=' after variable name"); return NULL; }
        AstNode *value = expression(p); if (!value) return NULL; AstNode *n = ast_new(AST_LET, keyword.line, keyword.column); if (!n) { ast_free(value); return NULL; }
        n->as.declaration.name = copy_token(name); n->as.declaration.mutable = mutable; n->as.declaration.value = value; if (!n->as.declaration.name) { ast_free(n); return NULL; } return n;
    }
    if (match(p, TOKEN_RETURN)) { Token k = p->previous; AstNode *v = check(p, TOKEN_NEWLINE) || check(p, TOKEN_DEDENT) || check(p, TOKEN_EOF) ? NULL : expression(p); AstNode *n = ast_new(AST_RETURN, k.line, k.column); if (!n) { ast_free(v); return NULL; } n->as.return_statement.value = v; return n; }
    AstNode *v = expression(p); if (!v) return NULL; AstNode *n = ast_new(AST_EXPRESSION_STATEMENT, v->line, v->column); if (!n) { ast_free(v); return NULL; } n->as.expression_statement.expression = v; return n;
}
void parser_init(Parser *p, const char *source, size_t length) { memset(p, 0, sizeof(*p)); lexer_init(&p->lexer, source, length); p->current = lexer_next(&p->lexer); }
AstNode *parser_parse(Parser *p) {
    AstNode *program = ast_new(AST_PROGRAM, 1, 1); if (!program) return NULL;
    while (!check(p, TOKEN_EOF) && !p->had_error) {
        if (match(p, TOKEN_NEWLINE)) continue;
        AstNode *s = statement(p); if (!s) { if (!p->had_error && check(p, TOKEN_EOF)) break; ast_free(program); return NULL; }
        if (!ast_list_push(&program->as.program.statements, s)) { ast_free(s); ast_free(program); return NULL; }
        match(p, TOKEN_NEWLINE);
    }
    if (check(p, TOKEN_ERROR)) error_current(p, "lexer error while parsing");
    if (p->had_error) { ast_free(program); return NULL; }
    return program;
}
const char *parser_error(const Parser *p) { return p->error_message; }
