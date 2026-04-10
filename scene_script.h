#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#pragma region Value

typedef enum {
	SS_VAL_NIL,
	SS_VAL_BOOL,
	SS_VAL_NUMBER,
	SS_VAL_STRING,
} SS_value_type;

typedef struct {
	SS_value_type type;
	union {
		bool boolean;
		double number;
		char *string;
	};
} SS_value;

SS_value SS_nil_value(void);
SS_value SS_bool_value(bool b);
SS_value SS_number_value(double n);
SS_value SS_string_value(const char *s);
void SS_value_free(SS_value *v);
bool SS_value_is_boolean(const SS_value *v);
bool SS_value_bool(const SS_value *v);
double SS_value_number(const SS_value *v);
const char *SS_value_string(const SS_value *v);
int SS_value_sprint(const SS_value *v, char *buf, size_t len);

#pragma endregion

#pragma region Token

typedef enum {
	SS_TOK_INVALID,
	SS_TOK_SCRIPT,
	SS_TOK_NUMBER,
	SS_TOK_STRING,
	SS_TOK_WORD,
	SS_TOK_MINUS,
	SS_TOK_NOT,
	SS_TOK_AND,
	SS_TOK_OR,
	SS_TOK_IS,
	SS_TOK_GT,
	SS_TOK_GTE,
	SS_TOK_LT,
	SS_TOK_LTE,
	SS_TOK_COMMA,
	SS_TOK_COLON,
	SS_TOK_LPAREN,
	SS_TOK_RPAREN,
	SS_TOK_INDENT,
} SS_token_type;

typedef struct {
	SS_token_type type;
	char *literal;
} SS_token;

const char *SS_token_type_str(SS_token_type t);

/* Lexer: tokenise a source string. Caller must free *out with SS_tokens_free. */
int SS_lex(const char *src, SS_token **out, size_t *out_len);
void SS_tokens_free(SS_token *tokens, size_t len);

#pragma endregion

#pragma region AST

typedef enum {
	SS_KEY_INVALID,
	SS_KEY_SCRIPT,
	SS_KEY_IF,
	SS_KEY_ELIF,
	SS_KEY_ELSE,
	SS_KEY_END,
	SS_KEY_RUN,
	SS_KEY_EQ,
	SS_KEY_TRUE,
	SS_KEY_FALSE,
} SS_keyword;

typedef enum {
	SS_STMT_INVALID,
	SS_STMT_EXPRESSION,
	SS_STMT_END,
	SS_STMT_RUN,
	SS_STMT_IF,
	SS_STMT_ELIF,
	SS_STMT_ELSE,
} SS_stmt_type;

typedef enum {
	SS_EXP_EMPTY,
	SS_EXP_BOOL,
	SS_EXP_NUMBER,
	SS_EXP_STRING,
	SS_EXP_WORD,
	SS_EXP_PREFIX,
	SS_EXP_INFIX,
	SS_EXP_CALL,
} SS_exp_type;

typedef struct SS_expression {
	SS_exp_type type;
	char *value;
	SS_token_type op;
	struct SS_expression **children;
	size_t children_len;
} SS_expression;

typedef struct SS_statement SS_statement;

typedef struct {
	SS_statement *stmts;
	size_t stmts_len;
} SS_block;

struct SS_statement {
	SS_stmt_type type;
	SS_expression *expression;
	SS_block *block;
	SS_statement **alternatives;
	size_t alts_len;
};

typedef struct {
	char *name;
	SS_block block;
} SS_script;

SS_keyword SS_literal_to_keyword(const char *s);

/* Parse an array of tokens into scripts. Caller must free with SS_scripts_free. */
int SS_parse(const SS_token *tokens, size_t tokens_len,
	SS_script **out, size_t *out_len);
void SS_scripts_free(SS_script *scripts, size_t len);

#pragma endregion

#pragma region Print

void SS_print_script(const SS_script *s);
int SS_sprint_expression(const SS_expression *e, char *buf, size_t len);

#pragma endregion

#pragma region Evaluator

typedef struct {
	const char *name;
	SS_value *args;
	size_t args_len;
} SS_call;

/* Callback invoked when a script calls an external function.
   Return 0 on success (write result into *result), non-zero on error. */
typedef int (*SS_call_fn)(const SS_call *call, SS_value *result, void *userdata);

typedef struct {
	char *name;
	SS_value value;
} SS_global;

typedef struct {
	SS_script *scripts;
	size_t scripts_len;
	SS_global *globals;
	size_t globals_len;
	size_t globals_cap;
} SS_program;

/* Initialise a program from source text. Returns 0 on success. */
int SS_program_init(SS_program *p, const char *src);
void SS_program_free(SS_program *p);

/* Set / get globals. */
void SS_program_set_global(SS_program *p, const char *name, SS_value v);
const SS_value *SS_program_get_global(const SS_program *p, const char *name);

/* Run a named script synchronously. Returns 0 on success. */
int SS_program_run(SS_program *p, const char *name,
	SS_call_fn fn, void *userdata);

#pragma endregion

#pragma region Context

typedef enum {
	SS_STATUS_DONE,
	SS_STATUS_CALL,
	SS_STATUS_ERROR,
} SS_status;

typedef struct SS_context SS_context;

/* Create a run context for a named script. Returns NULL on error. */
SS_context *SS_context_create(SS_program *p, const char *script_name);

/* Advance execution until the next call or completion. */
SS_status SS_context_step(SS_context *ctx);

/* When step returns SS_STATUS_CALL, get the pending call info. */
const SS_call *SS_context_get_call(const SS_context *ctx);

/* Provide the result for the pending call, then call step again. */
void SS_context_set_result(SS_context *ctx, SS_value result);

/* Free the context and all internal state. */
void SS_context_free(SS_context *ctx);

#pragma endregion
