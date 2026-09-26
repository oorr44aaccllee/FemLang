# Control-flow milestone

FemLang has indentation-aware blocks and supports `if` / `elif` / `else`
statements in the parser and tree-walking evaluator.

```femlang
fn classify(n):
    if n < 0:
        return -1
    elif n == 0:
        return 0
    else:
        return 1
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

## `elif`

`elif` chains onto an `if` and stands for "else if": each condition is tried in
order until one is true, and only that branch runs. It is parsed as a nested
`AST_IF` in the `else` slot, so conditions are **short-circuited** — a later
`elif` (or `else`) is never evaluated once an earlier branch was taken. Any
number of `elif` branches may follow, and the final `else` remains optional.

```femlang
let score = 10

if score == 10:
    "slay"
else:
    "try again"
```

An `elif` or `else` without a preceding `if` is a parse error
(`elif without a matching if` / `else without a matching if`).

## Conditions

Any value can be a condition and is interpreted through `value_truthy()`:
integers and floats are false when zero, strings are false when empty, `false`
and `null` are false, everything else is true.

Comparison operators `< <= > >=` are fully evaluated on two integers or two
floats and produce a boolean; comparing non-numbers reports
`comparison requires two numbers`. `==` / `!=` compare any two values of the
same type (different types are simply not equal).

## Current limitations

This milestone does not yet implement loops. User-defined functions
(`fn name(params):`) with recursion and closures are implemented
(docs/functions.md), as is native C function invocation from FemLang source
(`f(...)`, including the bundled `print` native); see docs/ownership.md for
argument ownership. Reassignment of `mut` bindings is implemented and covered
in docs/reassignment.md.