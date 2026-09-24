#include "evaluator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Entry {
    char *name;
    Value value;
    struct Entry *next;
} Entry;

struct Environment {
    Entry *head;
};

static char *dupstr(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1U);
    if (p != NULL) {
        memcpy(p, s, n + 1U);
    }
    return p;
}

Value value_null(void) {
    Value v = {VALUE_NULL, {.integer = 0}};
    return v;
}

Value value_bool(bool b) {
    Value v = {VALUE_BOOL, {.boolean = b}};
    return v;
}

Value value_int(int64_t i) {
    Value v = {VALUE_INT, {.integer = i}};
    return v;
}

Value value_float(double f) {
    Value v = {VALUE_FLOAT, {.floating = f}};
    return v;
}

Value value_string_copy(const char *source) {
    Value v = {VALUE_STRING, {.string = NULL}};
    if (source == NULL) {
        return v;
    }
    v.as.string = dupstr(source);
    if (v.as.string == NULL) {
        v.type = VALUE_NULL;
    }
    return v;
}

Value value_clone(const Value *value) {
    if (value == NULL) {
        return value_null();
    }

    Value copy = *value;
    if (value->type == VALUE_STRING) {
        copy.as.string = NULL;
        if (value->as.string != NULL) {
            copy.as.string = dupstr(value->as.string);
            if (copy.as.string == NULL) {
                copy.type = VALUE_NULL;
            }
        }
    }
    return copy;
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
    value->as.integer = 0;
}

void print_value(const Value *value) {
    if (value == NULL) {
        printf("null");
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
            printf("%.17g", value->as.floating);
            break;
        case VALUE_STRING:
            printf("%s", value->as.string != NULL ? value->as.string : "");
            break;
        default:
            printf("null");
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
    return calloc(1, sizeof(Environment));
}

void env_free(Environment *env) {
    if (env == NULL) {
        return;
    }

    while (env->head != NULL) {
        Entry *next = env->head->next;
        free(env->head->name);
        value_free(&env->head->value);
        free(env->head);
        env->head = next;
    }

    free(env);
}

void env_define(Environment *env, const char *name, Value value) {
    if (env == NULL || name == NULL) {
        if (value.type == VALUE_STRING) {
            value_free(&value);
        }
        return;
    }

    Entry *entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        if (value.type == VALUE_STRING) {
            value_free(&value);
        }
        return;
    }

    entry->name = dupstr(name);
    if (entry->name == NULL) {
        free(entry);
        if (value.type == VALUE_STRING) {
            value_free(&value);
        }
        return;
    }

    entry->value = value_clone(&value);
    entry->next = env->head;
    env->head = entry;

    if (value.type == VALUE_STRING) {
        value_free(&value);
    }
}

Value *env_lookup(Environment *env, const char *name) {
    if (env == NULL || name == NULL) {
        return NULL;
    }

    for (Entry *entry = env->head; entry != NULL; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            return &entry->value;
        }
    }

    return NULL;
}

