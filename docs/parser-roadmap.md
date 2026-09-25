# FemLang parser and evaluator milestone

This document summarizes the current milestone in development.

## What is now implemented

The repository includes the core pieces necessary for a minimal working language
prototype:

- lexer tokenization with indentation handling
- AST definitions
- parser for declarations, expression statements, assignments, calls,
  and `if`/`else`
- runtime tree-walking evaluator
- command-line execution of a `.fem` file (`src/eval_main.c`)
- environment model with immutable/mutable bindings and an error channel
- ownership-safe native C function registry (`src/native.c`, `include/native.h`)
- call expressions (`f(...)`) evaluated through the native registry, bundled
  `print` native

## Supported semantics

The interpreter currently supports:

- integer literals, float literals, strings, booleans, null
- `let` (immutable) and `mut` (mutable) declarations
- reassignment of `mut` bindings (deep-copied values)
- integer arithmetic with overflow and division-by-zero guards
- float arithmetic, including remainder (`%`, truncated like C)
- string concatenation and equality
- ordered comparisons `< <= > >=` on numbers (booleans; other types are a
  runtime error)
- `==` / `!=` on any two values of the same type (different types are unequal)
- native function call expressions with own-value argument passing
- `if` / `else` control flow
- unary minus and logical negation
- error reporting through the environment error channel

## Example

```femlang
let x = 10
let y = 20
let total = x + y
print(total)
```

Running `./femlang` on this file prints `30` (via `print`).

See also: `examples/calls.fem` (calls, comparisons, remainder).

## Not yet implemented

- user-defined functions (`fn`): `TOKEN_FN` is lexed but has no AST or
  evaluator support yet; only native callbacks are callable today
- lists, loops, `elif`, `try`/`catch`/`finally`, `match`
- string and list indexing

These are the next milestones. `fn` definitions build directly on the
call-expression machinery landed with the native registry.