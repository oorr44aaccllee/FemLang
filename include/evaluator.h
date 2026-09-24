#ifndef FEMLANG_EVALUATOR_H
#define FEMLANG_EVALUATOR_H
#include "ast.h"
#include "value.h"
typedef struct Environment Environment;
Environment*env_new(void);void env_free(Environment*);
bool env_define(Environment*,const char*,Value,bool mutable);
Value*env_lookup(Environment*,const char*);
bool env_assign(Environment*,const char*,const Value*);
Value eval_ast(Environment*,const AstNode*);
#endif
