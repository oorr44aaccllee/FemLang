#ifndef FEMLANG_NATIVE_H
#define FEMLANG_NATIVE_H

#include "value.h"

#include <stdbool.h>
#include <stddef.h>

typedef Value (*FemNativeFunction)(size_t argument_count, const Value *arguments);

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
bool native_register(FemNativeRegistry *registry, const char *name, FemNativeFunction function);
FemNativeFunction native_lookup(const FemNativeRegistry *registry, const char *name);

#endif
