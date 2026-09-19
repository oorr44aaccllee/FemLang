#include "evaluator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct EnvironmentEntry {
    char *name;
    Value value;
    struct EnvironmentEntry *next;
} EnvironmentEntry;

struct Environment {
    EnvironmentEntry *head;
};

static char *duplicate_string(const char *source) {
    size_t length = strlen(source);
    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, source, length + 1U);
    return copy;
}

Value value_null(void) {
    Value value;
    value.type = VALUE_NULL;
    value.as.integer = 0;
    return value;
}

Value value_bool(bool value) {
    Value result;
    result.type = VALUE_BOOL;
    result.as.boolean = value;
    return result;
}

Value value_int(int64_t value) {
    Value result;
    result.type = VALUE_INT;
    result.as.integer = value;
    return result;
}

Value value_float(double value) {
    Value result;
    result.type = VALUE_FLOAT;
    result.as.floating = value;
    return result;
}

Value value_string_copy(const char *source) {
    Value result;
    result.type = VALUE_STRING;
    result.as.string = duplicate_string(source);
    if (result.as.string == NULL) {
        result.type = VALUE_NULL;
    }
    return result;
}

void value_free(Value *value) {
    if (value == NULL) {
        return;
    }

    if (value->type == VALUE_STRING && value->as.string != NULL) {
        free(value->as.string);
        value->as.string = NULL;
    }
    value->type = VALUE_NULL;
}

void print_value(const Value *value) {
    if (value == NULL) {
        printf("(null)");
        return;
    }

    switch (value->type) {
        case VALUE_NULL:
            printf("null");
            break;
        case VALUE_BOOL:
            printf("%s", value->as.boolean ? "true" : "false");
            break;
        case VALUE_INT:
            printf("%lld", (long long)value->as.integer);
            break;
        case VALUE_FLOAT:
            printf("%f", value->as.floating);
            break;
        case VALUE_STRING:
            printf("%s", value->as.string != NULL ? value->as.string : "");
            break;
        default:
            printf("(unknown)");
            break;
    }
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
        default:
            return false;
    }
}

Environment *env_new(void) {
    Environment *env = calloc(1, sizeof(*env));
    return env;
}

void env_free(Environment *env) {
    if (env == NULL) {
        return;
    }

    EnvironmentEntry *entry = env->head;
    while (entry != NULL) {
        EnvironmentEntry *next = entry->next;
        free(entry->name);
        value_free(&entry->value);
        free(entry);
        entry = next;
    }

    free(env);
}

void env_define(Environment *env, const char *name, Value value) {
    if (env == NULL || name == NULL) {
        return;
    }

    EnvironmentEntry *entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        return;
    }

    entry->name = duplicate_string(name);
    if (entry->name == NULL) {
        free(entry);
        return;
    }

    entry->value = value;
    entry->next = env->head;
    env->head = entry;
}

Value *env_lookup(Environment *env, const char *name) {
    if (env == NULL || name == NULL) {
        return NULL;
    }

    for (EnvironmentEntry *entry = env->head; entry != NULL; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            return &entry->value;
        }
    }

    return NULL;
}

static Value eval_unary(const AstNode *node, Environment *env) {
    Value value = eval_ast(env, node->as.unary.operand);
    switch (node->as.unary.operator_type) {
        case TOKEN_MINUS:
            if (value.type == VALUE_INT) {
                value.as.integer = -value.as.integer;
                return value;
            }
            if (value.type == VALUE_FLOAT) {
                value.as.floating = -value.as.floating;
                return value;
            }
            return value_null();
        case TOKEN_BANG:
            return value_bool(!value_truthy(&value));
        default:
            return value_null();
    }
}

