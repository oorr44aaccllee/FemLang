# Value, environment, and registry ownership in FemLang

This document describes the ownership model used across the C core. The rules
are designed so a failure never leaks memory and no two owners can ever free the
same pointer.

## `Value` ownership

A `Value` struct owns its own heap data and nothing else:

- `VALUE_STRING` owns a single NUL-terminated `char *` allocated with `malloc`.
  The pointer is non-NULL while the value is alive.
- `VALUE_ERROR` owns its message exactly like `VALUE_STRING` owns its bytes
  (malloc'd, NUL-terminated `char *`). It is a transient control value: native
  callbacks return it to report a runtime error, and the evaluator converts a
  returned `VALUE_ERROR` into the environment error channel (see
  "Call-expression ownership" below). Production code never stores a
  `VALUE_ERROR`; `value_clone()` deep-copies the message and `value_free()`
  releases it, so ownership for it follows the string rules.
- `VALUE_FN` owns a `FemFunction` struct (see `include/value.h`) whose name
  and parameter array are heap-owned. The body AST node is **borrowed** (it
  lives as long as the parsed program), and the closure environment is
  **retained**: `value_function()` deep-copies the name and parameters and
  calls `env_retain()` on the closure. `value_clone()` duplicates name and
  parameters but shares the body node, retaining the closure again; copies are
  therefore independent except for the shared, immutable body.
- `VALUE_NATIVE` stores a **borrowed** `FemNativeFunction` pointer (a native
  callback registered in the environment's registry). The value does not own
  it: `value_clone()` copies the pointer, `value_free()` only resets the value
  to `VALUE_NULL`, and the callback stays owned by whoever registered it.
- Every other type (`VALUE_NULL`, `VALUE_BOOL`, `VALUE_INT`, `VALUE_FLOAT`)
  owns no heap memory.

Guarantees:

- `value_clone()` deep-copies strings, error messages, and function values
  and shallow-copies non-heap-owning values.
- `value_free()` releases the owned string/message and resets the value to
  `VALUE_NULL`. It is a no-op on an already-freed value, so freeing a value
  twice is safe.
- If a string copy cannot be allocated, `value_string_copy()` and
  `value_clone()` return `VALUE_NULL` instead of a partial value. Callers that
  asked for a string and received `VALUE_NULL` must treat that as an allocation
  failure and must not reuse the returned value as a real (empty) string.
- `value_to_string(value)` returns the display form of any value as a **new
  owned string**: strings are their raw bytes (no quotes, distinct buffer),
  every other type is its textual representation, and `NULL` renders as
  `"null"`. It follows the same convention: allocation failure yields
  `VALUE_NULL`.

## Environment ownership

Each binding in the environment owns two heap objects:

1. a deep copy of the binding name (a `char *`);
2. a deep copy of the binding value (`value_clone()` semantics above).

Environments form a **parent chain** and are **reference-counted**:

- `env_new()` creates a root with one reference held by the host. `env_free()`
  is the host's release; `env_retain()`/`env_release()` transfer shared
  ownership.
- A function call frame is an environment whose parent is the called function's
  closure environment; the frame retains its parent, and a `VALUE_FN` retains
  its closure. Frames are transient: `eval_function_call()` releases the frame
  when the call returns.
- When the last reference drops, `env_destroy()` releases every binding
  (deep-freed values, releasing any `VALUE_FN` closures) and then releases the
  parent chain upward. `env_release()` is a no-op at zero references, which is
  what makes weakly-stored function copies safe to free during destruction
  (see below).
- **Weak bindings.** Storing a function value inside the very environment it
  closes over — the idiomatic self- or sibling-recursive definition — installs
  a deep copy of the function **without** `env_retain()`. `env_define()` and
  `env_assign()` detect `value.fn->closure == env` and use the weak path; every
  other store retains. Weak storage breaks the would-be reference cycles of
  recursive definitions so an environment can be freed when its last external
  reference drops.
- **Known limitation.** A cycle that spans two *different* environments (A
  stores a function closing over B while B stores a function closing over A,
  and neither is the weak self-closure case) is not collected by the
  reference counter. The idiomatic patterns (self recursion, escaping
  closures) do not create such cycles, and the limitation is accepted rather
  than solving it with a garbage collector.

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
  `integer arithmetic error`, `division by zero`
- ordering comparisons on non-numbers: `comparison requires two numbers`
- applying a binary operator to incompatible operands:
  `cannot apply operator '<op>' to these values`
- calling a non-function value: `attempt to call a non-function value`
- calling a user function with the wrong argument count:
  `function 'name' expects 2 arguments, got 1`
- `return` used outside any function: `return outside a function`
- recursion past the depth guard: `recursion limit exceeded`
- any message returned by a native as a `VALUE_ERROR` (for example the
  standard library's `len() expects a string`)

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

## Call-expression ownership

Each environment owns a `FemNativeRegistry` (exposed via
`env_native_registry()`); `env_free()` releases it. Identifier resolution
checks user bindings first, then the registry: a registered name resolves to a
`VALUE_NATIVE` holding the borrowed callback, and a `let` binding shadows a
native with the same name.

`AST_CALL` evaluation:

1. the callee is evaluated and must be a `VALUE_NATIVE` (a registered native)
   or a `VALUE_FN` (a user function); otherwise `attempt to call a
   non-function value` is recorded;
2. every argument is evaluated left to right into a temporary array that the
   call owns for the duration of the call;
3. for a `VALUE_NATIVE` callee, the callback receives `(count, args)` and must
   treat the array as borrowed — the evaluator frees each argument value
   (deep-freed for strings) and the array right after the call returns; the
   callback owns only the `Value` it returns;
4. for a `VALUE_FN` callee, the evaluator opens a frame environment parented to
   the callee's closure, binds each parameter as an immutable copy of the
   corresponding argument, evaluates the body, and returns the body's `return`
   value (or `null`); the frame is released when the call returns;
5. if any argument evaluation fails, already-evaluated arguments are released
   and the call returns `VALUE_NULL`;
6. if the call returns a `VALUE_ERROR`, the evaluator records its message
   through `env_record_error()`, releases the error value, and returns
   `VALUE_NULL` — so a native error behaves like any other runtime error and
   the error value is never leaked or stored.