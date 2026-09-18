# FemLang roadmap

This document summarizes the short-term direction for the language and the current repository milestone.

## Current focus

The repo currently contains the core token + lexer foundation in C11, with a simple command-line tool that reads a source file and prints the produced tokens.

This is the first and most important engineering milestone because the parser and runtime will depend on the token stream being reliable.

## Minimal first release target

The minimal feature set for the first usable milestone is intentionally simple but practical:

- integers
- strings
- booleans
- lists
- functions
- conditionals
- loops
- native C binding

These features are enough to validate the language design while still supporting real scripts and tooling experiments.

## Optional playful vocabulary

The language should be welcoming and expressive without becoming unserious or hard to maintain.

Optional aliases could include:

- `spark` as `let`
- `serve` as `return`
- `slay` as `break`
- `skip` as `continue`

These should be implemented as optional syntax sugar or purely lexical aliases. The core language should remain conventional and easy to reason about.

## Near-term roadmap

1. Finish tokenizer tests and diagnostics
2. Add parser and AST definitions
3. Build a tree-walking evaluator for basic semantics
4. Define the bytecode format and compiler pipeline
5. Implement a lightweight VM
6. Add native C registration API
7. Introduce a small standard library
8. Add examples and documentation

## Design principle

FemLang should feel like a language that is both expressive and honest: playful in spirit, professional in structure.

That balance is the heart of the project.
