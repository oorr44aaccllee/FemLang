#include "evaluator.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Value constructors and helpers (declared in value.h). A VALUE_STRING owns a
 * malloc'd NUL-terminated buffer; every other type owns no memory. Constructors
 * never leak: on allocation failure a string value collapses to VALUE_NULL.
 */

Value value_null(void) {
    Value value = {VALUE_NULL, {.integer = 0}};
    return value;
}

Value value_bool(bool boolean) {
    Value value = {VALUE_BOOL, {.boolean = boolean}};
    return value;
}

Value value_int(int64_t integer) {
    Value value = {VALUE_INT, {.integer = integer}};
    return value;
}

Value value_float(double floating) {
    Value value = {VALUE_FLOAT, {.floating = floating}};
    return value;
}

Value value_native(FemNativeFunction function) {
    Value value = {VALUE_NATIVE, {.function = function}};
    return value;
}

static char *copy_string(const char *source) {
    size_t length = strlen(source);
    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, source, length + 1U);
    return copy;
}

Value value_string_copy(const char *source) {
    if (source == NULL) {
        return value_null();
    }
    char *copy = copy_string(source);
    if (copy == NULL) {
        return value_null();
    }
    Value value;
    value.type = VALUE_STRING;
    value.as.string = copy;
    return value;
}

Value value_error(const char *message) {
    if (message == NULL) {
        return value_null();
    }
    char *copy = copy_string(message);
    if (copy == NULL) {
        return value_null();
    }
    Value value;
    value.type = VALUE_ERROR;
    value.as.string = copy;
    return value;
}

static void append_value_to_buffer(
    const Value *value,
    char *buffer,
    size_t capacity
) {
    switch (value->type) {
        case VALUE_NULL:
            snprintf(buffer, capacity, "null");
            break;
        case VALUE_BOOL:
            snprintf(buffer, capacity, "%s",
                     value->as.boolean ? "true" : "false");
            break;
        case VALUE_INT:
            snprintf(buffer, capacity, "%lld", (long long)value->as.integer);
            break;
        case VALUE_FLOAT:
            snprintf(buffer, capacity, "%.17g", value->as.floating);
            break;
        case VALUE_NATIVE:
            snprintf(buffer, capacity, "<native function>");
            break;
        case VALUE_ERROR:
            if (value->as.string != NULL) {
                snprintf(buffer, capacity, "%s", value->as.string);
            } else {
                snprintf(buffer, capacity, "error");
            }
            break;
        case VALUE_STRING:
            break;
    }
}

/*
 * Returns the display form of a value as a new owned string: strings are the
 * raw bytes (no quotes), every other type is its textual representation. On
 * allocation failure returns VALUE_NULL (see value.h "asked for string, got
 * NULL" convention).
 */
Value value_to_string(const Value *value) {
    if (value == NULL) {
        return value_string_copy("null");
    }
    if (value->type == VALUE_STRING) {
        return value_string_copy(value->as.string);
    }
    char buffer[64];
    append_value_to_buffer(value, buffer, sizeof(buffer));
    return value_string_copy(buffer);
}

Value value_clone(const Value *value) {
    if (value == NULL) {
        return value_null();
    }
    if (value->type == VALUE_STRING) {
        return value_string_copy(value->as.string);
    }
    if (value->type == VALUE_ERROR) {
        return value_error(value->as.string);
    }
    /* Non-string types own no heap memory; a shallow copy is safe. */
    return *value;
}

void value_free(Value *value) {
    if (value == NULL) {
        return;
    }
    if (value->type == VALUE_STRING || value->type == VALUE_ERROR) {
        free(value->as.string);
    }
    value->type = VALUE_NULL;
    value->as.integer = 0;
}

bool value_truthy(const Value *value) {
    if (value == NULL) {
        return false;
    }
    switch (value->type) {
        case VALUE_NULL:
            return false;
        case VALUE_BOOL:
            return value->as.boolean;
        case VALUE_INT:
            return value->as.integer != 0;
        case VALUE_FLOAT:
            return value->as.floating != 0.0;
        case VALUE_STRING:
            return value->as.string != NULL && value->as.string[0] != '\0';
        case VALUE_NATIVE:
            return false;
        case VALUE_ERROR:
            return false;
    }
    return false;
}

