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