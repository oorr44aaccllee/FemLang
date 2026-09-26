#ifndef FEMLANG_EVALUATOR_H
#define FEMLANG_EVALUATOR_H

#include "ast.h"
#include "native.h"
#include "value.h"

#include <stdbool.h>

typedef struct Environment Environment;

/*
 * Environment stores named bindings:
 *   char *name;    heap copy, owned by the environment
 *   Value value;   heap-cloned on define and assign (see value.h)
 *   bool mutable;
 *
 * Environments form a parent chain: env_new() creates a root, function calls
 * create frame environments whose parent is the called function's closure
 * environment. Identifier resolution, assignment, and native lookup walk the
 * chain up to the root, so a function sees the bindings of every scope it was
 * defined inside (lexical scoping).
 *
 * Ownership: environments are reference-counted. env_new() returns a root
 * with one reference held by the host; env_retain()/env_release() transfer
 * shared ownership (frames retain their parent, function values retain their
 * closure). env_free() is the host's env_release(): when the last reference
 * drops the environment is destroyed, releasing its bindings. Storing a
 * function value inside the very environment it closes over is a weak
 * binding (no retain) so that idiomatic self/sibling recursive definitions
 * do not create reference cycles.
 *
 * Each environment owns a FemNativeRegistry; only the root's is usually
 * populated. Identifier resolution checks user bindings first and the native
 * registry second (walking the chain), so a `let` binding shadows a native
 * function with the same name. env_native_registry() exposes the registry of
 * a root environment so the host program can register native functions (e.g.
 * print) before evaluation.
 *
 * env_define() clones both the name and the value; the caller keeps ownership
 * of its value argument. env_lookup() returns a borrowed pointer into the
 * environment that is valid only until that binding is reassigned or the
 * environment is freed; callers must clone before storing or mutating.
 *
 * env_assign() clones the incoming value, releases the previous stored value,
 * and only then installs the clone. It fails without touching the stored value
 * when the binding is missing, immutable, or the clone could not be allocated.
 *
 * The first semantic failure (failed assignment, failed declaration, integer
 * overflow, division by zero, a returned native/function error, recursion
 * limit) is recorded in the root environment and reported by env_error();
 * once recorded, further evaluations return null immediately so execution does
 * not continue past the first error.
 */
Environment *env_new(void);
void env_free(Environment *env);
Environment *env_retain(Environment *env);
void env_release(Environment *env);

FemNativeRegistry *env_native_registry(Environment *env);

bool env_define(
    Environment *env,
    const char *name,
    const Value *value,
    bool mutable
);

Value *env_lookup(Environment *env, const char *name);

bool env_assign(
    Environment *env,
    const char *name,
    const Value *value
);

const char *env_error(const Environment *env);

/*
 * Evaluates a parsed program. Returns a value the caller owns; release it
 * with value_free(). When a runtime error occurred, the error text is
 * available via env_error(env) and the returned value is VALUE_NULL.
 */
Value eval_ast(Environment *env, const AstNode *node);

#endif