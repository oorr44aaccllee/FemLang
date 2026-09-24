#ifndef FEMLANG_VALUE_H
#define FEMLANG_VALUE_H

#include <stdbool.h>
#include <stdint.h>

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
void print_value(const Value *value);
bool value_truthy(const Value *value);

#endif
