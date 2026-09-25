# Value, environment, and registry ownership in FemLang

This document describes the ownership model used across the C core. The rules
are designed so a failure never leaks memory and no two owners can ever free the
same pointer.

## `Value` ownership

A `Value` struct owns its own heap data and nothing else:

- `VALUE_STRING` owns a single NUL-terminated `char *` allocated with `malloc`.
  The pointer is non-NULL while the value is alive.
- Every other type (`VALUE_NULL`, `VALUE_BOOL`, `VALUE_INT`, `VALUE_FLOAT`) owns
  no heap memory.

Guarantees:

- `value_clone()` deep-copies strings and shallow-copies non-string values.
- `value_free()` releases the owned string and resets the value to `VALUE_NULL`.
  It is a no-op on an already-freed value, so freeing a value twice is safe.
- If a string copy cannot be allocated, `value_string_copy()` and
  `value_clone()` return `VALUE_NULL` instead of a partial value. Callers that
  asked for a string and received `VALUE_NULL` must treat that as an allocation
  failure and must not reuse the returned value as a real (empty) string.

## Environment ownership

Each binding in the environment owns two heap objects:

1. a deep copy of the binding name (a `char *`);
2. a deep copy of the binding value (`value_clone()` semantics above).

API contract:

- `env_define(env, name, &value, mutable)` copies both the name and the value.
  The caller keeps ownership of its `value` argument and may free it afterwards.
- `env_lookup(env, name)` returns a **borrowed** pointer into the environment.
  It is valid only until that binding is reassigned or the environment is freed;
  callers must clone before storing a lookup result.
- `env_assign(env, name, &value)` clones the incoming value first, then releases
  the previous stored value, then installs the clone. If the binding is missing
  or immutable, or the clone fails, the stored value is left untouched. This
  ordering guarantees `entry->value` is never left in a freed state.

## Failure and error recording

The first semantic failure is recorded once in the environment:

- assigning to an undefined variable: `cannot assign to undefined variable 'x'`
- assigning to an immutable binding: `cannot reassign immutable variable 'x'`
- reading an undefined variable: `undefined variable 'x'`
- allocation failure: `out of memory`
- integer overflow, division by zero, or `INT64_MIN / -1`:
  `integer arithmetic error`

After an error is recorded, every later evaluation returns `VALUE_NULL`
immediately, so execution does not continue past the first failure. The running
program result is therefore well-defined (null plus one error message), and can
never silently run with a stale half-updated state.

## Native registry ownership

`FemNativeRegistry` owns a growable array of `{ name, function }` bindings:

- `native_register()` copies the name; the registry owns the copy and frees it
  in `native_registry_free()`.
- Registering an existing name replaces only the function pointer; the original
  name copy is kept, so the registry count never grows on replacement.
- NULL registry, NULL name, and NULL function pointer are all rejected and
  return `false`.
- `native_registry_free()` releases every copied name and re-initializes the
  registry to the empty state, so calling it twice is safe.