static Value eval_binary(const AstNode *node, Environment *env) {
    Value left = eval_ast(env, node->as.binary.left);
    Value right = eval_ast(env, node->as.binary.right);

    switch (node->as.binary.operator_type) {
        case TOKEN_PLUS:
            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                return value_int(left.as.integer + right.as.integer);
            }
            if (left.type == VALUE_FLOAT || right.type == VALUE_FLOAT) {
                double a = left.type == VALUE_FLOAT ? left.as.floating : (double)left.as.integer;
                double b = right.type == VALUE_FLOAT ? right.as.floating : (double)right.as.integer;
                return value_float(a + b);
            }
            if (left.type == VALUE_STRING && right.type == VALUE_STRING) {
                size_t total = strlen(left.as.string) + strlen(right.as.string) + 1U;
                char *combined = malloc(total);
                if (combined == NULL) {
                    return value_null();
                }
                snprintf(combined, total, "%s%s", left.as.string, right.as.string);
                Value out = value_string_copy(combined);
                free(combined);
                return out;
            }
            return value_null();

        case TOKEN_MINUS:
            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                return value_int(left.as.integer - right.as.integer);
            }
            if (left.type == VALUE_FLOAT || right.type == VALUE_FLOAT) {
                double a = left.type == VALUE_FLOAT ? left.as.floating : (double)left.as.integer;
                double b = right.type == VALUE_FLOAT ? right.as.floating : (double)right.as.integer;
                return value_float(a - b);
            }
            return value_null();

        case TOKEN_STAR:
            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                return value_int(left.as.integer * right.as.integer);
            }
            if (left.type == VALUE_FLOAT || right.type == VALUE_FLOAT) {
                double a = left.type == VALUE_FLOAT ? left.as.floating : (double)left.as.integer;
                double b = right.type == VALUE_FLOAT ? right.as.floating : (double)right.as.integer;
                return value_float(a * b);
            }
            return value_null();

        case TOKEN_SLASH:
            if (left.type == VALUE_INT && right.type == VALUE_INT && right.as.integer != 0) {
                return value_int(left.as.integer / right.as.integer);
            }
            if ((left.type == VALUE_FLOAT || right.type == VALUE_FLOAT) &&
                right.type != VALUE_NULL &&
                (right.type != VALUE_INT || right.as.integer != 0)) {
                double divisor = right.type == VALUE_FLOAT ? right.as.floating : (double)right.as.integer;
                double dividend = left.type == VALUE_FLOAT ? left.as.floating : (double)left.as.integer;
                return value_float(dividend / divisor);
            }
            return value_null();

        case TOKEN_EQUAL_EQUAL:
            if (left.type == VALUE_STRING && right.type == VALUE_STRING) {
                return value_bool(strcmp(left.as.string, right.as.string) == 0);
            }
            if (left.type == VALUE_BOOL && right.type == VALUE_BOOL) {
                return value_bool(left.as.boolean == right.as.boolean);
            }
            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                return value_bool(left.as.integer == right.as.integer);
            }
            if (left.type == VALUE_FLOAT && right.type == VALUE_FLOAT) {
                return value_bool(left.as.floating == right.as.floating);
            }
            return value_bool(false);

        case TOKEN_BANG_EQUAL:
            if (left.type == VALUE_STRING && right.type == VALUE_STRING) {
                return value_bool(strcmp(left.as.string, right.as.string) != 0);
            }
            if (left.type == VALUE_BOOL && right.type == VALUE_BOOL) {
                return value_bool(left.as.boolean != right.as.boolean);
            }
            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                return value_bool(left.as.integer != right.as.integer);
            }
            if (left.type == VALUE_FLOAT && right.type == VALUE_FLOAT) {
                return value_bool(left.as.floating != right.as.floating);
            }
            return value_bool(true);

        case TOKEN_LESS:
            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                return value_bool(left.as.integer < right.as.integer);
            }
            if (left.type == VALUE_FLOAT || right.type == VALUE_FLOAT) {
                double a = left.type == VALUE_FLOAT ? left.as.floating : (double)left.as.integer;
                double b = right.type == VALUE_FLOAT ? right.as.floating : (double)right.as.integer;
                return value_bool(a < b);
            }
            return value_null();
        case TOKEN_GREATER:
            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                return value_bool(left.as.integer > right.as.integer);
            }
            if (left.type == VALUE_FLOAT || right.type == VALUE_FLOAT) {
                double a = left.type == VALUE_FLOAT ? left.as.floating : (double)left.as.integer;
                double b = right.type == VALUE_FLOAT ? right.as.floating : (double)right.as.integer;
                return value_bool(a > b);
            }
            return value_null();
        default:
            return value_null();
    }
}

Value eval_ast(Environment *env, const AstNode *node) {
    if (node == NULL) {
        return value_null();
    }

    switch (node->type) {
        case AST_INTEGER:
            return value_int(node->as.integer);
        case AST_FLOAT:
            return value_float(node->as.floating);
        case AST_STRING:
            return value_string_copy(node->as.string);
        case AST_BOOLEAN:
            return value_bool(node->as.boolean);
        case AST_NULL:
            return value_null();
        case AST_IDENTIFIER: {
            Value *value = env_lookup(env, node->as.identifier);
            if (value == NULL) {
                return value_null();
            }
            return *value;
        }
        case AST_LET: {
            Value value = eval_ast(env, node->as.declaration.value);
            env_define(env, node->as.declaration.name, value);
            return value;
        }
        case AST_RETURN:
            return eval_ast(env, node->as.return_statement.value);
        case AST_UNARY:
            return eval_unary(node, env);
        case AST_BINARY:
            return eval_binary(node, env);
        case AST_EXPRESSION_STATEMENT:
            return eval_ast(env, node->as.expression_statement.expression);
        case AST_PROGRAM: {
            Value last = value_null();
            for (size_t i = 0; i < node->as.program.statements.count; i++) {
                last = eval_ast(env, node->as.program.statements.items[i]);
            }
            return last;
        }
        default:
            return value_null();
    }
}
