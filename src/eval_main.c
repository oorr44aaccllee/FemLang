#include "evaluator.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
/* Existing file-reading and CLI behavior is retained in the evaluator driver. */
int main(int argc,char**argv){if(argc!=2){fprintf(stderr,"Usage: %s <source-file>\n",argv[0]);return EXIT_FAILURE;}FILE*f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return EXIT_FAILURE;}fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);if(n<0){fclose(f);return EXIT_FAILURE;}char*s=malloc((size_t)n+1U);if(!s){fclose(f);return EXIT_FAILURE;}size_t got=fread(s,1,(size_t)n,f);fclose(f);if(got!=(size_t)n){free(s);return EXIT_FAILURE;}s[n]='\0';Parser p;parser_init(&p,s,(size_t)n);AstNode*root=parser_parse(&p);if(!root){fprintf(stderr,"Parse error: %s\n",parser_error(&p));free(s);return EXIT_FAILURE;}Environment*e=env_new();Value v=eval_ast(e,root);print_value(&v);putchar('\n');value_free(&v);env_free(e);ast_free(root);free(s);return EXIT_SUCCESS;}