static Value eval(Environment *env, const AstNode *node) {
    if (node == NULL) {
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
            if (stored == NULL) {
                return value_null();
            }
            return value_clone(stored);
        }
        case AST_LET: {
            Value evaluated = eval(env, node->as.declaration.value);
            Value owned = value_clone(&evaluated);
            env_define(env, node->as.declaration.name, owned);
            value_free(&owned);
            return evaluated;
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
            bool truthy = value_truthy(&condition);
            value_free(&condition);

            if (truthy) {
                return eval(env, node->as.if_statement.then_branch);
            }

            return eval(env, node->as.if_statement.else_branch);
        }
        case AST_UNARY: {
            Value operand = eval(env, node->as.unary.operand);
            if (node->as.unary.operator_type == TOKEN_BANG) {
                Value result = value_bool(!value_truthy(&operand));
                value_free(&operand);
                return result;
            }

            if (node->as.unary.operator_type == TOKEN_MINUS) {
                if (operand.type == VALUE_INT) {
                    operand.as.integer = -operand.as.integer;
                    return operand;
                }
                if (operand.type == VALUE_FLOAT) {
                    operand.as.floating = -operand.as.floating;
                    return operand;
                }
            }

            value_free(&operand);
            return value_null();
        }
        case AST_BINARY: {
            Value left = eval(env, node->as.binary.left);
            Value right = eval(env, node->as.binary.right);
            TokenType op = node->as.binary.operator_type;

            if (op == TOKEN_PLUS && left.type == VALUE_STRING && right.type == VALUE_STRING) {
                size_t left_len = left.as.string != NULL ? strlen(left.as.string) : 0;
                size_t right_len = right.as.string != NULL ? strlen(right.as.string) : 0;
                char *combined = malloc(left_len + right_len + 1U);
                if (combined == NULL) {
                    value_free(&left);
                    value_free(&right);
                    return value_null();
                }
                memcpy(combined, left.as.string != NULL ? left.as.string : "", left_len);
                memcpy(combined + left_len, right.as.string != NULL ? right.as.string : "", right_len + 1U);
                Value result = value_string_copy(combined);
                free(combined);
                value_free(&left);
                value_free(&right);
                return result;
            }

            if (left.type == VALUE_INT && right.type == VALUE_INT) {
                int64_t result = 0;
                bool ok = true;

                switch (op) {
                    case TOKEN_PLUS:
                        result = left.as.integer + right.as.integer;
                        break;
                    case TOKEN_MINUS:
                        result = left.as.integer - right.as.integer;
                        break;
                    case TOKEN_STAR:
                        result = left.as.integer * right.as.integer;
                        break;
                    case TOKEN_SLASH:
                        if (right.as.integer == 0) {
                            ok = false;
                        } else {
                            result = left.as.integer / right.as.integer;
                        }
                        break;
                    case TOKEN_PERCENT:
                        if (right.as.integer == 0) {
                            ok = false;
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
                if (!ok) {
                    return value_null();
                }
                return value_int(result);
            }

            if (left.type == VALUE_FLOAT || right.type == VALUE_FLOAT) {
                double a = left.type == VALUE_FLOAT ? left.as.floating : (double)left.as.integer;
                double b = right.type == VALUE_FLOAT ? right.as.floating : (double)right.as.integer;
                double result = 0.0;
                bool ok = true;

                switch (op) {
                    case TOKEN_PLUS:
                        result = a + b;
                        break;
                    case TOKEN_MINUS:
                        result = a - b;
                        break;
                    case TOKEN_STAR:
                        result = a * b;
                        break;
                    case TOKEN_SLASH:
                        if (b == 0.0) {
                            ok = false;
                        } else {
                            result = a / b;
                        }
                        break;
                    default:
                        ok = false;
                        break;
                }

                value_free(&left);
                value_free(&right);
                if (!ok) {
                    return value_null();
                }
                return value_float(result);
            }

            if (op == TOKEN_EQUAL_EQUAL || op == TOKEN_BANG_EQUAL) {
                bool equal = false;
                if (left.type == VALUE_STRING && right.type == VALUE_STRING) {
                    equal = strcmp(left.as.string != NULL ? left.as.string : "", right.as.string != NULL ? right.as.string : "") == 0;
                } else if (left.type == VALUE_INT && right.type == VALUE_INT) {
                    equal = left.as.integer == right.as.integer;
                } else if (left.type == VALUE_BOOL && right.type == VALUE_BOOL) {
                    equal = left.as.boolean == right.as.boolean;
                } else if (left.type == VALUE_NULL && right.type == VALUE_NULL) {
                    equal = true;
                }

                value_free(&left);
                value_free(&right);
                return value_bool(op == TOKEN_EQUAL_EQUAL ? equal : !equal);
            }

            if ((op == TOKEN_LESS || op == TOKEN_LESS_EQUAL || op == TOKEN_GREATER || op == TOKEN_GREATER_EQUAL) &&
                left.type == VALUE_INT && right.type == VALUE_INT) {
                bool result = false;
                switch (op) {
                    case TOKEN_LESS:
                        result = left.as.integer < right.as.integer;
                        break;
                    case TOKEN_LESS_EQUAL:
                        result = left.as.integer <= right.as.integer;
                        break;
                    case TOKEN_GREATER:
                        result = left.as.integer > right.as.integer;
                        break;
                    case TOKEN_GREATER_EQUAL:
                        result = left.as.integer >= right.as.integer;
                        break;
                    default:
                        break;
                }

                value_free(&left);
                value_free(&right);
                return value_bool(result);
            }

            value_free(&left);
            value_free(&right);
            return value_null();
        }
        default:
            return value_null();
    }
}

Value eval_ast(Environment *env, const AstNode *node) {
    return eval(env, node);
}
