# FemLang test suite

This directory contains the first automated regression tests for the language core.

## Covered areas

### Lexer

The lexer tests verify:

- keywords;
- identifiers;
- integer literals;
- operators;
- colons;
- newlines;
- indentation;
- dedentation;
- end-of-file handling.

### Parser

The parser tests verify:

- `if` / `else` AST construction;
- block nodes;
- nested expression structure;
- arithmetic precedence, including multiplication before addition.

### Evaluator

The evaluator tests verify:

- true conditional branches;
- false conditional branches;
- variable lookup;
- integer arithmetic;
- final program results.

## Running the tests

From the repository root:

```bash
make test
```

Expected output:

```text
All FemLang tests passed.
```

For AddressSanitizer and UndefinedBehaviorSanitizer, use:

```bash
make asan
```

The test runner intentionally uses the existing public lexer, parser, AST, and evaluator interfaces. This keeps the tests close to the way future tools, such as the VS Code language server, will consume the language implementation.
