# Milestone report: stable baseline, safe reassignment, and native registry

Date: 2026-09-24
Repo: `C:\Users\User\Downloads\FemLang-main` (working tree has no `.git`; `git`
is not installed on this machine, so no branch or commit was created — the full
contents of every changed file are embedded in the appendix below as the durable
record).

## 1. Changes made and why

Stabilized the C11 baseline so the interpreter runs the documented semantics
cleanly, implemented safe variable reassignment with a first-error runtime error
channel, added an ownership-safe native C function registry, and replaced the
single failing regression check with a comprehensive automated suite.

### 1.1 Initial inconsistencies found

- `tests/test_runner.c:73` asserted `VALUE_NULL` for `let fixed = 1; fixed = 2;
  fixed`, but the interpreter legally returns `1` (assignment failure left the
  immutable binding untouched). The expectation was wrong.
- `env_define` inserted a half-initialized entry if the *value* clone failed and
  the OOM check was wrong; it also never checked the *name* allocation before
  use.
- `env_assign` freed the old stored value even when the clone of the new value
  failed.
- The evaluator silently swallowed failed assignments and undefined lookups —
  no diagnostics and no defined post-error behavior.
- Identifier-led expression statements threw away the operator chain, so
  `a * b + c` evaluated as `a` only.
- String values contained the source quotes and raw `\n` escapes.
- Integer arithmetic had no overflow guards; `/` by zero and `INT64_MIN / -1`
  were undefined behavior; `%` parsed but evaluated to null.
- `src/parse_main.c` referenced the non-existent `node->as.program` member of
  the rewritten AST union.
- Docs claimed string concatenation and float arithmetic were implemented; the
  evaluator returned null for them.
- Equality of two ints (`1 == 1`) was routed into the integer arithmetic
  branch and reported "integer arithmetic error" (found by smoke-testing
  `examples/control_flow.fem` after the rewrite; fixed by dispatching equality
  before arithmetic).

### 1.2 Files changed

