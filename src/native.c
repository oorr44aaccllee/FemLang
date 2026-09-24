#include "native.h"

#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *source) {
    size_t length = strlen(source);
    char *copy = malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, source, length + 1U);
    }
    return copy;
}

void native_registry_init(FemNativeRegistry *registry) {
    if (registry == NULL) {
        return;
    }

    registry->bindings = NULL;
    registry->count = 0;
    registry->capacity = 0;
}

void native_registry_free(FemNativeRegistry *registry) {
    if (registry == NULL) {
        return;
    }

    for (size_t i = 0; i < registry->count; i++) {
        free(registry->bindings[i].name);
    }

    free(registry->bindings);
    native_registry_init(registry);
}

bool native_register(
    FemNativeRegistry *registry,
    const char *name,
    FemNativeFunction function
) {
    if (registry == NULL || name == NULL || function == NULL) {
        return false;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->bindings[i].name, name) == 0) {
            registry->bindings[i].function = function;
            return true;
        }
    }

    if (registry->count == registry->capacity) {
        size_t new_capacity = registry->capacity == 0
            ? 8U
            : registry->capacity * 2U;
        FemNativeBinding *new_bindings = realloc(
            registry->bindings,
            new_capacity * sizeof(*new_bindings)
        );

        if (new_bindings == NULL) {
            return false;
        }

        registry->bindings = new_bindings;
        registry->capacity = new_capacity;
    }

    char *name_copy = copy_string(name);
    if (name_copy == NULL) {
        return false;
    }

    registry->bindings[registry->count].name = name_copy;
    registry->bindings[registry->count].function = function;
    registry->count++;
    return true;
}

FemNativeFunction native_lookup(
    const FemNativeRegistry *registry,
    const char *name
) {
    if (registry == NULL || name == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->bindings[i].name, name) == 0) {
            return registry->bindings[i].function;
        }
    }

    return NULL;
}
