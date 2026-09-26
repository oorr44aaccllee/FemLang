#ifndef FEMLANG_BUILTINS_H
#define FEMLANG_BUILTINS_H

#include "native.h"

/*
 * FemLang standard library
 * ------------------------
 * fem_stdlib_register() registers the built-in native functions into a
 * registry (normally the environment's registry via env_native_registry()):
 *
 *   print(a, b, ...)  prints each argument (strings raw, other values in
 *                     their display form) separated by a space, ends with a
 *                     newline, returns null
 *   len(s)            byte length of a string
 *   int(x)            truncates a float toward zero; bool -> 0/1; identity
 *                     for integers; anything else is an error
 *   float(x)          int -> double; bool -> 0.0/1.0; identity for floats
 *   str(x)            display form of any value as a string (strings are
 *                     returned unchanged, raw bytes, no quotes)
 *   type(x)           "null" "bool" "int" "float" "string" "native" "error"
 *
 * Natives report errors by returning a VALUE_ERROR value (see value.h); the
 * evaluator turns that into the environment error channel and stops the
 * program. Registration never fails silently: it returns the registry so the
 * caller can free it, and each builtin is either registered or the host
 * program failed allocation earlier.
 */
void fem_stdlib_register(FemNativeRegistry *registry);

#endif