#include "builtins.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static Value native_print(size_t argument_count, const Value *arguments) {
    for (size_t i = 0; i < argument_count; i++) {
        Value text = value_to_string(&arguments[i]);
        if (text.type != VALUE_STRING) {
            value_free(&text);
            return value_error("out of memory");
        }
        if (i > 0) {
            putchar(' ');
        }
        fputs(text.as.string, stdout);
        value_free(&text);
    }
    putchar('\n');
    return value_null();
}

static Value native_len(size_t argument_count, const Value *arguments) {
    if (argument_count != 1) {
        return value_error("len() expects exactly 1 argument");
    }
    if (arguments[0].type != VALUE_STRING) {
        return value_error("len() expects a string");
    }
    return value_int((int64_t)strlen(arguments[0].as.string));
}

static Value native_int(size_t argument_count, const Value *arguments) {
    if (argument_count != 1) {
        return value_error("int() expects exactly 1 argument");
    }
    const Value *argument = &arguments[0];
    switch (argument->type) {
        case VALUE_INT:
            return value_int(argument->as.integer);
        case VALUE_BOOL:
            return value_int(argument->as.boolean ? 1 : 0);
        case VALUE_FLOAT: {
            double x = argument->as.floating;
            if (x >= (double)INT64_MIN && x < 9223372036854775808.0) {
                return value_int((int64_t)x);
            }
            return value_error("int() argument out of range");
        }
        default:
            return value_error("int() expects a number or bool");
    }
}

static Value native_float(size_t argument_count, const Value *arguments) {
    if (argument_count != 1) {
        return value_error("float() expects exactly 1 argument");
    }
    const Value *argument = &arguments[0];
    switch (argument->type) {
        case VALUE_INT:
            return value_float((double)argument->as.integer);
        case VALUE_BOOL:
            return value_float(argument->as.boolean ? 1.0 : 0.0);
        case VALUE_FLOAT:
            return value_float(argument->as.floating);
        default:
            return value_error("float() expects a number or bool");
    }
}

static Value native_str(size_t argument_count, const Value *arguments) {
    if (argument_count != 1) {
        return value_error("str() expects exactly 1 argument");
    }
    Value text = value_to_string(&arguments[0]);
    if (text.type != VALUE_STRING) {
        value_free(&text);
        return value_error("out of memory");
    }
    return text;
}

static Value native_type(size_t argument_count, const Value *arguments) {
    if (argument_count != 1) {
        return value_error("type() expects exactly 1 argument");
    }
    const char *name;
    switch (arguments[0].type) {
        case VALUE_NULL: name = "null"; break;
        case VALUE_BOOL: name = "bool"; break;
        case VALUE_INT: name = "int"; break;
        case VALUE_FLOAT: name = "float"; break;
        case VALUE_STRING: name = "string"; break;
        case VALUE_NATIVE: name = "native"; break;
        case VALUE_ERROR: name = "error"; break;
        default: name = "unknown"; break;
    }
    return value_string_copy(name);
}

void fem_stdlib_register(FemNativeRegistry *registry) {
    if (registry == NULL) {
        return;
    }
    native_register(registry, "print", native_print);
    native_register(registry, "len", native_len);
    native_register(registry, "int", native_int);
    native_register(registry, "float", native_float);
    native_register(registry, "str", native_str);
    native_register(registry, "type", native_type);
}