void print_value(const Value *value) {
    if (value == NULL) {
        fputs("null", stdout);
        return;
    }
    switch (value->type) {
        case VALUE_NULL:
            fputs("null", stdout);
            break;
        case VALUE_BOOL:
            fputs(value->as.boolean ? "true" : "false", stdout);
            break;
        case VALUE_INT:
            printf("%lld", (long long)value->as.integer);
            break;
        case VALUE_FLOAT:
            printf("%.17g", value->as.floating);
            break;
        case VALUE_STRING:
            putchar('"');
            if (value->as.string != NULL) {
                for (const char *p = value->as.string; *p != '\0'; p++) {
                    switch (*p) {
                        case '"': fputs("\\\"", stdout); break;
                        case '\\': fputs("\\\\", stdout); break;
                        case '\n': fputs("\\n", stdout); break;
                        case '\t': fputs("\\t", stdout); break;
                        case '\r': fputs("\\r", stdout); break;
                        default: putchar(*p); break;
                    }
                }
            }
            putchar('"');
            break;
        case VALUE_NATIVE:
            fputs("<native function>", stdout);
            break;
        case VALUE_ERROR:
            if (value->as.string != NULL) {
                fputs(value->as.string, stdout);
            }
            break;
    }
}

/*
 * Environment
 * -----------
 * Bindings are stored in a singly linked list; each entry owns its name copy
 * and its value copy. The first runtime error is recorded once in the
 * environment and reported through env_error().
 */

typedef struct Entry {
    char *name;
    Value value;
    bool mutable;
    struct Entry *next;
} Entry;

struct Environment {
    Entry *head;
    FemNativeRegistry natives;
    char error_message[256];
};

static void env_record_error(Environment *env, const char *message) {
    if (env == NULL || env->error_message[0] != '\0') {
        return;
    }
    snprintf(env->error_message, sizeof(env->error_message), "%s", message);
}

static void env_record_errorf(Environment *env, const char *format, const char *name) {
    if (env == NULL || env->error_message[0] != '\0') {
        return;
    }
    snprintf(env->error_message, sizeof(env->error_message), format, name);
}

static Entry *env_find(Environment *env, const char *name) {
    if (env == NULL || name == NULL) {
        return NULL;
    }
    for (Entry *entry = env->head; entry != NULL; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

Environment *env_new(void) {
    Environment *env = calloc(1, sizeof(Environment));
    if (env != NULL) {
        native_registry_init(&env->natives);
    }
    return env;
}

void env_free(Environment *env) {
    if (env == NULL) {
        return;
    }
    Entry *entry = env->head;
    while (entry != NULL) {
        Entry *next = entry->next;
        free(entry->name);
        value_free(&entry->value);
        free(entry);
        entry = next;
    }
    native_registry_free(&env->natives);
    free(env);
}

FemNativeRegistry *env_native_registry(Environment *env) {
    return env != NULL ? &env->natives : NULL;
}

bool env_define(Environment *env, const char *name, const Value *value, bool mutable) {
    if (env == NULL || name == NULL) {
        return false;
    }

    char *name_copy = copy_string(name);
    if (name_copy == NULL) {
        env_record_error(env, "out of memory");
        return false;
    }

    Value value_copy = value_clone(value);
    if (value != NULL && value->type == VALUE_STRING && value_copy.type != VALUE_STRING) {
        free(name_copy);
        env_record_error(env, "out of memory");
        return false;
    }

    Entry *entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        value_free(&value_copy);
        free(name_copy);
        env_record_error(env, "out of memory");
        return false;
    }

    entry->name = name_copy;
    entry->value = value_copy;
    entry->mutable = mutable;
    entry->next = env->head;
    env->head = entry;
    return true;
}

Value *env_lookup(Environment *env, const char *name) {
    Entry *entry = env_find(env, name);
    if (entry == NULL) {
        return NULL;
    }
    return &entry->value;
}

bool env_assign(Environment *env, const char *name, const Value *value) {
    if (env == NULL || name == NULL || value == NULL) {
        return false;
    }

    Entry *entry = env_find(env, name);
    if (entry == NULL) {
        env_record_errorf(env, "cannot assign to undefined variable '%s'", name);
        return false;
    }
    if (!entry->mutable) {
        env_record_errorf(env, "cannot reassign immutable variable '%s'", name);
        return false;
    }

    Value copy = value_clone(value);
    if (value->type == VALUE_STRING && copy.type != VALUE_STRING) {
        env_record_error(env, "out of memory");
        return false;
    }

    value_free(&entry->value);
    entry->value = copy;
    return true;
}

const char *env_error(const Environment *env) {
    if (env == NULL || env->error_message[0] == '\0') {
        return NULL;
    }
    return env->error_message;
}

/*
 * Evaluator
 * ---------
 * Once an error is recorded, evaluation stops: every later eval() returns null
 * without side effects, so the program result is well-defined after failure.
 */

static bool int_add(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool int_subtract(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b)) {
        return false;
    }
    *out = a - b;
    return true;
}

