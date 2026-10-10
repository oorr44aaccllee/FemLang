# FemLang test suite

This directory contains the automated regression tests for the language core.
The runner is `tests/test_runner.c`, a single self-contained C program that
exercises the public lexer, parser, AST, evaluator, native-registry, and
standard-library (builtins) interfaces �?" the same interfaces future tools
(such as a VS Code language server) will use.

## Covered areas

### Lexer

- keywords, including the playful aliases (`spark` -> `let`, `serve` ->
  `return`, `slay` -> `break`, `skip` -> `continue`, `uwu` -> `fn`, `owo` ->
  `if`, `nya` -> `else`, `rawr` -> `while`);
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
- `elif` chains: `if`/`elif`/`else`, multiple `elif`s, `elif` without `else`,
  and stray `elif`/`else` without a preceding `if` is a parse error;
- call expressions: empty and argument lists, argument expressions, calls as
  operands (precedence), nested calls (`f(g(2))(3)`), calls in declarations,
  and a missing closing paren is a parse error;
- function definitions: name and parameter list shape, the zero-parameter
  form, and error cases (missing name, missing `(`, non-identifier parameter,
  missing block);
- loops: `while` shape, `for i in low..high` shape (integer and identifier
  bounds), `break`/`continue` statements inside a loop body, and error cases
  (`while` without a body or colon, `for` without a variable, `in`, `..`, or
  `:`);
- list literals (empty, heterogeneous, nested, trailing comma) and the missing
  `]` error;
- index expressions: shape, index precedence over `+`, `f(x)[0]` and
  `m[0][1]` chains, and the missing `]` error;
- `for x in xs:` list mode parses as `AST_FOR` with no upper bound;
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
- `if` / `elif` / `else` chains: first true branch wins across any number of
  `elif`s, `else` runs when none match, no branch is taken when there is no
  `else` (result is null), and unreached `elif` conditions are not evaluated
  (short-circuit), including inside function bodies;
- integer arithmetic, unary minus, precedence;
- division by zero reports an error (integer and float);
- undefined variable lookup reports an error;
- native call expressions: basic, nested, in expressions and declarations,
  calling a native stored in a binding, empty argument lists, string
  arguments, calling an undefined name or a non-function value, and argument
  evaluation errors stop the call;
- ordered comparisons `< <= > >=` on integers/floats (with non-numbers and
  mixed types reported as errors);
- float remainder (`%`) and float division-by-zero guards;
- mixed-type binary operations report an error instead of silently returning
  null;
- `VALUE_ERROR` propagation: a native returning `value_error("boom")` stops the
  program with that message through the error channel, even when the call is
  an operand of a larger expression;
- user-defined functions: basic calls, local scope isolation, implicit (null)
  and early `return`, `return` inside `if`, calls inside expressions and
  nested calls, user functions calling natives (including surfaced native
  errors), recursion (fib, deep-but-legal recursion at depth 200, mutual
  recursion), the recursion guard (`recursion limit exceeded`), arity errors,
  `return outside a function`, immutable function bindings and parameters,
  closures (capture by variable, shared mutable state, escaping closures with
  independent state, `make_counter`, higher-order functions and function
  composition), functional value flow (`let g = f; g()`), and re-declaring a
  function name;
- loops: `while` iteration counting, `for` sums (ascending and descending
  ranges, empty ranges), `break` from `while` and `for`, `continue` skipping
  odd/even iterations, nested loops (inner `break` exits only the inner
  loop), `return` from inside a loop inside a function, loop variable binding
  (remains bound after the loop, rebinding it does not disturb the counter),
  and the runtime errors (`break outside a loop`, `continue outside a
  loop`, `for range requires two integers`);
- lists: literals (empty, heterogeneous, nested) and nested evaluation,
  indexing (in place, through a binding, nested `m[1][0]`, and string indexing
  to a one-character string), index errors (out of bounds, negative,
  non-integer index, and non-indexable operands), `for x in xs:` iteration
  (sum, `break`, `continue`, binding retention, empty list, nested lists, and
  the non-list error), structural equality (equal, unequal, nested, differing
  lengths, and cross-type), and truthiness (empty false, non-empty true).

### Standard library

- `len`: byte length of strings (including the empty string) and element
  count of lists; arity and type errors (`len(42)`, `len()`);
- `int`: float truncation toward zero, bool conversion, integer passthrough;
  non-numeric, wrong-arity, and out-of-range arguments are errors;
- `float`: integer/bool/float conversion; type and arity errors;
- `str`: display form of integers, floats, booleans, null, and identity for
  strings;
- `type`: `"int"`, `"float"`, `"bool"`, `"string"`, `"list"`, `"null"`,
  `"native"`, and `"fn"` for a user-defined function value; `str(f)` renders
  `<fn 'name'>`; `print(f)` displays the same; arity errors;
- `print`: success paths (zero or more arguments, mixed types) return null
  with no error.

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
- `value_free()` on strings, nulls, and already-freed values;
- native values: borrowed function pointer, shallow clone, free resets to null;
- error values: owned message, deep clone (distinct buffers), free resets to
  null;
- function values: `value_function()` deep-copies name/parameters, borrows the
  body, retains the closure; clones duplicate name/parameters (independent
  buffers) while sharing the body and re-retaining the closure; invalid inputs
  (NULL name or closure) yield `VALUE_NULL`; `value_to_string()` renders
  `<fn 'name'>`;
- list values: `value_list()` deep-copies every element (strings have distinct
  buffers, integers compare equal), clones own independent element arrays,
  `value_free()` releases elements, empty lists are valid, and
  `value_to_string()` renders `[1, a, true]` with raw string elements and
  recursively for nested lists;
- `value_to_string()`: display form of every value type, including raw string
  identity (distinct buffer) and the null-argument case.

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