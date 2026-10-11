# FemLang parser and evaluator milestone

This document summarizes the current milestone in development.

## What is now implemented

The repository includes the core pieces necessary for a minimal working language
prototype:

- lexer tokenization with indentation handling
- AST definitions
- parser for declarations, expression statements, assignments, calls,
  list literals, indexing, `if`/`elif`/`else`, `while`, `for ... in ...`,
  `break`, and `continue`
- runtime tree-walking evaluator
- command-line execution of a `.fem` file (`src/eval_main.c`)
- environment model with immutable/mutable bindings and an error channel
- ownership-safe native C function registry (`src/native.c`, `include/native.h`)
- call expressions (`f(...)`) evaluated through the native registry,
  bundled `print` native
- native standard library (`src/builtins.c`, `include/builtins.h`):
  `print`, `len`, `int`, `float`, `str`, `type`, with `VALUE_ERROR` reporting
- user-defined functions (`fn name(params):` blocks) with local frames,
  recursion under a depth guard, and closures (see `docs/functions.md`)
- ordered lists (`[..]` literals), `xs[i]` indexing (lists and strings),
  structural list equality, and `for x in xs:` iteration (see `docs/lists.md`)

## Supported semantics

The interpreter currently supports:

- integer literals, float literals, strings, booleans, null, lists
- `let` (immutable) and `mut` (mutable) declarations
- reassignment of `mut` bindings (deep-copied values)
- integer arithmetic with overflow and division-by-zero guards
- float arithmetic, including remainder (`%`, truncated like C)
- string concatenation and equality
- ordered comparisons `< <= > >=` on numbers (booleans; other types are a
  runtime error)
- `==` / `!=` on any two values of the same type (different types are unequal;
  functions and natives never compare equal)
- native function call expressions with own-value argument passing
- user-defined function calls, with lexical closures and local environments;
  `return` (early or implicit null) and `return` outside a function is an error
- `if` / `elif` / `else` control flow (elif short-circuits: later conditions
  are only reached when earlier ones were false)
- loops: `while` condition loops, `for i in low..high` integer ranges
  (upper bound exclusive, descending when `low > high`), and `break` /
  `continue` (with the `slay` / `skip` aliases); `return` from inside a loop
  exits the loop and the function
- lists: `[..]` literals (heterogeneous, nestable, trailing comma allowed),
  `xs[i]` indexing of lists and strings (bounds-checked), structural `==` /
  `!=`, truthiness by length, and `for x in xs:` iteration
- unary minus and logical negation
- error reporting through the environment error channel

## Example

```femlang
fn add(a, b):
    return a + b

let x = 10
let y = 20
print(add(x, y))
```

Running `./femlang` on this file prints `30` (via `print`).

See also:
- `examples/calls.fem` (calls, comparisons, remainder);
- `examples/stdlib.fem` (standard library tour);
- `examples/functions.fem` (functions, recursion, closures);
- `examples/loops.fem` (while, for ranges, break/continue);
- `examples/lists.fem` (list literals, indexing, for-in, equality);
- `docs/stdlib.md` for the native standard library reference,
  `docs/functions.md` for the function and closure reference,
  `docs/loops.md` for the loop reference, and `docs/lists.md` for lists.

## Not yet implemented

- `try`/`catch`/`finally`, `match`

These are the next milestones. Functions build directly on the
call-expression machinery landed with the native registry; the standard
library is itself native callbacks and can be supplemented by
`fn`-based library code going forward.