static bool int_multiply(int64_t a, int64_t b, int64_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }
    if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN)) {
        return false;
    }
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) {
                return false;
            }
        } else if (b < INT64_MIN / a) {
            return false;
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b) {
                return false;
            }
        } else if (a < INT64_MAX / b) {
            return false;
        }
    }
    *out = a * b;
    return true;
}

static const char *operator_symbol(TokenType type) {
    switch (type) {
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_PERCENT: return "%";
        case TOKEN_EQUAL_EQUAL: return "==";
        case TOKEN_BANG_EQUAL: return "!=";
        case TOKEN_LESS: return "<";
        case TOKEN_LESS_EQUAL: return "<=";
        case TOKEN_GREATER: return ">";
        case TOKEN_GREATER_EQUAL: return ">=";
        default: return "?";
    }
}

static Value eval(Environment *env, const AstNode *node) {
    if (node == NULL || env_error(env) != NULL) {
        return value_null();
    }

    switch (node->type) {
        case AST_INTEGER:
            return value_int(node->as.integer);

        case AST_FLOAT:
            return value_float(node->as.floating);

        case AST_BOOLEAN:
            return value_bool(node->as.boolean);

        case AST_NULL:
            return value_null();

        case AST_STRING:
            return value_string_copy(node->as.string);

        case AST_IDENTIFIER: {
            Value *stored = env_lookup(env, node->as.identifier);
            if (stored != NULL) {
                return value_clone(stored);
            }
            FemNativeFunction function =
                native_lookup(&env->natives, node->as.identifier);
            if (function != NULL) {
                return value_native(function);
            }
            env_record_errorf(env, "undefined variable '%s'", node->as.identifier);
            return value_null();
        }

        case AST_LET: {
            Value value = eval(env, node->as.declaration.value);
            if (!env_define(env, node->as.declaration.name, &value,
                            node->as.declaration.mutable)) {
                value_free(&value);
                return value_null();
            }
            return value;
        }

        case AST_ASSIGNMENT: {
            Value value = eval(env, node->as.assignment.value);
            if (!env_assign(env, node->as.assignment.name, &value)) {
                value_free(&value);
                return value_null();
            }
            return value;
        }

        case AST_RETURN:
            return eval(env, node->as.return_statement.value);

        case AST_EXPRESSION_STATEMENT:
            return eval(env, node->as.expression_statement.expression);

        case AST_PROGRAM:
        case AST_BLOCK: {
            Value last = value_null();
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                Value current = eval(env, node->as.block.statements.items[i]);
                value_free(&last);
                last = current;
            }
            return last;
        }

        case AST_IF: {
            Value condition = eval(env, node->as.if_statement.condition);
            bool is_truthy = value_truthy(&condition);
            value_free(&condition);
            return eval(env, is_truthy
                            ? node->as.if_statement.then_branch
                            : node->as.if_statement.else_branch);
        }

        case AST_UNARY: {
            Value operand = eval(env, node->as.unary.operand);

            if (node->as.unary.operator_type == TOKEN_BANG) {
                bool result = !value_truthy(&operand);
                value_free(&operand);
                return value_bool(result);
            }
            if (node->as.unary.operator_type == TOKEN_MINUS &&
                operand.type == VALUE_INT) {
                if (operand.as.integer == INT64_MIN) {
                    value_free(&operand);
                    env_record_error(env, "integer arithmetic error");
                    return value_null();
                }
                operand.as.integer = -operand.as.integer;
                return operand;
            }
            if (node->as.unary.operator_type == TOKEN_MINUS &&
                operand.type == VALUE_FLOAT) {
                operand.as.floating = -operand.as.floating;
                return operand;
            }

            value_free(&operand);
            return value_null();
        }

        case AST_BINARY: {
            Value left = eval(env, node->as.binary.left);
            Value right = eval(env, node->as.binary.right);
            TokenType op = node->as.binary.operator_type;

            if (op == TOKEN_EQUAL_EQUAL || op == TOKEN_BANG_EQUAL) {
                bool equal = false;
                if (left.type == right.type) {
                    switch (left.type) {
                        case VALUE_INT:
                            equal = left.as.integer == right.as.integer;
                            break;
                        case VALUE_FLOAT:
                            equal = left.as.floating == right.as.floating;
                            break;
                        case VALUE_BOOL:
                            equal = left.as.boolean == right.as.boolean;
                            break;
                        case VALUE_NULL:
                            equal = true;
                            break;
                        case VALUE_STRING:
                            equal = left.as.string != NULL &&
                                    right.as.string != NULL &&
                                    strcmp(left.as.string, right.as.string) == 0;
                            break;
                        default:
                            equal = false;
                            break;
                    }
                }
                value_free(&left);
                value_free(&right);
                return value_bool(op == TOKEN_EQUAL_EQUAL ? equal : !equal);
            }

            if (op == TOKEN_LESS || op == TOKEN_LESS_EQUAL ||
                op == TOKEN_GREATER || op == TOKEN_GREATER_EQUAL) {
                bool result = false;
                bool numeric = false;
                if (left.type == VALUE_INT && right.type == VALUE_INT) {
                    result = op == TOKEN_LESS ? left.as.integer < right.as.integer
                           : op == TOKEN_LESS_EQUAL ? left.as.integer <= right.as.integer
                           : op == TOKEN_GREATER ? left.as.integer > right.as.integer
                           : left.as.integer >= right.as.integer;
                    numeric = true;
                } else if (left.type == VALUE_FLOAT && right.type == VALUE_FLOAT) {
                    result = op == TOKEN_LESS ? left.as.floating < right.as.floating
                           : op == TOKEN_LESS_EQUAL ? left.as.floating <= right.as.floating
                           : op == TOKEN_GREATER ? left.as.floating > right.as.floating
                           : left.as.floating >= right.as.floating;
                    numeric = true;
                }
                value_free(&left);
                value_free(&right);
                if (numeric) {
                    return value_bool(result);
                }
                env_record_error(env, "comparison requires two numbers");
                return value_null();
            }

            if (left.type == VALUE_STRING && right.type == VALUE_STRING &&
                op == TOKEN_PLUS) {
                size_t left_length = strlen(left.as.string);
                size_t right_length = strlen(right.as.string);
                char *joined = malloc(left_length + right_length + 1U);
                Value result = value_null();
                if (joined != NULL) {
                    memcpy(joined, left.as.string, left_length);
                    memcpy(joined + left_length, right.as.string, right_length);
                    joined[left_length + right_length] = '\0';
                    result.type = VALUE_STRING;
                    result.as.string = joined;
                } else {
                    env_record_error(env, "out of memory");
                }
                value_free(&left);
                value_free(&right);
                return result;
            }

            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                int64_t result = 0;
                bool ok = true;
                switch (op) {
                    case TOKEN_PLUS:
                        ok = int_add(left.as.integer, right.as.integer, &result);
                        break;
                    case TOKEN_MINUS:
                        ok = int_subtract(left.as.integer, right.as.integer, &result);
                        break;
                    case TOKEN_STAR:
                        ok = int_multiply(left.as.integer, right.as.integer, &result);
                        break;
                    case TOKEN_SLASH:
                        if (right.as.integer == 0) {
                            env_record_error(env, "division by zero");
                            ok = false;
                        } else if (left.as.integer == INT64_MIN && right.as.integer == -1) {
                            ok = false;
                        } else {
                            result = left.as.integer / right.as.integer;
                        }
                        break;
                    case TOKEN_PERCENT:
                        if (right.as.integer == 0) {
                            env_record_error(env, "division by zero");
                            ok = false;
                        } else if (left.as.integer == INT64_MIN && right.as.integer == -1) {
                            result = 0;
                        } else {
                            result = left.as.integer % right.as.integer;
                        }
                        break;
                    default:
                        ok = false;
                        break;
                }
                value_free(&left);
                value_free(&right);
                if (ok) {
                    return value_int(result);
                }
                env_record_error(env, "integer arithmetic error");
                return value_null();
            }

            if (left.type == VALUE_FLOAT && right.type == VALUE_FLOAT) {
                double result = 0.0;
                bool ok = true;
                switch (op) {
                    case TOKEN_PLUS:
                        result = left.as.floating + right.as.floating;
                        break;
                    case TOKEN_MINUS:
                        result = left.as.floating - right.as.floating;
                        break;
                    case TOKEN_STAR:
                        result = left.as.floating * right.as.floating;
                        break;
                    case TOKEN_SLASH:
                        if (right.as.floating == 0.0) {
                            env_record_error(env, "division by zero");
                            ok = false;
                        } else {
                            result = left.as.floating / right.as.floating;
                        }
                        break;
                    case TOKEN_PERCENT:
                        if (right.as.floating == 0.0) {
                            env_record_error(env, "division by zero");
                            ok = false;
                        } else {
                            result = fmod(left.as.floating, right.as.floating);
                        }
                        break;
                    default:
                        ok = false;
                        break;
                }
                value_free(&left);
                value_free(&right);
                if (ok) {
                    return value_float(result);
                }
                return value_null();
            }

            /* Unsupported operator or incompatible operand types. */
            {
                char message[128];
                snprintf(message, sizeof(message),
                         "cannot apply operator '%s' to these values",
                         operator_symbol(op));
                env_record_error(env, message);
            }
            value_free(&left);
            value_free(&right);
            return value_null();
        }

        case AST_CALL: {
            Value callee = eval(env, node->as.call.callee);
            if (callee.type != VALUE_NATIVE) {
                value_free(&callee);
                env_record_error(env, "attempt to call a non-function value");
                return value_null();
            }
            FemNativeFunction function = callee.as.function;
            value_free(&callee);

            size_t count = node->as.call.arguments.count;
            Value *arguments = NULL;
            if (count > 0) {
                arguments = malloc(count * sizeof(*arguments));
                if (arguments == NULL) {
                    env_record_error(env, "out of memory");
                    return value_null();
                }
            }

            size_t evaluated = 0;
            for (size_t i = 0; i < count; i++) {
                Value argument = eval(env, node->as.call.arguments.items[i]);
                if (env_error(env) != NULL) {
                    for (size_t j = 0; j < evaluated; j++) {
                        value_free(&arguments[j]);
                    }
                    free(arguments);
                    return value_null();
                }
                arguments[evaluated++] = argument;
            }

            Value result = function(count, arguments);
            for (size_t i = 0; i < evaluated; i++) {
                value_free(&arguments[i]);
            }
            free(arguments);

            if (result.type == VALUE_ERROR) {
                const char *message = result.as.string;
                if (message != NULL) {
                    env_record_error(env, message);
                }
                value_free(&result);
                return value_null();
            }
            return result;
        }

        case AST_LIST:
            return value_null();
    }

    return value_null();
}

Value eval_ast(Environment *env, const AstNode *node) {
    return eval(env, node);
}