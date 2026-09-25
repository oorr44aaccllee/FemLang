#ifndef FEMLANG_VALUE_H
#define FEMLANG_VALUE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Ownership rules
 * ---------------
 * Each Value owns its own heap data and nothing else:
 *
 *   - VALUE_STRING owns a single NUL-terminated char* allocated with malloc.
 *     The string may be NULL only when the Value itself is empty/invalid.
 *   - All other value types own no heap memory.
 *
 * Every heap-owning Value has exactly one logical owner. Copy ownership by
 * calling value_clone() (deep copy for strings) before storing a value where
 * the original must remain valid and separately owned. Release ownership with
 * value_free(); it is a no-op on VALUE_NULL and safe on already-freed values.
 *
 * value_string_copy() and value_clone() on a string return a VALUE_NULL value
 * if the allocation fails, so a failed clone never leaks or shares the
 * original string. Callers that rely on a successful string clone must treat
 * "requested string, got NULL" as an allocation failure.
 */

typedef enum {
    VALUE_NULL,
    VALUE_BOOL,
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_STRING
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        char *string;
    } as;
} Value;

Value value_null(void);
Value value_bool(bool value);
Value value_int(int64_t value);
Value value_float(double value);
Value value_string_copy(const char *source);
Value value_clone(const Value *value);
void value_free(Value *value);
bool value_truthy(const Value *value);
void print_value(const Value *value);

#endif