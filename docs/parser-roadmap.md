# FemLang parser and evaluator milestone

This document summarizes the current milestone in development.

## What is now implemented

The repository includes the core pieces necessary for a minimal working language prototype:

- lexer tokenization
- AST definitions
- parser for simple declarations and expressions
- runtime evaluator for basic arithmetic and values
- command-line execution of a `.fem` file
- environment model for variable storage

## Minimal supported semantics

The interpreter currently supports:

- integer literals
- floating-point literals
- strings
- booleans
- `let` / `mut` declarations
- arithmetic operations
- comparisons
- string concatenation
- basic program evaluation

## Example

```femlang
let x = 10
let y = 20
let total = x + y
print(total)
```

This should yield the value `30` once the runtime supports `print` as a native function.

## Planned next step

The immediate next milestone is to expand the language with:

- real `if` / `else` blocks
- function declarations and function calls
- loop support
- native C function registration
- a bytecode compiler and VM

This keeps the design honest and incremental, while still giving FemLang a coherent path toward a usable runtime.
