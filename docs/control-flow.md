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