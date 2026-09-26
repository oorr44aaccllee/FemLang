#ifndef FEMLANG_VALUE_H
#define FEMLANG_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Environment Environment;
typedef struct AstNode AstNode;

/*
 * A FemLang function value. Owned heap: the name and the parameter name
 * array of parameter_count pointers. The body is NOT owned: function
 * definitions are static source, so the body AST node is borrowed from the
 * program tree and lives as long as the parsed program. The closure is the
 * environment the function was defined in, retained with env_retain() (and
 * released in value_free()) so lexical capture keeps it alive (see
 * evaluator.h).
 */
typedef struct FemFunction {
    char *name;
    char **parameters;
    size_t parameter_count;
    AstNode *body;
    Environment *closure;
} FemFunction;

/*
 * Ownership rules
 * ---------------
 * Each Value owns its own heap data and nothing else:
 *
 *   - VALUE_STRING owns a single NUL-terminated char* allocated with malloc.
 *     The string may be NULL only when the Value itself is empty/invalid.
 *   - VALUE_ERROR owns its message the same way VALUE_STRING owns its bytes.
 *     It is a transient control value: native callbacks return it to report a
 *     runtime error instead of silently returning null. The evaluator turns a
 *     returned VALUE_ERROR into an environment error message and never lets it
 *     reach a binding.
 *   - VALUE_NATIVE stores a borrowed FemNativeFunction pointer: the Value does
 *     not own it, value_clone() copies the pointer, and value_free() only
 *     resets the Value to VALUE_NULL.
 *   - VALUE_FN owns a FemFunction struct: the name and parameter array are
 *     owned, the body AST node is borrowed from the program tree, and the
 *     closure environment is retained (env_retain()/env_release()). Deep
 *     copies of a function value are independent but share the body node and
 *     share the closure environment.
 *   - All other value types own no heap memory.
 *
 * Every heap-owning Value has exactly one logical owner. Copy ownership by
 * calling value_clone() (deep copy for strings and function values) before
 * storing a value where the original must remain valid and separately owned.
 * Release ownership with value_free(); it is a no-op on VALUE_NULL and safe on
 * already-freed values.
 *
 * value_string_copy() and value_clone() on a heap-owning value return a
 * VALUE_NULL value if the allocation fails, so a failed clone never leaks or
 * shares the original. Callers that rely on a successful clone must treat
 * "requested a string/function, got NULL" as an allocation failure.
 */

typedef enum {
    VALUE_NULL,
    VALUE_BOOL,
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_STRING,
    VALUE_NATIVE,
    VALUE_FN,
    VALUE_ERROR
} ValueType;

typedef struct Value Value;

/*
 * A native C callback used as a callable value. The function pointer is
 * borrowed: it points into whatever registered it (normally the environment's
 * native registry) and is not owned by the Value.
 */
typedef Value (*FemNativeFunction)(size_t argument_count, const Value *arguments);

struct Value {
    ValueType type;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        char *string;
        FemNativeFunction function;
        FemFunction *fn;
    } as;
};

Value value_null(void);
Value value_bool(bool value);
Value value_int(int64_t value);
Value value_float(double value);
Value value_string_copy(const char *source);
Value value_native(FemNativeFunction function);
Value value_function(
    const char *name,
    char **parameters,
    size_t parameter_count,
    AstNode *body,
    Environment *closure
);
Value value_error(const char *message);
Value value_to_string(const Value *value);
Value value_clone(const Value *value);
void value_free(Value *value);
bool value_truthy(const Value *value);
void print_value(const Value *value);

#endif