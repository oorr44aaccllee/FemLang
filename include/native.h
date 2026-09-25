#ifndef FEMLANG_NATIVE_H
#define FEMLANG_NATIVE_H

#include "value.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Native C function registry
 * --------------------------
 *
 * FemNativeFunction (a C callback type) is defined in value.h; a registry
 * maps plain C callbacks to names. It is usable both directly and through the
 * environment once call expressions are wired up.
 *
 *   - native_register() copies the name; the registry owns the copy.
 *   - Registering an existing name replaces the previous function and keeps
 *     the original name copy (no churn, registry count is unchanged).
 *   - The registry grows by reallocation without losing existing entries.
 *   - NULL names, NULL function pointers, and NULL registries are rejected.
 *   - native_registry_free() releases every copied name and can be called on
 *     an already-initialized/freed registry (it re-initializes).
 */

typedef struct {
    char *name;
    FemNativeFunction function;
} FemNativeBinding;

typedef struct {
    FemNativeBinding *bindings;
    size_t count;
    size_t capacity;
} FemNativeRegistry;

void native_registry_init(FemNativeRegistry *registry);
void native_registry_free(FemNativeRegistry *registry);

bool native_register(
    FemNativeRegistry *registry,
    const char *name,
    FemNativeFunction function
);

FemNativeFunction native_lookup(
    const FemNativeRegistry *registry,
    const char *name
);

#endif