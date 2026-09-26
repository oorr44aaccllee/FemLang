#include "evaluator.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Value eval(Environment *env, const AstNode *node);

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

/*
 * Types whose Value owns heap memory. Clone failures for these collapse the
 * clone to VALUE_NULL, so callers can detect an out-of-memory condition.
 */
static bool value_heap_owning(ValueType type) {
    return type == VALUE_STRING || type == VALUE_ERROR || type == VALUE_FN;
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

/*
 * Deep-copies a FemFunction: a fresh name, a fresh parameter array, the
 * borrowed body pointer, and the same closure. When retain_closure is true the
 * closure environment is retained (value_clone() semantics); the weak-binding
 * path passes false because the target environment itself still owns the
 * binding (see env_define). Returns NULL on allocation failure with no
 * side effects.
 */
static FemFunction *function_copy(
    const FemFunction *source,
    bool retain_closure
) {
    FemFunction *copy = malloc(sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }

    char *name = copy_string(source->name);
    char **parameters = NULL;
    if (name == NULL) {
        free(copy);
        return NULL;
    }

    if (source->parameter_count > 0) {
        parameters = calloc(source->parameter_count, sizeof(*parameters));
        if (parameters == NULL) {
            free(name);
            free(copy);
            return NULL;
        }
        for (size_t i = 0; i < source->parameter_count; i++) {
            parameters[i] = copy_string(source->parameters[i]);
            if (parameters[i] == NULL) {
                for (size_t j = 0; j < i; j++) {
                    free(parameters[j]);
                }
                free(parameters);
                free(name);
                free(copy);
                return NULL;
            }
        }
    }

    copy->name = name;
    copy->parameters = parameters;
    copy->parameter_count = source->parameter_count;
    copy->body = source->body;
    copy->closure = source->closure;
    if (retain_closure) {
        copy->closure = env_retain(source->closure);
    }
    return copy;
}

static void function_free(FemFunction *function) {
    if (function == NULL) {
        return;
    }
    for (size_t i = 0; i < function->parameter_count; i++) {
        free(function->parameters[i]);
    }
    free(function->parameters);
    free(function->name);
    env_release(function->closure);
    free(function);
}

/*
 * Builds a function value. The name and parameter strings are deep-copied,
 * the body is borrowed (owned by the program AST), and the closure is
 * retained. On allocation failure returns VALUE_NULL (see value.h).
 */
Value value_function(
    const char *name,
    char **parameters,
    size_t parameter_count,
    AstNode *body,
    Environment *closure
) {
    if (name == NULL || closure == NULL) {
        return value_null();
    }
    FemFunction *function = calloc(1, sizeof(*function));
    if (function == NULL) {
        return value_null();
    }

    function->name = copy_string(name);
    if (function->name == NULL) {
        free(function);
        return value_null();
    }

    if (parameter_count > 0) {
        function->parameters = calloc(parameter_count, sizeof(*function->parameters));
        if (function->parameters == NULL) {
            free(function->name);
            free(function);
            return value_null();
        }
        for (size_t i = 0; i < parameter_count; i++) {
            function->parameters[i] = copy_string(parameters[i]);
            if (function->parameters[i] == NULL) {
                function->parameter_count = i;
                function_free(function);
                return value_null();
            }
        }
    }

    function->parameter_count = parameter_count;
    function->body = body;
    function->closure = env_retain(closure);

    Value value;
    value.type = VALUE_FN;
    value.as.fn = function;
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
        case VALUE_FN:
            snprintf(buffer, capacity, "<fn '%s'>",
                     value->as.fn != NULL ? value->as.fn->name : "?");
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
    if (value->type == VALUE_FN) {
        FemFunction *copy = function_copy(value->as.fn, true);
        if (copy == NULL) {
            return value_null();
        }
        Value cloned;
        cloned.type = VALUE_FN;
        cloned.as.fn = copy;
        return cloned;
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
    } else if (value->type == VALUE_FN) {
        function_free(value->as.fn);
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
        case VALUE_FN:
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
        case VALUE_FN:
            if (value->as.fn != NULL && value->as.fn->name != NULL) {
                fprintf(stdout, "<fn '%s'>", value->as.fn->name);
            } else {
                fputs("<fn>", stdout);
            }
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
 * and its value copy. Environments form a parent chain (function call frames
 * link up to the closure environment that defined them). They are
 * reference-counted: the host holds one reference on the root, call frames
 * retain their parent, and function values retain their closure. Errors are
 * recorded once on the ROOT environment so every caller sees the same state;
 * the first runtime error stops evaluation everywhere.
 */

typedef struct Entry {
    char *name;
    Value value;
    bool mutable;
    struct Entry *next;
} Entry;

/*
 * Recursion guard. The tree-walking evaluator recurses through several C
 * frames per FemLang call, so the limit must stay comfortably below the C
 * stack size; 256 documented the worst case for the platforms we run on.
 */
#define MAX_CALL_DEPTH 256u

struct Environment {
    Entry *head;
    Environment *parent;
    unsigned references;
    bool call_frame;
    bool returned;
    Value return_value;
    FemNativeRegistry natives;
    unsigned call_depth;
    char error_message[256];
};

static Environment *env_root(Environment *env) {
    if (env == NULL) {
        return NULL;
    }
    while (env->parent != NULL) {
        env = env->parent;
    }
    return env;
}

static void env_record_error(Environment *env, const char *message) {
    Environment *root = env_root(env);
    if (root == NULL || root->error_message[0] != '\0') {
        return;
    }
    snprintf(root->error_message, sizeof(root->error_message), "%s", message);
}

static void env_record_error_format(
    Environment *env,
    const char *format,
    ...
) {
    Environment *root = env_root(env);
    if (root == NULL || root->error_message[0] != '\0') {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(root->error_message, sizeof(root->error_message), format, args);
    va_end(args);
}

/*
 * Finds a binding in env's own bindings only (no parent walk). Used by
 * declarations, which always create a local binding.
 */
static Entry *env_find_own(Environment *env, const char *name) {
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

/*
 * Finds a binding anywhere in the lexical chain, returning both the entry and
 * the environment that owns it.
 */
static Entry *env_find(Environment *env, const char *name, Environment **owner) {
    for (Environment *current = env; current != NULL; current = current->parent) {
        Entry *entry = env_find_own(current, name);
        if (entry != NULL) {
            if (owner != NULL) {
                *owner = current;
            }
            return entry;
        }
    }
    return NULL;
}

static Environment *env_nearest_call_frame(Environment *env) {
    for (Environment *current = env; current != NULL; current = current->parent) {
        if (current->call_frame) {
            return current;
        }
    }
    return NULL;
}

Environment *env_new(void) {
    Environment *env = calloc(1, sizeof(Environment));
    if (env != NULL) {
        native_registry_init(&env->natives);
        env->references = 1;
    }
    return env;
}

/*
 * Creates a child environment whose parent is `closure`. The child retains
 * the parent so the closure chain stays alive while the child is referenced.
 * Used for function call frames (call_frame=true) and only reachable from
 * call machinery, so it is not exposed in evaluator.h.
 */
static Environment *env_new_child(Environment *closure, bool call_frame) {
    if (closure == NULL) {
        return NULL;
    }
    Environment *env = env_new();
    if (env == NULL) {
        return NULL;
    }
    env->parent = env_retain(closure);
    env->call_frame = call_frame;
    return env;
}

Environment *env_retain(Environment *env) {
    if (env != NULL) {
        env->references++;
    }
    return env;
}

static void env_destroy(Environment *env) {
    Entry *entry = env->head;
    while (entry != NULL) {
        Entry *next = entry->next;
        free(entry->name);
        value_free(&entry->value);
        free(entry);
        entry = next;
    }
    env->head = NULL;

    if (env->returned && env->return_value.type != VALUE_NULL) {
        value_free(&env->return_value);
    }
    env->returned = false;
    env->return_value = value_null();

    if (env->parent != NULL) {
        Environment *parent = env->parent;
        env->parent = NULL;
        env_release(parent);
    }
    native_registry_free(&env->natives);
    free(env);
}

void env_release(Environment *env) {
    if (env == NULL) {
        return;
    }
    /*
     * references is how many live owners keep this environment. The binding
     * values that call env_release() on the environment being destroyed are
     * released as part of that destruction; once references reaches zero the
     * environment is gone, so further releases (from weak self-bindings whose
     * closure is this very environment) are no-ops.
     */
    if (env->references > 1) {
        env->references--;
        return;
    }
    if (env->references == 1) {
        env->references = 0;
        env_destroy(env);
    }
}

/* Host-side release. env_free() keeps its historical name. */
void env_free(Environment *env) {
    env_release(env);
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

    /*
     * Values are deep-copied into the binding. A function value closes over
     * some environment; if that environment is the one accepting the binding
     * (recursive/sibling definitions, top-level functions), the copy is weak:
     * it does not retain the closure, so a function never keeps its own
     * defining scope alive. Any other closure is retained normally so values
     * escaping their frame keep it alive.
     */
    bool weak = value != NULL && value->type == VALUE_FN &&
                value->as.fn != NULL && value->as.fn->closure == env;
    Value value_copy;
    if (weak) {
        FemFunction *copy = function_copy(value->as.fn, false);
        if (copy == NULL) {
            free(name_copy);
            env_record_error(env, "out of memory");
            return false;
        }
        value_copy.type = VALUE_FN;
        value_copy.as.fn = copy;
    } else {
        value_copy = value_clone(value);
    }

    if (value != NULL && value_heap_owning(value->type) &&
        value_copy.type != value->type) {
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
    Entry *entry = env_find(env, name, NULL);
    if (entry == NULL) {
        return NULL;
    }
    return &entry->value;
}

/*
 * Native lookup walks the lexical chain because only the root registry is
 * normally populated; a frame environment still resolves `print` through its
 * parent chain up to the root.
 */
static FemNativeFunction env_lookup_native(const Environment *env, const char *name) {
    for (const Environment *current = env; current != NULL; current = current->parent) {
        FemNativeFunction function = native_lookup(&current->natives, name);
        if (function != NULL) {
            return function;
        }
    }
    return NULL;
}

bool env_assign(Environment *env, const char *name, const Value *value) {
    if (env == NULL || name == NULL || value == NULL) {
        return false;
    }

    Environment *owner = NULL;
    Entry *entry = env_find(env, name, &owner);
    if (entry == NULL) {
        env_record_error_format(env, "cannot assign to undefined variable '%s'", name);
        return false;
    }
    if (!entry->mutable) {
        env_record_error_format(env, "cannot reassign immutable variable '%s'", name);
        return false;
    }

    bool weak = value->type == VALUE_FN && value->as.fn != NULL &&
                value->as.fn->closure == owner;
    Value copy;
    if (weak) {
        FemFunction *copy_fn = function_copy(value->as.fn, false);
        if (copy_fn == NULL) {
            env_record_error(env, "out of memory");
            return false;
        }
        copy.type = VALUE_FN;
        copy.as.fn = copy_fn;
    } else {
        copy = value_clone(value);
    }
    if (value_heap_owning(value->type) && copy.type != value->type) {
        env_record_error(env, "out of memory");
        return false;
    }

    value_free(&entry->value);
    entry->value = copy;
    return true;
}

const char *env_error(const Environment *env) {
    const Environment *root = env;
    if (root != NULL) {
        while (root->parent != NULL) {
            root = root->parent;
        }
    }
    if (root == NULL || root->error_message[0] == '\0') {
        return NULL;
    }
    return root->error_message;
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

/*
 * A user-defined (VALUE_FN) call: creates a frame environment parented to the
 * function's closure, binds parameters, evaluates the body under the recursion
 * guard, and returns the function's return value (null when none was
 * produced). The argument values are supplied as a borrowed array owned by the
 * caller; parameters are deployed as cloned bindings.
 */
static Value eval_function_call(
    Environment *caller,
    const Value *callee,
    size_t count,
    const Value *arguments
) {
    const FemFunction *function = callee->as.fn;
    if (count != function->parameter_count) {
        env_record_error_format(caller,
                                "function '%s' expects %zu arguments, got %zu",
                                function->name,
                                function->parameter_count, count);
        return value_null();
    }

    Environment *root = env_root(caller);
    if (root->call_depth >= MAX_CALL_DEPTH) {
        env_record_error(caller, "recursion limit exceeded");
        return value_null();
    }

    Environment *frame = env_new_child(function->closure, true);
    if (frame == NULL) {
        env_record_error(caller, "out of memory");
        return value_null();
    }
    root->call_depth++;

    for (size_t i = 0; i < count; i++) {
        if (!env_define(frame, function->parameters[i], &arguments[i], false)) {
            break;
        }
    }

    Value result = value_null();
    if (env_error(caller) == NULL) {
        Value body_result = eval(frame, function->body);
        value_free(&body_result);
        if (frame->returned) {
            result = frame->return_value;
            frame->return_value = value_null();
            frame->returned = false;
        }
    }

    root->call_depth--;
    env_free(frame);
    return result;
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
                env_lookup_native(env, node->as.identifier);
            if (function != NULL) {
                return value_native(function);
            }
            env_record_error_format(env, "undefined variable '%s'",
                                    node->as.identifier);
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

        case AST_RETURN: {
            Environment *frame = env_nearest_call_frame(env);
            if (frame == NULL) {
                env_record_error(env, "return outside a function");
                return value_null();
            }
            Value returned = value_null();
            if (node->as.return_statement.value != NULL) {
                returned = eval(env, node->as.return_statement.value);
            }
            if (frame->returned) {
                value_free(&returned);
            } else {
                frame->return_value = returned;
                frame->returned = true;
            }
            return value_null();
        }

        case AST_FUNCTION: {
            const AstNodeList *params = &node->as.function.parameters;
            char **parameter_names = NULL;
            if (params->count > 0) {
                parameter_names = malloc(params->count * sizeof(*parameter_names));
                if (parameter_names == NULL) {
                    env_record_error(env, "out of memory");
                    return value_null();
                }
                for (size_t i = 0; i < params->count; i++) {
                    parameter_names[i] = params->items[i]->as.identifier;
                }
            }
            Value function = value_function(
                node->as.function.name,
                parameter_names,
                params->count,
                node->as.function.body,
                env);
            free(parameter_names);
            if (function.type != VALUE_FN) {
                env_record_error(env, "out of memory");
                return value_null();
            }
            if (!env_define(env, node->as.function.name, &function, false)) {
                value_free(&function);
                return value_null();
            }
            return function;
        }

        case AST_EXPRESSION_STATEMENT:
            return eval(env, node->as.expression_statement.expression);

        case AST_PROGRAM:
        case AST_BLOCK: {
            Value last = value_null();
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                Value current = eval(env, node->as.block.statements.items[i]);
                value_free(&last);
                last = current;
                if (env->call_frame && env->returned) {
                    break;
                }
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
            if (callee.type != VALUE_NATIVE && callee.type != VALUE_FN) {
                value_free(&callee);
                env_record_error(env, "attempt to call a non-function value");
                return value_null();
            }

            size_t count = node->as.call.arguments.count;
            Value *arguments = NULL;
            if (count > 0) {
                arguments = malloc(count * sizeof(*arguments));
                if (arguments == NULL) {
                    value_free(&callee);
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
                    value_free(&callee);
                    return value_null();
                }
                arguments[evaluated++] = argument;
            }

            Value result;
            if (callee.type == VALUE_NATIVE) {
                FemNativeFunction function = callee.as.function;
                value_free(&callee);
                result = function(count, arguments);
            } else {
                result = eval_function_call(env, &callee, count, arguments);
                value_free(&callee);
            }
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