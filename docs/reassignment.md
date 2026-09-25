# Safe variable reassignment

FemLang distinguishes immutable and mutable bindings:

```femlang
let fixed = 1
mut score = 0
score = score + 1
score
```

Running this with `./femlang` prints `1`.

## Rules

- `let name = value` creates an **immutable** binding.
- `mut name = value` creates a **mutable** binding.
- An expression statement `name = value` reassigns the binding.

Reassignment is a statement, not an expression. It is rejected in two cases:

1. the name was never declared (`cannot assign to undefined variable 'x'`), or
2. the binding was declared with `let` (`cannot reassign immutable variable
   'x'`).

A failed reassignment never creates an implicit variable and never modifies the
stored value.

## How a successful reassignment works

The environment API (`env_assign`) performs, in order:

1. look up the binding; fail if missing or immutable;
2. clone the incoming value (deep copy for strings);
3. release the previously stored value;
4. install the clone.

Because the clone is completed before the old value is released, a failure can
never leave the binding in a freed or half-updated state. Strings stored in the
environment never share a buffer with expression temporaries.

## Error handling

The first failing operation records an error message in the environment (see
docs/ownership.md) and stops further evaluation. The interpreter prints it to
stderr and exits with a non-zero status:

```text
$ ./femlang bad.fem
Runtime error: cannot reassign immutable variable 'fixed'
```

## Validation

```bash
make clean
make
make test
```