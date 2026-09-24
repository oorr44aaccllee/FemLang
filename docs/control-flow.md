# Control-flow milestone

FemLang now has indentation-aware blocks and supports `if` / `else` statements in the parser and tree-walking evaluator.

```femlang
let score = 10

if score >= 10:
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

The lexer emits `INDENT` and `DEDENT` tokens. The parser represents blocks as `AST_BLOCK` nodes and conditionals as `AST_IF` nodes. The evaluator executes only the selected branch.

## Current limitations

This milestone does not yet implement `elif`, loops, reassignment, functions, lists, or native C functions. Those are the next runtime features after validating indentation and conditional execution.