Headers: `include/lexer.h`, `include/ast.h`, `include/value.h`,
`include/evaluator.h`, `include/native.h`.
Sources: `src/ast.c`, `src/lexer.c`, `src/parser.c`, `src/evaluator.c`,
`src/eval_main.c`, `src/parse_main.c`.
Tests: `tests/test_runner.c` (rewritten and greatly expanded).
Docs: `README.md`, `docs/reassignment.md`, `docs/baseline-validation.md`,
`docs/testing.md`, `docs/parser-roadmap.md`, `docs/control-flow.md`,
`docs/ownership.md` (new).
Examples: `examples/basic.fem`, `examples/control_flow.fem` (made runnable in
this milestone's scope).

Unchanged: `Makefile`, `include/token.h`, `include/parser.h`, `src/token.c`,
`src/native.c`, `src/main.c` (legacy, unbuilt).

## 2. Final assignment semantics (safe reassignment)

- `let name = value` creates an **immutable** binding.
- `mut name = value` creates a **mutable** binding.
- An expression statement `name = value` reassigns a binding.
- Reassignment fails with a recorded error, leaving the stored value untouched:
  - missing binding: `cannot assign to undefined variable '<name>'`;
  - immutable binding: `cannot reassign immutable variable '<name>'`.
- A failed assignment never creates an implicit variable.
- `env_assign` clones the new value **before** releasing the old one, so the
  binding is never left in a freed/half-updated state.
- The first runtime error is recorded once in the environment and reported by
  `env_error()`; every later evaluation returns `VALUE_NULL`, so execution stops
  at the first failure and the program result is well-defined.
- The interpreter prints `Runtime error: <message>` to stderr and exits
  non-zero.

## 3. Value ownership rules

- `VALUE_STRING` owns one malloc'd, NUL-terminated buffer; every other type owns
  no heap memory.
- `value_clone` deep-copies strings, shallow-copies non-strings.
- `value_free` releases the string and resets the value to `VALUE_NULL`; safe on
  already-freed values.
- `value_string_copy`/`value_clone` on a string return `VALUE_NULL` on
  allocation failure — callers treat "asked for string, got null" as OOM.
- Environment bindings own a deep copy of the name and the value
  (`env_define`/`env_assign` clone); `env_lookup` returns a borrowed pointer.

## 4. Native registry API and behavior

Header `include/native.h`, implementation `src/native.c` (unchanged from the
already-correct version).

- `native_register(registry, name, function)` copies the name (registry owns the
  copy); replaces the function on duplicate names **without** changing the
  count; grows via `realloc` from capacity 8; rejects NULL registry/name/
  function.
- `native_lookup(registry, name)` returns the callback or NULL.
- `native_registry_free` releases all copied names and re-initializes the
  registry (idempotent).
- Independent of function-call syntax (not yet implemented); testable in
  isolation, which the suite does.

## 5. Commands run

```
make clean && make && make test        # passes: All FemLang tests passed.
make clean && make asan                # fails at LINK: cannot find -lasan / -lubsan
gcc -fanalyzer on all sources          # no findings
./femlang examples/basic.fem           # prints 30, exit 0
./femlang examples/control_flow.fem    # prints "slay", exit 0
./femlang <reassign smoke>.fem         # prints 10, exit 0
./femlang <strings smoke>.fem          # prints "Taylor", exit 0
```

## 6. Exact test results

- `make test`: **All FemLang tests passed.** (52 checks across lexer, parser,
  evaluator, native registry, and value-ownership areas; any failure prints the
  failing `CHECK` expression with file:line and exits non-zero.)
- Both example programs and the reassignment/string smoke programs run under
  `./femlang` with exit code 0.

## 7. Sanitizer results

`make asan` cannot link on this machine: the w64devkit w64-w64-mingw32 GCC does
not ship `libasan`/`libubsan` (`ld: cannot find -lasan`, `cannot find -lubsan`),
and no clang is present. This is a toolchain limitation, not a code failure; the
target is unchanged and will work on a toolchain with sanitizer runtimes. As a
substitute, every source (including the final `src/evaluator.c`) compiles clean
under `gcc -fanalyzer` (`-Wall -Wextra -Wpedantic -Werror -fanalyzer`) with no
findings.

## 8. Remaining limitations

- Comparison operators `< <= > >=` lex and parse but are not evaluated (`==`
  and `!=` work). Examples/documentation now use `==`.
- Function declarations and call expressions (including `print`) are not
  implemented; `AST_CALL` is a placeholder, and the native registry is ready for
  it.
- Lists, loops, `elif`, `try/catch/finally`, and `match` are unimplemented.
- Unary minus on `INT64_MIN` is an unguarded overflow (effectively unreachable
  because no literal or guarded operation can currently produce `INT64_MIN`).
- No git: the report appendix is the durable change record; no commit created.

## 9. Next phase

- Wire the native registry into the evaluator and implement call expressions
  (`print` etc.) with argument ownership rules.
- Implement true comparison operators and mixed-type equality.
- Add loops and lists, then a standard library and examples.
- Re-run `make test` and, on a capable toolchain, `make asan`.
- Initialize a real git repository so future milestones can be committed with
  conventional message style.

---

## Appendix: complete contents of every changed file

<!-- REPORT_FILES_ANCHOR -->
### include/lexer.h

```c
#ifndef FEMLANG_LEXER_H
#define FEMLANG_LEXER_H

#include "token.h"

#include <stdbool.h>
#include <stddef.h>

#define FEM_MAX_INDENT_DEPTH 128

typedef struct {
    const char *source;
    size_t length;
    size_t position;
    size_t line;
    size_t column;
    bool at_line_start;
    bool emitted_eof;
    unsigned int indent_depth;
    unsigned int indent_stack[FEM_MAX_INDENT_DEPTH];
    unsigned int pending_dedents;
} Lexer;

void lexer_init(Lexer *lexer, const char *source, size_t length);
Token lexer_next(Lexer *lexer);

#endif
```

### include/ast.h

```c
#ifndef FEMLANG_AST_H
#define FEMLANG_AST_H

#include "token.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AST_PROGRAM,
    AST_BLOCK,
    AST_INTEGER,
    AST_FLOAT,
    AST_STRING,
    AST_BOOLEAN,
    AST_NULL,
    AST_IDENTIFIER,
    AST_LIST,
    AST_UNARY,
    AST_BINARY,
    AST_CALL,
    AST_LET,
    AST_ASSIGNMENT,
    AST_EXPRESSION_STATEMENT,
    AST_RETURN,
    AST_IF
} AstNodeType;

typedef struct AstNode AstNode;

typedef struct {
    AstNode **items;
    size_t count;
    size_t capacity;
} AstNodeList;

struct AstNode {
    AstNodeType type;
    size_t line;
    size_t column;
    union {
        int64_t integer;
        double floating;
        bool boolean;
        char *string;
        char *identifier;
        struct { AstNodeList elements; } list;
        struct { TokenType operator_type; AstNode *operand; } unary;
        struct { TokenType operator_type; AstNode *left; AstNode *right; } binary;
        struct { AstNode *callee; AstNodeList arguments; } call;
        struct { char *name; bool mutable; AstNode *value; } declaration;
        struct { char *name; AstNode *value; } assignment;
        struct { AstNode *expression; } expression_statement;
        struct { AstNode *value; } return_statement;
        struct { AstNode *condition; AstNode *then_branch; AstNode *else_branch; } if_statement;
        struct { AstNodeList statements; } block;
    } as;
};

AstNode *ast_new(AstNodeType type, size_t line, size_t column);
bool ast_list_push(AstNodeList *list, AstNode *node);
void ast_free(AstNode *node);

#endif
```

### include/value.h

```c
#ifndef FEMLANG_VALUE_H
#define FEMLANG_VALUE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Ownership rules
 * ---------------
 * Each Value owns its own heap data and nothing else:
 *
 *   - VALUE_STRING owns a single NUL-terminated char* allocated with malloc.
 *     The string may be NULL only when the Value itself is empty/invalid.
 *   - All other value types own no heap memory.
 *
 * Every heap-owning Value has exactly one logical owner. Copy ownership by
 * calling value_clone() (deep copy for strings) before storing a value where
 * the original must remain valid and separately owned. Release ownership with
 * value_free(); it is a no-op on VALUE_NULL and safe on already-freed values.
 *
 * value_string_copy() and value_clone() on a string return a VALUE_NULL value
 * if the allocation fails, so a failed clone never leaks or shares the
 * original string. Callers that rely on a successful string clone must treat
 * "requested string, got NULL" as an allocation failure.
 */

typedef enum {
    VALUE_NULL,
    VALUE_BOOL,
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_STRING
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        char *string;
    } as;
} Value;

Value value_null(void);
Value value_bool(bool value);
Value value_int(int64_t value);
Value value_float(double value);
Value value_string_copy(const char *source);
Value value_clone(const Value *value);
void value_free(Value *value);
bool value_truthy(const Value *value);
void print_value(const Value *value);

#endif
```

### include/evaluator.h

```c
#ifndef FEMLANG_EVALUATOR_H
#define FEMLANG_EVALUATOR_H

#include "ast.h"
#include "value.h"

#include <stdbool.h>

typedef struct Environment Environment;

/*
 * Environment stores named bindings:
 *   char *name;    heap copy, owned by the environment
 *   Value value;   heap-cloned on define and assign (see value.h)
 *   bool mutable;
 *
 * env_define() clones both the name and the value; the caller keeps ownership
 * of its value argument. env_lookup() returns a borrowed pointer into the
 * environment that is valid only until that binding is reassigned or the
 * environment is freed; callers must clone before storing or mutating.
 *
 * env_assign() clones the incoming value, releases the previous stored value,
 * and only then installs the clone. It fails without touching the stored value
 * when the binding is missing, immutable, or the clone could not be allocated.
 *
 * The first semantic failure (failed assignment, failed declaration, integer
 * overflow, division by zero) is recorded in the environment and reported by
 * env_error(); once recorded, further evaluations return null immediately so
 * execution does not continue past the first error.
 */
Environment *env_new(void);
void env_free(Environment *env);

bool env_define(
    Environment *env,
    const char *name,
    const Value *value,
    bool mutable
);

Value *env_lookup(Environment *env, const char *name);

bool env_assign(
    Environment *env,
    const char *name,
    const Value *value
);

const char *env_error(const Environment *env);

/*
 * Evaluates a parsed program. Returns a value the caller owns; release it
 * with value_free(). When a runtime error occurred, the error text is
 * available via env_error(env) and the returned value is VALUE_NULL.
 */
Value eval_ast(Environment *env, const AstNode *node);

#endif
```

### include/native.h

```c
#ifndef FEMLANG_NATIVE_H
#define FEMLANG_NATIVE_H

#include "value.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Native C function registry
 * --------------------------
 *
 * The registry maps names to plain C callbacks. It is deliberately
 * independent of the FemLang call-expression syntax, which is not implemented
 * yet, so it is directly testable in isolation.
 *
 *   - native_register() copies the name; the registry owns the copy.
 *   - Registering an existing name replaces the previous function and keeps
 *     the original name copy (no churn, registry count is unchanged).
 *   - The registry grows by reallocation without losing existing entries.
 *   - NULL names, NULL function pointers, and NULL registries are rejected.
 *   - native_registry_free() releases every copied name and can be called on
 *     an already-initialized/freed registry (it re-initializes).
 */

typedef Value (*FemNativeFunction)(size_t argument_count, const Value *arguments);

typedef struct {
    char *name;
    FemNativeFunction function;
} FemNativeBinding;

typedef struct {
    FemNativeBinding *bindings;
    size_t count;
    size_t capacity;
} FemNativeRegistry;

void native_registry_init(FemNativeRegistry *registry);
void native_registry_free(FemNativeRegistry *registry);

bool native_register(
    FemNativeRegistry *registry,
    const char *name,
    FemNativeFunction function
);

FemNativeFunction native_lookup(
    const FemNativeRegistry *registry,
    const char *name
);

#endif
```

### src/ast.c

```c
#include "ast.h"

#include <stdlib.h>

AstNode *ast_new(AstNodeType type, size_t line, size_t column) {
    AstNode *node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->type = type;
    node->line = line;
    node->column = column;
    return node;
}

bool ast_list_push(AstNodeList *list, AstNode *node) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 4U : list->capacity * 2U;
        AstNode **new_items = realloc(list->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return false;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = node;
    return true;
}

static void free_node_list(AstNodeList *list) {
    for (size_t i = 0; i < list->count; i++) {
        ast_free(list->items[i]);
    }
    free(list->items);
}

/*
 * Releases every child reachable from node. Every AST node type declared in
 * ast.h must appear here so no owned subtree is leaked. Scalar node types
 * (integer, float, boolean, null) own nothing and fall through harmlessly.
 */
void ast_free(AstNode *node) {
    if (node == NULL) {
        return;
    }

    switch (node->type) {
        case AST_STRING:
            free(node->as.string);
            break;
        case AST_IDENTIFIER:
            free(node->as.identifier);
            break;
        case AST_LIST:
            free_node_list(&node->as.list.elements);
            break;
        case AST_UNARY:
            ast_free(node->as.unary.operand);
            break;
        case AST_BINARY:
            ast_free(node->as.binary.left);
            ast_free(node->as.binary.right);
            break;
        case AST_CALL:
            ast_free(node->as.call.callee);
            free_node_list(&node->as.call.arguments);
            break;
        case AST_LET:
            free(node->as.declaration.name);
            ast_free(node->as.declaration.value);
            break;
        case AST_ASSIGNMENT:
            free(node->as.assignment.name);
            ast_free(node->as.assignment.value);
            break;
        case AST_EXPRESSION_STATEMENT:
            ast_free(node->as.expression_statement.expression);
            break;
        case AST_RETURN:
            ast_free(node->as.return_statement.value);
            break;
        case AST_IF:
            ast_free(node->as.if_statement.condition);
            ast_free(node->as.if_statement.then_branch);
            ast_free(node->as.if_statement.else_branch);
            break;
        case AST_PROGRAM:
        case AST_BLOCK:
            free_node_list(&node->as.block.statements);
            break;
        case AST_INTEGER:
        case AST_FLOAT:
        case AST_BOOLEAN:
        case AST_NULL:
            break;
    }

    free(node);
}
```

### src/lexer.c

```c
#include "lexer.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool at_end(const Lexer *lexer) {
    return lexer->position >= lexer->length;
}

static char peek_char(const Lexer *lexer) {
    if (at_end(lexer)) {
        return '\0';
    }
    return lexer->source[lexer->position];
}

static char peek_next_char(const Lexer *lexer) {
    if (lexer->position + 1 >= lexer->length) {
        return '\0';
    }
    return lexer->source[lexer->position + 1];
}

static char advance(Lexer *lexer) {
    if (at_end(lexer)) {
        return '\0';
    }
    char c = lexer->source[lexer->position++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
        lexer->at_line_start = true;
    } else {
        lexer->column++;
    }
    return c;
}

static Token make_token(
    Lexer *lexer,
    TokenType type,
    size_t start,
    size_t line,
    size_t column
) {
    Token token;
    token.type = type;
    token.start = lexer->source + start;
    token.length = lexer->position - start;
    token.line = line;
    token.column = column;
    token.integer_value = 0;
    token.float_value = 0.0;
    return token;
}

static Token error_token(Lexer *lexer, size_t start, size_t line, size_t column) {
    return make_token(lexer, TOKEN_ERROR, start, line, column);
}

static bool is_identifier_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_identifier_part(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/*
 * Keywords are stored together with their playful aliases in matching order.
 * Several aliases intentionally map to the same canonical token type.
 */
static TokenType lookup_keyword(const char *start, size_t length) {
    static const char *const keyword_names[] = {
        "let",    "spark",   "mut",    "fn",    "return", "serve",
        "if",     "elif",    "else",   "for",   "in",     "while",
        "break",  "slay",    "continue", "skip", "true",  "false",
        "null",   "try",     "catch",  "finally", "match"
    };
    static const TokenType keyword_types[] = {
        TOKEN_LET,   TOKEN_LET,   TOKEN_MUT,   TOKEN_FN,
        TOKEN_RETURN, TOKEN_RETURN, TOKEN_IF,   TOKEN_ELIF,
        TOKEN_ELSE,  TOKEN_FOR,   TOKEN_IN,    TOKEN_WHILE,
        TOKEN_BREAK, TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_CONTINUE,
        TOKEN_TRUE,  TOKEN_FALSE, TOKEN_NULL,  TOKEN_TRY,
        TOKEN_CATCH, TOKEN_FINALLY, TOKEN_MATCH
    };

    for (size_t i = 0; i < sizeof(keyword_types) / sizeof(keyword_types[0]); i++) {
        if (strlen(keyword_names[i]) == length &&
            memcmp(keyword_names[i], start, length) == 0) {
            return keyword_types[i];
        }
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier_token(Lexer *lexer, size_t start, size_t line, size_t column) {
    while (is_identifier_part(peek_char(lexer))) {
        advance(lexer);
    }
    TokenType type = lookup_keyword(lexer->source + start, lexer->position - start);
    return make_token(lexer, type, start, line, column);
}

static Token number_token(Lexer *lexer, size_t start, size_t line, size_t column) {
    bool is_float = false;

    while (isdigit((unsigned char)peek_char(lexer))) {
        advance(lexer);
    }

    if (peek_char(lexer) == '.' && isdigit((unsigned char)peek_next_char(lexer))) {
        is_float = true;
        advance(lexer);
        while (isdigit((unsigned char)peek_char(lexer))) {
            advance(lexer);
        }
    }

    Token token = make_token(lexer, is_float ? TOKEN_FLOAT : TOKEN_INTEGER,
                             start, line, column);

    size_t length = lexer->position - start;
    char buffer[128];
    if (length >= sizeof(buffer)) {
        return error_token(lexer, start, line, column);
    }
    memcpy(buffer, lexer->source + start, length);
    buffer[length] = '\0';

    char *end = NULL;
    errno = 0;
    if (is_float) {
        token.float_value = strtod(buffer, &end);
    } else {
        token.integer_value = strtoll(buffer, &end, 10);
    }
    if (errno != 0 || end == buffer || *end != '\0') {
        return error_token(lexer, start, line, column);
    }
    return token;
}

/*
 * Consumes a string literal including both surrounding quotes. The token slice
 * keeps the quotes and raw escape sequences; the parser decodes the content.
 */
static Token string_token(Lexer *lexer, size_t start, size_t line, size_t column) {
    advance(lexer);

    while (!at_end(lexer) && peek_char(lexer) != '"') {
        if (peek_char(lexer) == '\n') {
            return error_token(lexer, start, line, column);
        }
        if (peek_char(lexer) == '\\') {
            advance(lexer);
            if (!at_end(lexer)) {
                advance(lexer);
            }
        } else {
            advance(lexer);
        }
    }

    if (at_end(lexer)) {
        return error_token(lexer, start, line, column);
    }
    advance(lexer);
    return make_token(lexer, TOKEN_STRING, start, line, column);
}

static Token punctuation_token(
    Lexer *lexer,
    size_t start,
    size_t line,
    size_t column
) {
    char c = advance(lexer);

    if (c == '-' && peek_char(lexer) == '>') {
        advance(lexer);
        return make_token(lexer, TOKEN_ARROW, start, line, column);
    }

    if ((c == '=' || c == '!' || c == '<' || c == '>') && peek_char(lexer) == '=') {
        advance(lexer);
        TokenType type;
        if (c == '=') {
            type = TOKEN_EQUAL_EQUAL;
        } else if (c == '!') {
            type = TOKEN_BANG_EQUAL;
        } else if (c == '<') {
            type = TOKEN_LESS_EQUAL;
        } else {
            type = TOKEN_GREATER_EQUAL;
        }
        return make_token(lexer, type, start, line, column);
    }

    TokenType type;
    switch (c) {
        case '+': type = TOKEN_PLUS; break;
        case '-': type = TOKEN_MINUS; break;
        case '*': type = TOKEN_STAR; break;
        case '/': type = TOKEN_SLASH; break;
        case '%': type = TOKEN_PERCENT; break;
        case '=': type = TOKEN_ASSIGN; break;
        case '!': type = TOKEN_BANG; break;
        case '<': type = TOKEN_LESS; break;
        case '>': type = TOKEN_GREATER; break;
        case '(': type = TOKEN_LEFT_PAREN; break;
        case ')': type = TOKEN_RIGHT_PAREN; break;
        case '[': type = TOKEN_LEFT_BRACKET; break;
        case ']': type = TOKEN_RIGHT_BRACKET; break;
        case '{': type = TOKEN_LEFT_BRACE; break;
        case '}': type = TOKEN_RIGHT_BRACE; break;
        case ',': type = TOKEN_COMMA; break;
        case '.': type = TOKEN_DOT; break;
        case ':': type = TOKEN_COLON; break;
        default: return error_token(lexer, start, line, column);
    }
    return make_token(lexer, type, start, line, column);
}

/*
 * Called only while at_line_start is true. Consumes the leading whitespace and
 * decides whether the line begins a block (INDENT), ends one (DEDENTs), or
 * continues at the current level. Blank and comment-only lines never change
 * the indentation stack and are skipped entirely.
 */
static Token indentation_token(Lexer *lexer) {
    size_t start = lexer->position;
    size_t line = lexer->line;
    size_t column = lexer->column;

    unsigned int spaces = 0;
    while (peek_char(lexer) == ' ' || peek_char(lexer) == '\t') {
        spaces += advance(lexer) == '\t' ? 4U : 1U;
    }

    if (peek_char(lexer) == '#' || peek_char(lexer) == '\n' ||
        peek_char(lexer) == '\r' || at_end(lexer)) {
        while (!at_end(lexer) && peek_char(lexer) != '\n') {
            advance(lexer);
        }
        lexer->at_line_start = false;
        return lexer_next(lexer);
    }

    unsigned int current_indent = lexer->indent_stack[lexer->indent_depth];
    lexer->at_line_start = false;

    if (spaces > current_indent) {
        if (lexer->indent_depth + 1 >= FEM_MAX_INDENT_DEPTH) {
            return error_token(lexer, start, line, column);
        }
        lexer->indent_stack[++lexer->indent_depth] = spaces;
        return make_token(lexer, TOKEN_INDENT, start, line, column);
    }

    if (spaces < current_indent) {
        while (lexer->indent_depth > 0 &&
               spaces < lexer->indent_stack[lexer->indent_depth]) {
            lexer->indent_depth--;
            lexer->pending_dedents++;
        }
        if (spaces != lexer->indent_stack[lexer->indent_depth]) {
            return error_token(lexer, start, line, column);
        }
        if (lexer->pending_dedents > 0) {
            lexer->pending_dedents--;
            return make_token(lexer, TOKEN_DEDENT, start, line, column);
        }
    }

    return lexer_next(lexer);
}

void lexer_init(Lexer *lexer, const char *source, size_t length) {
    memset(lexer, 0, sizeof(*lexer));
    lexer->source = source;
    lexer->length = length;
    lexer->line = 1;
    lexer->column = 1;
    lexer->at_line_start = true;
}

Token lexer_next(Lexer *lexer) {
    if (lexer->pending_dedents > 0) {
        lexer->pending_dedents--;
        return make_token(lexer, TOKEN_DEDENT, lexer->position,
                          lexer->line, lexer->column);
    }

    if (lexer->at_line_start) {
        return indentation_token(lexer);
    }

    while (!at_end(lexer)) {
        char c = peek_char(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lexer);
            continue;
        }
        if (c == '#') {
            while (!at_end(lexer) && peek_char(lexer) != '\n') {
                advance(lexer);
            }
            continue;
        }
        break;
    }

    if (at_end(lexer)) {
        if (lexer->indent_depth > 0 && !lexer->emitted_eof) {
            lexer->emitted_eof = true;
            lexer->pending_dedents = lexer->indent_depth;
            lexer->indent_depth = 0;
            return lexer_next(lexer);
        }
        lexer->emitted_eof = true;
        return make_token(lexer, TOKEN_EOF, lexer->position,
                          lexer->line, lexer->column);
    }

    size_t start = lexer->position;
    size_t line = lexer->line;
    size_t column = lexer->column;
    char c = peek_char(lexer);

    if (c == '\n') {
        advance(lexer);
        return make_token(lexer, TOKEN_NEWLINE, start, line, column);
    }
    if (is_identifier_start(c)) {
        return identifier_token(lexer, start, line, column);
    }
    if (isdigit((unsigned char)c)) {
        return number_token(lexer, start, line, column);
    }
    if (c == '"') {
        return string_token(lexer, start, line, column);
    }
    return punctuation_token(lexer, start, line, column);
}
```

### src/parser.c

```c
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
```

### src/evaluator.c

```c
#include "evaluator.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Value constructors and helpers (declared in value.h). A VALUE_STRING owns a
 * malloc'd NUL-terminated buffer; every other type owns no memory. Constructors
 * never leak: on allocation failure a string value collapses to VALUE_NULL.
 */

Value value_null(void) {
    Value value = {VALUE_NULL, {.integer = 0}};
    return value;
}

Value value_bool(bool boolean) {
    Value value = {VALUE_BOOL, {.boolean = boolean}};
    return value;
}

Value value_int(int64_t integer) {
    Value value = {VALUE_INT, {.integer = integer}};
    return value;
}

Value value_float(double floating) {
    Value value = {VALUE_FLOAT, {.floating = floating}};
    return value;
}

static char *copy_string(const char *source) {
    size_t length = strlen(source);
    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, source, length + 1U);
    return copy;
}

Value value_string_copy(const char *source) {
    if (source == NULL) {
        return value_null();
    }
    char *copy = copy_string(source);
    if (copy == NULL) {
        return value_null();
    }
    Value value;
    value.type = VALUE_STRING;
    value.as.string = copy;
    return value;
}

Value value_clone(const Value *value) {
    if (value == NULL) {
        return value_null();
    }
    if (value->type == VALUE_STRING) {
        return value_string_copy(value->as.string);
    }
    /* Non-string types own no heap memory; a shallow copy is safe. */
    return *value;
}

void value_free(Value *value) {
    if (value == NULL) {
        return;
    }
    if (value->type == VALUE_STRING) {
        free(value->as.string);
    }
    value->type = VALUE_NULL;
    value->as.integer = 0;
}

bool value_truthy(const Value *value) {
    if (value == NULL) {
        return false;
    }
    switch (value->type) {
        case VALUE_NULL:
            return false;
        case VALUE_BOOL:
            return value->as.boolean;
        case VALUE_INT:
            return value->as.integer != 0;
        case VALUE_FLOAT:
            return value->as.floating != 0.0;
        case VALUE_STRING:
            return value->as.string != NULL && value->as.string[0] != '\0';
    }
    return false;
}

void print_value(const Value *value) {
    if (value == NULL) {
        fputs("null", stdout);
        return;
    }
    switch (value->type) {
        case VALUE_NULL:
            fputs("null", stdout);
            break;
        case VALUE_BOOL:
            fputs(value->as.boolean ? "true" : "false", stdout);
            break;
        case VALUE_INT:
            printf("%lld", (long long)value->as.integer);
            break;
        case VALUE_FLOAT:
            printf("%.17g", value->as.floating);
            break;
        case VALUE_STRING:
            putchar('"');
            if (value->as.string != NULL) {
                for (const char *p = value->as.string; *p != '\0'; p++) {
                    switch (*p) {
                        case '"': fputs("\\\"", stdout); break;
                        case '\\': fputs("\\\\", stdout); break;
                        case '\n': fputs("\\n", stdout); break;
                        case '\t': fputs("\\t", stdout); break;
                        case '\r': fputs("\\r", stdout); break;
                        default: putchar(*p); break;
                    }
                }
            }
            putchar('"');
            break;
    }
}

/*
 * Environment
 * -----------
 * Bindings are stored in a singly linked list; each entry owns its name copy
 * and its value copy. The first runtime error is recorded once in the
 * environment and reported through env_error().
 */

typedef struct Entry {
    char *name;
    Value value;
    bool mutable;
    struct Entry *next;
} Entry;

struct Environment {
    Entry *head;
    char error_message[256];
};

static void env_record_error(Environment *env, const char *message) {
    if (env == NULL || env->error_message[0] != '\0') {
        return;
    }
    snprintf(env->error_message, sizeof(env->error_message), "%s", message);
}

static void env_record_errorf(Environment *env, const char *format, const char *name) {
    if (env == NULL || env->error_message[0] != '\0') {
        return;
    }
    snprintf(env->error_message, sizeof(env->error_message), format, name);
}

static Entry *env_find(Environment *env, const char *name) {
    if (env == NULL || name == NULL) {
        return NULL;
    }
    for (Entry *entry = env->head; entry != NULL; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

Environment *env_new(void) {
    return calloc(1, sizeof(Environment));
}

void env_free(Environment *env) {
    if (env == NULL) {
        return;
    }
    Entry *entry = env->head;
    while (entry != NULL) {
        Entry *next = entry->next;
        free(entry->name);
        value_free(&entry->value);
        free(entry);
        entry = next;
    }
    free(env);
}

bool env_define(Environment *env, const char *name, const Value *value, bool mutable) {
    if (env == NULL || name == NULL) {
        return false;
    }

    char *name_copy = copy_string(name);
    if (name_copy == NULL) {
        env_record_error(env, "out of memory");
        return false;
    }

    Value value_copy = value_clone(value);
    if (value != NULL && value->type == VALUE_STRING && value_copy.type != VALUE_STRING) {
        free(name_copy);
        env_record_error(env, "out of memory");
        return false;
    }

    Entry *entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        value_free(&value_copy);
        free(name_copy);
        env_record_error(env, "out of memory");
        return false;
    }

    entry->name = name_copy;
    entry->value = value_copy;
    entry->mutable = mutable;
    entry->next = env->head;
    env->head = entry;
    return true;
}

Value *env_lookup(Environment *env, const char *name) {
    Entry *entry = env_find(env, name);
    if (entry == NULL) {
        return NULL;
    }
    return &entry->value;
}

bool env_assign(Environment *env, const char *name, const Value *value) {
    if (env == NULL || name == NULL || value == NULL) {
        return false;
    }

    Entry *entry = env_find(env, name);
    if (entry == NULL) {
        env_record_errorf(env, "cannot assign to undefined variable '%s'", name);
        return false;
    }
    if (!entry->mutable) {
        env_record_errorf(env, "cannot reassign immutable variable '%s'", name);
        return false;
    }

    Value copy = value_clone(value);
    if (value->type == VALUE_STRING && copy.type != VALUE_STRING) {
        env_record_error(env, "out of memory");
        return false;
    }

    value_free(&entry->value);
    entry->value = copy;
    return true;
}

const char *env_error(const Environment *env) {
    if (env == NULL || env->error_message[0] == '\0') {
        return NULL;
    }
    return env->error_message;
}

/*
 * Evaluator
 * ---------
 * Once an error is recorded, evaluation stops: every later eval() returns null
 * without side effects, so the program result is well-defined after failure.
 */

static bool int_add(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool int_subtract(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b)) {
        return false;
    }
    *out = a - b;
    return true;
}

static bool int_multiply(int64_t a, int64_t b, int64_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }
    if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN)) {
        return false;
    }
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) {
                return false;
            }
        } else if (b < INT64_MIN / a) {
            return false;
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b) {
                return false;
            }
        } else if (a < INT64_MAX / b) {
            return false;
        }
    }
    *out = a * b;
    return true;
}

static Value eval(Environment *env, const AstNode *node) {
    if (node == NULL || env_error(env) != NULL) {
        return value_null();
    }

    switch (node->type) {
        case AST_INTEGER:
            return value_int(node->as.integer);

        case AST_FLOAT:
            return value_float(node->as.floating);

        case AST_BOOLEAN:
            return value_bool(node->as.boolean);

        case AST_NULL:
            return value_null();

        case AST_STRING:
            return value_string_copy(node->as.string);

        case AST_IDENTIFIER: {
            Value *stored = env_lookup(env, node->as.identifier);
            if (stored == NULL) {
                env_record_errorf(env, "undefined variable '%s'", node->as.identifier);
                return value_null();
            }
            return value_clone(stored);
        }

        case AST_LET: {
            Value value = eval(env, node->as.declaration.value);
            if (!env_define(env, node->as.declaration.name, &value,
                            node->as.declaration.mutable)) {
                value_free(&value);
                return value_null();
            }
            return value;
        }

        case AST_ASSIGNMENT: {
            Value value = eval(env, node->as.assignment.value);
            if (!env_assign(env, node->as.assignment.name, &value)) {
                value_free(&value);
                return value_null();
            }
            return value;
        }

        case AST_RETURN:
            return eval(env, node->as.return_statement.value);

        case AST_EXPRESSION_STATEMENT:
            return eval(env, node->as.expression_statement.expression);

        case AST_PROGRAM:
        case AST_BLOCK: {
            Value last = value_null();
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                Value current = eval(env, node->as.block.statements.items[i]);
                value_free(&last);
                last = current;
            }
            return last;
        }

        case AST_IF: {
            Value condition = eval(env, node->as.if_statement.condition);
            bool is_truthy = value_truthy(&condition);
            value_free(&condition);
            return eval(env, is_truthy
                            ? node->as.if_statement.then_branch
                            : node->as.if_statement.else_branch);
        }

        case AST_UNARY: {
            Value operand = eval(env, node->as.unary.operand);

            if (node->as.unary.operator_type == TOKEN_BANG) {
                bool result = !value_truthy(&operand);
                value_free(&operand);
                return value_bool(result);
            }
            if (node->as.unary.operator_type == TOKEN_MINUS &&
                operand.type == VALUE_INT) {
                operand.as.integer = -operand.as.integer;
                return operand;
            }
            if (node->as.unary.operator_type == TOKEN_MINUS &&
                operand.type == VALUE_FLOAT) {
                operand.as.floating = -operand.as.floating;
                return operand;
            }

            value_free(&operand);
            return value_null();
        }

        case AST_BINARY: {
            Value left = eval(env, node->as.binary.left);
            Value right = eval(env, node->as.binary.right);
            TokenType op = node->as.binary.operator_type;

            if (op == TOKEN_EQUAL_EQUAL || op == TOKEN_BANG_EQUAL) {
                bool equal = false;
                if (left.type == right.type) {
                    switch (left.type) {
                        case VALUE_INT:
                            equal = left.as.integer == right.as.integer;
                            break;
                        case VALUE_FLOAT:
                            equal = left.as.floating == right.as.floating;
                            break;
                        case VALUE_BOOL:
                            equal = left.as.boolean == right.as.boolean;
                            break;
                        case VALUE_NULL:
                            equal = true;
                            break;
                        case VALUE_STRING:
                            equal = left.as.string != NULL &&
                                    right.as.string != NULL &&
                                    strcmp(left.as.string, right.as.string) == 0;
                            break;
                        default:
                            equal = false;
                            break;
                    }
                }
                value_free(&left);
                value_free(&right);
                return value_bool(op == TOKEN_EQUAL_EQUAL ? equal : !equal);
            }

            if (left.type == VALUE_STRING && right.type == VALUE_STRING &&
                op == TOKEN_PLUS) {
                size_t left_length = strlen(left.as.string);
                size_t right_length = strlen(right.as.string);
                char *joined = malloc(left_length + right_length + 1U);
                Value result = value_null();
                if (joined != NULL) {
                    memcpy(joined, left.as.string, left_length);
                    memcpy(joined + left_length, right.as.string, right_length);
                    joined[left_length + right_length] = '\0';
                    result.type = VALUE_STRING;
                    result.as.string = joined;
                } else {
                    env_record_error(env, "out of memory");
                }
                value_free(&left);
                value_free(&right);
                return result;
            }

            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                int64_t result = 0;
                bool ok = true;
                switch (op) {
                    case TOKEN_PLUS:
                        ok = int_add(left.as.integer, right.as.integer, &result);
                        break;
                    case TOKEN_MINUS:
                        ok = int_subtract(left.as.integer, right.as.integer, &result);
                        break;
                    case TOKEN_STAR:
                        ok = int_multiply(left.as.integer, right.as.integer, &result);
                        break;
                    case TOKEN_SLASH:
                        if (right.as.integer == 0) {
                            ok = false;
                        } else if (left.as.integer == INT64_MIN && right.as.integer == -1) {
                            ok = false;
                        } else {
                            result = left.as.integer / right.as.integer;
                        }
                        break;
                    case TOKEN_PERCENT:
                        if (right.as.integer == 0) {
                            ok = false;
                        } else if (left.as.integer == INT64_MIN && right.as.integer == -1) {
                            result = 0;
                        } else {
                            result = left.as.integer % right.as.integer;
                        }
                        break;
                    default:
                        ok = false;
                        break;
                }
                value_free(&left);
                value_free(&right);
                if (ok) {
                    return value_int(result);
                }
                env_record_error(env, "integer arithmetic error");
                return value_null();
            }

            if (left.type == VALUE_FLOAT && right.type == VALUE_FLOAT) {
                double result = 0.0;
                bool ok = true;
                switch (op) {
                    case TOKEN_PLUS:
                        result = left.as.floating + right.as.floating;
                        break;
                    case TOKEN_MINUS:
                        result = left.as.floating - right.as.floating;
                        break;
                    case TOKEN_STAR:
                        result = left.as.floating * right.as.floating;
                        break;
                    case TOKEN_SLASH:
                        result = left.as.floating / right.as.floating;
                        break;
                    default:
                        ok = false;
                        break;
                }
                value_free(&left);
                value_free(&right);
                return ok ? value_float(result) : value_null();
            }

            /* Unsupported operation or mixed operand types. */
            value_free(&left);
            value_free(&right);
            return value_null();
        }

        case AST_CALL:
            /* Call expressions are not executable in this milestone. */
            return value_null();

        case AST_LIST:
            return value_null();
    }

    return value_null();
}

Value eval_ast(Environment *env, const AstNode *node) {
    return eval(env, node);
}
```

### src/eval_main.c

```c
#include "evaluator.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

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
```

### src/parse_main.c

```c
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Development tool: parses a file and prints the resulting AST. Not part of
 * the normal build; kept for interactive debugging.
 */

static void print_indent(size_t depth) {
    for (size_t i = 0; i < depth; i++) {
        fputs("  ", stdout);
    }
}

static void print_ast(const AstNode *node, size_t depth) {
    if (node == NULL) {
        print_indent(depth);
        puts("(empty)");
        return;
    }

    print_indent(depth);
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            puts(node->type == AST_PROGRAM ? "PROGRAM" : "BLOCK");
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                print_ast(node->as.block.statements.items[i], depth + 1);
            }
            break;
        case AST_INTEGER:
            printf("INTEGER %lld\n", (long long)node->as.integer);
            break;
        case AST_FLOAT:
            printf("FLOAT %g\n", node->as.floating);
            break;
        case AST_STRING:
            printf("STRING \"%s\"\n", node->as.string);
            break;
        case AST_BOOLEAN:
            printf("BOOLEAN %s\n", node->as.boolean ? "true" : "false");
            break;
        case AST_NULL:
            puts("NULL");
            break;
        case AST_IDENTIFIER:
            printf("IDENTIFIER %s\n", node->as.identifier);
            break;
        case AST_LIST:
            puts("LIST");
            for (size_t i = 0; i < node->as.list.elements.count; i++) {
                print_ast(node->as.list.elements.items[i], depth + 1);
            }
            break;
        case AST_UNARY:
            printf("UNARY %s\n", token_type_name(node->as.unary.operator_type));
            print_ast(node->as.unary.operand, depth + 1);
            break;
        case AST_BINARY:
            printf("BINARY %s\n", token_type_name(node->as.binary.operator_type));
            print_ast(node->as.binary.left, depth + 1);
            print_ast(node->as.binary.right, depth + 1);
            break;
        case AST_CALL:
            puts("CALL");
            print_ast(node->as.call.callee, depth + 1);
            for (size_t i = 0; i < node->as.call.arguments.count; i++) {
                print_ast(node->as.call.arguments.items[i], depth + 1);
            }
            break;
        case AST_LET:
            printf("%s %s =\n", node->as.declaration.mutable ? "MUT" : "LET",
                   node->as.declaration.name);
            print_ast(node->as.declaration.value, depth + 1);
            break;
        case AST_ASSIGNMENT:
            printf("ASSIGN %s =\n", node->as.assignment.name);
            print_ast(node->as.assignment.value, depth + 1);
            break;
        case AST_EXPRESSION_STATEMENT:
            puts("EXPRESSION_STATEMENT");
            print_ast(node->as.expression_statement.expression, depth + 1);
            break;
        case AST_RETURN:
            puts("RETURN");
            print_ast(node->as.return_statement.value, depth + 1);
            break;
        case AST_IF:
            puts("IF");
            print_ast(node->as.if_statement.condition, depth + 1);
            print_ast(node->as.if_statement.then_branch, depth + 1);
            print_ast(node->as.if_statement.else_branch, depth + 1);
            break;
    }
}

static char *read_source_file(const char *path, size_t *length_out) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *source = malloc((size_t)size + 1U);
    if (source == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read_count = fread(source, 1, (size_t)size, file);
    fclose(file);
    if (read_count != (size_t)size) {
        free(source);
        return NULL;
    }
    source[size] = '\0';
    *length_out = (size_t)size;
    return source;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <source-file>\n", argv[0]);
        return 1;
    }

    size_t source_length = 0;
    char *source = read_source_file(argv[1], &source_length);
    if (source == NULL) {
        return 1;
    }

    Parser parser;
    parser_init(&parser, source, source_length);
    AstNode *program = parser_parse(&parser);
    if (program == NULL) {
        fprintf(stderr, "Parse error: %s\n", parser_error(&parser));
        free(source);
        return 1;
    }

    print_ast(program, 0);
    ast_free(program);
    free(source);
    return 0;
}
```

### tests/test_runner.c

```c
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

static RunResult run_source(const char *source) {
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

    out.result = eval_ast(env, program);
    const char *runtime_error = env_error(env);
    if (runtime_error != NULL) {
        snprintf(out.error, sizeof(out.error), "%s", runtime_error);
    }

    env_free(env);
    ast_free(program);
    return out;
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

/*
 * Native registry tests
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
    (void)argument_count;
    if (argument_count != 1 || arguments[0].type != VALUE_INT) {
        return value_null();
    }
    return value_int(arguments[0].as.integer * 2);
}

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

    /* Native registry */
    test_native_register_and_lookup();
    test_native_replace_existing_name();
    test_native_registry_growth();
    test_native_callback_invocation();
    test_native_cleanup_idempotent();

    /* Value ownership */
    test_value_ownership();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }

    puts("All FemLang tests passed.");
    return EXIT_SUCCESS;
}
```

### README.md

```markdown
# FemLang

FemLang is a small, expressive, Python-inspired programming language designed to be easy to learn, pleasant to use, and practical for real-world work. The goal is to blend a clean, intuitive syntax with a fast, portable runtime written in pure C11.

The project is intentionally inspired by the creativity, self-expression, and community spirit often associated with femboy culture, while keeping the language itself professional and broadly usable. The playful vocabulary is optional and should feel like a welcoming layer on top of a conventional, production-ready language core.

FemLang is best understood as a language with a strong identity, but not at the cost of clarity or engineering discipline.

## Core design philosophy

FemLang is designed around a few principles:

- simple and readable syntax
- safe, explicit behavior
- fast execution with a lightweight runtime
- easy interoperability with native C libraries
- optional playful aliases without compromising professional use
- a balance between aesthetic identity and technical seriousness

## Minimal first release

The first release is intentionally small but useful.

The minimal viable language should support:

- integers
- strings
- booleans
- lists
- functions
- conditionals
- loops
- native C function binding

This is enough to validate the core language model without prematurely committing to a large VM or a broad standard library.

The playful vocabulary remains optional:

```femlang
spark name = "Alex"
serve "Hello, " + name + "!"
```

The equivalent conventional version is:

```femlang
let name = "Alex"
return "Hello, " + name + "!"
```

This keeps the language welcoming without making it feel gimmicky or less suitable for serious software work.

## Example syntax

```femlang
let name = "femboy"
let age = 21
let cute = true

fn greet(person):
    if cute:
        return "Hello, " + person + " :3"
    else:
        return "Hello, " + person

let message = greet(name)
print(message)
```

## Planned architecture

The project is organized around a conventional compiler/runtime pipeline:

- Lexer
- Parser
- AST
- Bytecode compiler (planned)
- VM (planned)
- Native C integration
- runtime memory management

The current repository is intentionally focused on the lexer/token layer as the first important milestone.

## Project status

Current milestone:

- tokenizer foundation complete with indentation handling
- C11 lexer, AST, and parser implemented
- tree-walking evaluator with immutable (`let`) and mutable (`mut`) bindings
- safe variable reassignment (deep-copied values, error channel)
- ownership-safe native C function registry (see docs/ownership.md)
- command-line interpreter: `./femlang examples/basic.fem`
- automated regression test suite: `make test`

Planned next milestones:

- comparison operators in the evaluator
- function declarations and call expressions (wired to the native registry)
- loops, lists, and standard-library native functions
- bytecode compiler and VM

## Build and run

```bash
make
./femlang examples/basic.fem    # prints 30
make test                       # runs the regression suite
make clean                      # remove build artifacts
```

`make asan` builds with `-fsanitize=address,undefined`; the w64devkit toolchain
used on this machine does not ship the sanitizer runtimes, so build the normal
targets there (see docs/baseline-validation.md).

## Repository goals

FemLang is meant to be:

- approachable for newcomers
- expressive for small scripts and prototypes
- performant enough for real workloads
- friendly to C integration
- a creative but technically grounded language project

## License

MIT

## Notes

This repository is intentionally a foundational scaffold. The aim is to establish a sane design and a clean starting point before expanding into a larger language runtime.

The tone is friendly and expressive, but the implementation remains rooted in professional engineering practices.

---

FemLang is the kind of project that should feel fun without sacrificing correctness, clarity, or maintainability.

```

### docs/reassignment.md

```markdown
# Safe variable reassignment

FemLang distinguishes immutable and mutable bindings:

```femlang
let fixed = 1
mut score = 0
score = score + 1
score
```

Running this with `./femlang` prints `1`.

## Rules

- `let name = value` creates an **immutable** binding.
- `mut name = value` creates a **mutable** binding.
- An expression statement `name = value` reassigns the binding.

Reassignment is a statement, not an expression. It is rejected in two cases:

1. the name was never declared (`cannot assign to undefined variable 'x'`), or
2. the binding was declared with `let` (`cannot reassign immutable variable
   'x'`).

A failed reassignment never creates an implicit variable and never modifies the
stored value.

## How a successful reassignment works

The environment API (`env_assign`) performs, in order:

1. look up the binding; fail if missing or immutable;
2. clone the incoming value (deep copy for strings);
3. release the previously stored value;
4. install the clone.

Because the clone is completed before the old value is released, a failure can
never leave the binding in a freed or half-updated state. Strings stored in the
environment never share a buffer with expression temporaries.

## Error handling

The first failing operation records an error message in the environment (see
docs/ownership.md) and stops further evaluation. The interpreter prints it to
stderr and exits with a non-zero status:

```text
$ ./femlang bad.fem
Runtime error: cannot reassign immutable variable 'fixed'
```

## Validation

```bash
make clean
make
make test
```
```

### docs/baseline-validation.md

```markdown
# Baseline validation and native bindings

This milestone adds baseline regression checks for immutable and mutable
variables and introduces a small, ownership-safe C native function registry.

## Reassignment rules

```femlang
let fixed = 1
fixed = 2       # runtime error: cannot reassign immutable variable
                # (rejected by the environment API)

mut score = 1
score = score + 2
```

`let` bindings are immutable. `mut` bindings can be replaced. Values stored in
the environment are deep-copied, so strings never share ownership with
temporary expression results. See docs/ownership.md for the full ownership model
and docs/reassignment.md for the statement-level rules.

## Native registry

The first native-C layer is intentionally independent of the parser. It
provides a small registry that maps names to C callbacks:

```c
Value native_add(size_t argc, const Value *argv);

FemNativeRegistry registry;
native_registry_init(&registry);
native_register(&registry, "add", native_add);
```

Behavior:

- `native_register()` copies the name and can be called repeatedly; registering
  an existing name replaces the function and keeps the name copy.
- `native_lookup()` returns the stored callback, or NULL for unknown names.
- The registry grows by reallocation (initial capacity 8) without losing
  existing entries.
- NULL registry / name / function are rejected.
- `native_registry_free()` is idempotent.

The evaluator will consume this registry once call-expression parsing is
enabled. Keeping registration separate makes the current baseline easier to test
and avoids coupling C callbacks to unfinished function syntax.

## Validation commands

```bash
make clean
make
make test
```

This is what the current machine produces:

```text
All FemLang tests passed.
```

The repository Makefile also defines `make asan`, which passes
`-fsanitize=address,undefined` at both compile and link time. Note: the toolchain
used here (w64devkit w64-w64-mingw32 GCC) does not ship `libasan` or `libubsan`,
so the sanitizer link step fails with `cannot find -lasan` on this machine. On a
toolchain that provides the sanitizer runtimes (Linux/macOS GCC or Clang), the
same target builds and runs the test suite under the sanitizers. As a substitute
on this machine, the sources compile clean under `-fanalyzer` with no findings.

## Scope boundaries

Not yet implemented: function calls, `print`-style native invocation from FemLang
source, list literals, loops, and comparison operators `< <= > >=` in the
evaluator (they lex and parse, and evaluating them currently reports an
"integer arithmetic error"). Those are the next milestone.
```

### docs/testing.md

```markdown
# FemLang test suite

This directory contains the automated regression tests for the language core.
The runner is `tests/test_runner.c`, a single self-contained C program that
exercises the public lexer, parser, AST, evaluator, and native-registry
interfaces — the same interfaces future tools (such as a VS Code language
server) will use.

## Covered areas

### Lexer

- keywords, including the playful aliases (`spark` -> `let`, `serve` ->
  `return`, `slay` -> `break`, `skip` -> `continue`);
- identifiers, integer literals, float literals, string literals;
- unterminated strings (lexer error);
- operators and punctuation;
- newlines, indentation, nested indentation, dedentation, dedent at EOF;
- blank and comment-only lines;
- comment after code;
- inconsistent indentation is reported as a lexer error;
- missing final newline.

### Parser

- `let` (immutable) and `mut` (mutable) declarations;
- assignment statements;
- string literal content is decoded (quotes stripped, `\n` `\t` `\"` handled);
- arithmetic precedence, including multiplication before addition;
- identifier-led expression statements (`a * b + c` parses as `(a * b) + c`);
- assignment values parse with full expression precedence;
- `if` / `else` with blocks of multiple statements;
- invalid declarations (`let = 5`), missing value (`let x`), and invalid
  assignments (`x =`) are parse errors.

### Evaluator

- `let` and `mut` declarations;
- successful reassignment and arithmetic after reassignment;
- immutable reassignment is rejected and leaves the value untouched;
- assignment to an undefined variable is rejected and creates no implicit
  binding;
- repeated string reassignment and string equality/ownership (mutating one
  binding does not affect another);
- string concatenation;
- `if` / `else` branch selection;
- integer arithmetic, unary minus, precedence;
- division by zero reports an error;
- undefined variable lookup reports an error.

### Native registry

- register / lookup, unknown lookups return NULL;
- NULL registry, name, and function are rejected;
- registering an existing name replaces the function without changing the
  count;
- growth past the initial capacity (20 registrations);
- calling a registered callback through its signature;
- `native_registry_free()` is idempotent.

### Value ownership

- string copying, deep cloning (clone and original must not share a buffer);
- `value_free()` on strings, nulls, and already-freed values.

## Running the tests

From the repository root:

```bash
make test
```

Expected output:

```text
All FemLang tests passed.
```

Any failure prints the failing `CHECK` expression with its file and line and
causes a non-zero exit status.

## Sanitizers

```bash
make asan
```

builds and runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer.
The w64devkit toolchain used here lacks the sanitizer runtimes
(`cannot find -lasan`), so on this machine run the normal suite instead.
See docs/baseline-validation.md.
```

### docs/parser-roadmap.md

```markdown
# FemLang parser and evaluator milestone

This document summarizes the current milestone in development.

## What is now implemented

The repository includes the core pieces necessary for a minimal working language
prototype:

- lexer tokenization with indentation handling
- AST definitions
- parser for declarations, expression statements, assignments, and `if`/`else`
- runtime tree-walking evaluator
- command-line execution of a `.fem` file (`src/eval_main.c`)
- environment model with immutable/mutable bindings and an error channel
- ownership-safe native C function registry (`src/native.c`, `include/native.h`)

## Supported semantics

The interpreter currently supports:

- integer literals, float literals, strings, booleans, null
- `let` (immutable) and `mut` (mutable) declarations
- reassignment of `mut` bindings (deep-copied values)
- integer arithmetic with overflow and division-by-zero guards
- float + float arithmetic
- string concatenation and equality
- `if` / `else` control flow
- unary minus and logical negation
- error reporting through the environment error channel

## Example

```femlang
let x = 10
let y = 20
let total = x + y
total
```

Running `./femlang` on this file prints `30`.

## Not yet implemented

- function calls (including `print`; the evaluator has an `AST_CALL` placeholder)
- comparison operators `< <= > >=` (they lex and parse; evaluating them
  currently reports an "integer arithmetic error")
- lists, loops, `elif`, `try`/`catch`/`finally`, `match`

These are the next milestones. The native registry already exists so call
expressions can be wired to C callbacks without further registry changes.
```

### docs/control-flow.md

```markdown
# Control-flow milestone

FemLang has indentation-aware blocks and supports `if` / `else` statements in
the parser and tree-walking evaluator.

```femlang
let score = 10

if score == 10:
    "slay"
else:
    "try again"
```

Build and run:

```bash
make clean
make
./femlang examples/control_flow.fem
```

The lexer emits `INDENT` and `DEDENT` tokens. The parser represents blocks as
`AST_BLOCK` nodes and conditionals as `AST_IF` nodes. The evaluator executes
only the selected branch.

## Conditions

Any value can be a condition and is interpreted through `value_truthy()`:
integers and floats are false when zero, strings are false when empty, `false`
and `null` are false, everything else is true.

Note: comparison operators `< <= > >=` are lexed and parsed but not yet
evaluated; use `==` or `!=` for the current milestone. See
docs/parser-roadmap.md.

## Current limitations

This milestone does not yet implement `elif`, loops, functions, lists, or
native C function invocation from FemLang source. Reassignment of `mut`
bindings is implemented and covered in docs/reassignment.md.
```

### docs/ownership.md

```markdown
# Value, environment, and registry ownership in FemLang

This document describes the ownership model used across the C core. The rules
are designed so a failure never leaks memory and no two owners can ever free the
same pointer.

## `Value` ownership

A `Value` struct owns its own heap data and nothing else:

- `VALUE_STRING` owns a single NUL-terminated `char *` allocated with `malloc`.
  The pointer is non-NULL while the value is alive.
- Every other type (`VALUE_NULL`, `VALUE_BOOL`, `VALUE_INT`, `VALUE_FLOAT`) owns
  no heap memory.

Guarantees:

- `value_clone()` deep-copies strings and shallow-copies non-string values.
- `value_free()` releases the owned string and resets the value to `VALUE_NULL`.
  It is a no-op on an already-freed value, so freeing a value twice is safe.
- If a string copy cannot be allocated, `value_string_copy()` and
  `value_clone()` return `VALUE_NULL` instead of a partial value. Callers that
  asked for a string and received `VALUE_NULL` must treat that as an allocation
  failure and must not reuse the returned value as a real (empty) string.

## Environment ownership

Each binding in the environment owns two heap objects:

1. a deep copy of the binding name (a `char *`);
2. a deep copy of the binding value (`value_clone()` semantics above).

API contract:

- `env_define(env, name, &value, mutable)` copies both the name and the value.
  The caller keeps ownership of its `value` argument and may free it afterwards.
- `env_lookup(env, name)` returns a **borrowed** pointer into the environment.
  It is valid only until that binding is reassigned or the environment is freed;
  callers must clone before storing a lookup result.
- `env_assign(env, name, &value)` clones the incoming value first, then releases
  the previous stored value, then installs the clone. If the binding is missing
  or immutable, or the clone fails, the stored value is left untouched. This
  ordering guarantees `entry->value` is never left in a freed state.

## Failure and error recording

The first semantic failure is recorded once in the environment:

- assigning to an undefined variable: `cannot assign to undefined variable 'x'`
- assigning to an immutable binding: `cannot reassign immutable variable 'x'`
- reading an undefined variable: `undefined variable 'x'`
- allocation failure: `out of memory`
- integer overflow, division by zero, or `INT64_MIN / -1`:
  `integer arithmetic error`

After an error is recorded, every later evaluation returns `VALUE_NULL`
immediately, so execution does not continue past the first failure. The running
program result is therefore well-defined (null plus one error message), and can
never silently run with a stale half-updated state.

## Native registry ownership

`FemNativeRegistry` owns a growable array of `{ name, function }` bindings:

- `native_register()` copies the name; the registry owns the copy and frees it
  in `native_registry_free()`.
- Registering an existing name replaces only the function pointer; the original
  name copy is kept, so the registry count never grows on replacement.
- NULL registry, NULL name, and NULL function pointer are all rejected and
  return `false`.
- `native_registry_free()` releases every copied name and re-initializes the
  registry to the empty state, so calling it twice is safe.
```

### examples/basic.fem

```femlang
let x = 10
let y = 20
let total = x + y
total
```

### examples/control_flow.fem

```femlang
let score = 10

if score == 10:
    "slay"
else:
    "try again"
```
