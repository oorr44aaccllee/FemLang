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