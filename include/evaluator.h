#ifndef FEMLANG_EVALUATOR_H
#define FEMLANG_EVALUATOR_H
#include "ast.h"
#include "value.h"
typedef struct Environment Environment;
Environment *env_new(void);
void env_free(Environment *env);
void env_define(Environment *env, const char *name, Value value);
Value *env_lookup(Environment *env, const char *name);
Value eval_ast(Environment *env, const AstNode *node);
#endif
