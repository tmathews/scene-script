#include <stdio.h>

#include "internal.h"

/* ── Value ──────────────────────────────────────────────────────────── */

SS_value SS_nil_value(void) {
	return (SS_value){.type = SS_VAL_NIL};
}

SS_value SS_bool_value(bool b) {
	return (SS_value){.type = SS_VAL_BOOL, .boolean = b};
}

SS_value SS_number_value(double n) {
	return (SS_value){.type = SS_VAL_NUMBER, .number = n};
}

SS_value SS_string_value(const char *s) {
	return (SS_value){.type = SS_VAL_STRING, .string = ss_str_dup(s)};
}

void SS_value_free(SS_value *v) {
	if (v->type == SS_VAL_STRING) {
		free(v->string);
		v->string = NULL;
	}
	v->type = SS_VAL_NIL;
}

bool SS_value_is_boolean(const SS_value *v) {
	return v->type == SS_VAL_NIL || v->type == SS_VAL_BOOL ||
	       v->type == SS_VAL_NUMBER;
}

bool SS_value_bool(const SS_value *v) {
	switch (v->type) {
	case SS_VAL_NIL:
		return false;
	case SS_VAL_BOOL:
		return v->boolean;
	case SS_VAL_NUMBER:
		return v->number != 0.0;
	default:
		return false;
	}
}

double SS_value_number(const SS_value *v) {
	return v->number;
}
const char *SS_value_string(const SS_value *v) {
	return v->string;
}

int SS_value_sprint(const SS_value *v, char *buf, size_t len) {
	switch (v->type) {
	case SS_VAL_NIL:
		return snprintf(buf, len, "(nil)");
	case SS_VAL_BOOL:
		return snprintf(buf, len, "%s", v->boolean ? "TRUE" : "FALSE");
	case SS_VAL_NUMBER:
		return snprintf(buf, len, "%.05f", v->number);
	case SS_VAL_STRING:
		return snprintf(buf, len, "%s", v->string);
	}
	return snprintf(buf, len, "Invalid Type");
}

/* ── Evaluator (stack machine) ─────────────────────────────────────── */

typedef enum { FRAME_BLOCK,
	FRAME_COND,
	FRAME_EXPR } frame_type;

enum {
	COND_EVAL_MAIN = 0,
	COND_CHECK_MAIN,
	COND_BODY,
	COND_EVAL_ALT,
	COND_CHECK_ALT,
};

typedef struct {
	frame_type type;
	union {
		struct {
			const SS_block *block;
			size_t index;
			bool awaiting;
		} block;
		struct {
			const SS_statement *stmt;
			int phase;
			size_t alt_index;
		} cond;
		struct {
			const SS_expression *expr;
			int phase;
			SS_value *args;
			size_t args_done;
			size_t argc;
			SS_value left;
			bool has_left;
		} expr;
	};
} frame;

struct SS_context {
	SS_program *program;
	frame *stack;
	size_t stack_len;
	size_t stack_cap;
	SS_value result;
	SS_call pending_call;
	bool done;
	bool error;
};

static void frame_cleanup(frame *f) {
	if (f->type != FRAME_EXPR)
		return;
	if (f->expr.args) {
		for (size_t i = 0; i < f->expr.args_done; i++)
			SS_value_free(&f->expr.args[i]);
		free(f->expr.args);
		f->expr.args = NULL;
	}
	if (f->expr.has_left) {
		SS_value_free(&f->expr.left);
		f->expr.has_left = false;
	}
}

static void push_frame(SS_context *ctx, frame f) {
	if (ctx->stack_len == ctx->stack_cap) {
		ctx->stack_cap = ctx->stack_cap ? ctx->stack_cap * 2 : 8;
		ctx->stack = realloc(ctx->stack, ctx->stack_cap * sizeof(frame));
	}
	ctx->stack[ctx->stack_len++] = f;
}

static void pop_frame(SS_context *ctx) {
	if (ctx->stack_len == 0)
		return;
	ctx->stack_len--;
	frame_cleanup(&ctx->stack[ctx->stack_len]);
}

static void clear_stack(SS_context *ctx) {
	while (ctx->stack_len > 0)
		pop_frame(ctx);
}

static void push_block_frame(SS_context *ctx, const SS_block *block) {
	frame f = {.type = FRAME_BLOCK,
		.block = {.block = block, .index = 0, .awaiting = false}};
	push_frame(ctx, f);
}

static void push_cond_frame(SS_context *ctx, const SS_statement *stmt) {
	frame f = {.type = FRAME_COND,
		.cond = {.stmt = stmt, .phase = COND_EVAL_MAIN, .alt_index = 0}};
	push_frame(ctx, f);
}

static void push_expr_frame(SS_context *ctx, const SS_expression *expr) {
	frame f = {.type = FRAME_EXPR, .expr = {.expr = expr, .phase = 0}};
	push_frame(ctx, f);
}

static const SS_script *find_script(const SS_program *p, const char *name) {
	for (size_t i = 0; i < p->scripts_len; i++)
		if (strcmp(p->scripts[i].name, name) == 0)
			return &p->scripts[i];
	return NULL;
}

static int resolve_leaf(SS_context *ctx, const SS_expression *expr,
	SS_value *out) {
	switch (expr->type) {
	case SS_EXP_BOOL:
		*out = SS_bool_value(strcmp(expr->value, "true") == 0);
		return 0;
	case SS_EXP_NUMBER: {
		char *end;
		double d = strtod(expr->value, &end);
		(void)end;
		*out = SS_number_value(d);
		return 0;
	}
	case SS_EXP_STRING:
		*out = SS_string_value(expr->value);
		return 0;
	case SS_EXP_WORD: {
		const SS_value *g = SS_program_get_global(ctx->program, expr->value);
		if (!g)
			return -1;
		if (g->type == SS_VAL_STRING)
			*out = SS_string_value(g->string);
		else
			*out = *g;
		return 0;
	}
	default:
		return -1;
	}
}

static bool is_leaf(const SS_expression *expr) {
	return expr->type == SS_EXP_BOOL || expr->type == SS_EXP_NUMBER ||
	       expr->type == SS_EXP_STRING || expr->type == SS_EXP_WORD;
}

/* Process the top EXPR frame. Returns 0=continue, 1=yield, -1=error. */
static int step_expr(SS_context *ctx) {
	frame *f = &ctx->stack[ctx->stack_len - 1];
	const SS_expression *expr = f->expr.expr;

	if (is_leaf(expr)) {
		if (resolve_leaf(ctx, expr, &ctx->result) != 0)
			return -1;
		pop_frame(ctx);
		return 0;
	}

	switch (expr->type) {
	case SS_EXP_PREFIX: {
		if (f->expr.phase == 0) {
			f->expr.phase = 1;
			push_expr_frame(ctx, expr->children[0]);
			return 0;
		}
		SS_value child = ctx->result;
		ctx->result = SS_nil_value();
		int rc = -1;
		switch (expr->op) {
		case SS_TOK_MINUS:
			if (child.type != SS_VAL_NUMBER) {
				SS_value_free(&child);
				return -1;
			}
			ctx->result = SS_number_value(child.number * -1.0);
			rc = 0;
			break;
		case SS_TOK_NOT:
			if (!SS_value_is_boolean(&child)) {
				SS_value_free(&child);
				return -1;
			}
			ctx->result = SS_bool_value(!SS_value_bool(&child));
			rc = 0;
			break;
		default:
			break;
		}
		SS_value_free(&child);
		if (rc != 0)
			return -1;
		pop_frame(ctx);
		return 0;
	}

	case SS_EXP_INFIX: {
		/* children[1]=left, children[0]=right */
		if (f->expr.phase == 0) {
			f->expr.has_left = false;
			f->expr.phase = 1;
			push_expr_frame(ctx, expr->children[1]);
			return 0;
		}
		if (f->expr.phase == 1) {
			SS_value left = ctx->result;
			ctx->result = SS_nil_value();
			if (!SS_value_is_boolean(&left)) {
				SS_value_free(&left);
				return -1;
			}
			bool lb = SS_value_bool(&left);
			if ((expr->op == SS_TOK_AND && !lb) || (expr->op == SS_TOK_OR && lb)) {
				ctx->result = SS_bool_value(lb);
				SS_value_free(&left);
				pop_frame(ctx);
				return 0;
			}
			f->expr.left = left;
			f->expr.has_left = true;
			f->expr.phase = 2;
			push_expr_frame(ctx, expr->children[0]);
			return 0;
		}
		SS_value right = ctx->result;
		ctx->result = SS_nil_value();
		SS_value left = f->expr.left;
		f->expr.has_left = false;
		if (!SS_value_is_boolean(&right)) {
			SS_value_free(&left);
			SS_value_free(&right);
			return -1;
		}
		bool lb = SS_value_bool(&left);
		bool rb = SS_value_bool(&right);
		SS_value_free(&left);
		SS_value_free(&right);
		switch (expr->op) {
		case SS_TOK_AND:
			ctx->result = SS_bool_value(lb && rb);
			break;
		case SS_TOK_OR:
			ctx->result = SS_bool_value(lb || rb);
			break;
		default:
			return -1;
		}
		pop_frame(ctx);
		return 0;
	}

	case SS_EXP_CALL: {
		size_t argc = expr->children_len > 0 ? expr->children_len - 1 : 0;

		if (f->expr.phase == 0) {
			f->expr.argc = argc;
			f->expr.args_done = 0;
			f->expr.args = argc > 0 ? calloc(argc, sizeof(SS_value)) : NULL;
			if (argc == 0) {
				f->expr.phase = 2;
			} else {
				f->expr.phase = 1;
				push_expr_frame(ctx, expr->children[1]);
				return 0;
			}
		}

		if (f->expr.phase == 1) {
			f->expr.args[f->expr.args_done] = ctx->result;
			ctx->result = SS_nil_value();
			f->expr.args_done++;
			if (f->expr.args_done < f->expr.argc) {
				push_expr_frame(ctx, expr->children[f->expr.args_done + 1]);
				return 0;
			}
			f->expr.phase = 2;
		}

		if (f->expr.phase == 2) {
			ctx->pending_call.name = expr->children[0]->value;
			ctx->pending_call.args = f->expr.args;
			ctx->pending_call.args_len = f->expr.argc;
			f->expr.phase = 3;
			return 1; /* yield */
		}

		/* phase 3: resumed after yield */
		for (size_t i = 0; i < f->expr.argc; i++)
			SS_value_free(&f->expr.args[i]);
		free(f->expr.args);
		f->expr.args = NULL;
		f->expr.args_done = 0;
		pop_frame(ctx);
		return 0;
	}

	default:
		return -1;
	}
}

/* Process the top BLOCK frame. Returns 0=continue, 1=yield, -1=error. */
static int step_block(SS_context *ctx) {
	frame *f = &ctx->stack[ctx->stack_len - 1];

	if (f->block.awaiting) {
		const SS_statement *stmt = &f->block.block->stmts[f->block.index];
		if (stmt->type == SS_STMT_EXPRESSION) {
			SS_value_free(&ctx->result);
		}
		ctx->result = SS_nil_value();
		f->block.awaiting = false;
		f->block.index++;
	}

	if (f->block.index >= f->block.block->stmts_len) {
		pop_frame(ctx);
		return 0;
	}

	const SS_statement *stmt = &f->block.block->stmts[f->block.index];
	switch (stmt->type) {
	case SS_STMT_EXPRESSION:
		f->block.awaiting = true;
		push_expr_frame(ctx, stmt->expression);
		return 0;
	case SS_STMT_END:
		clear_stack(ctx);
		ctx->done = true;
		return 0;
	case SS_STMT_RUN: {
		const char *target = stmt->expression->value;
		clear_stack(ctx);
		const SS_script *script = find_script(ctx->program, target);
		if (!script)
			return -1;
		push_block_frame(ctx, &script->block);
		return 0;
	}
	case SS_STMT_IF:
		f->block.awaiting = true;
		push_cond_frame(ctx, stmt);
		return 0;
	default:
		return -1;
	}
}

/* Process the top COND frame. Returns 0=continue, -1=error. */
static int step_cond(SS_context *ctx) {
	frame *f = &ctx->stack[ctx->stack_len - 1];

	switch (f->cond.phase) {
	case COND_EVAL_MAIN:
		f->cond.phase = COND_CHECK_MAIN;
		push_expr_frame(ctx, f->cond.stmt->expression);
		return 0;

	case COND_CHECK_MAIN: {
		SS_value res = ctx->result;
		ctx->result = SS_nil_value();
		if (!SS_value_is_boolean(&res)) {
			SS_value_free(&res);
			return -1;
		}
		if (SS_value_bool(&res)) {
			SS_value_free(&res);
			f->cond.phase = COND_BODY;
			push_block_frame(ctx, f->cond.stmt->block);
			return 0;
		}
		SS_value_free(&res);
		f->cond.alt_index = 0;
		f->cond.phase = COND_EVAL_ALT;
		return 0;
	}

	case COND_BODY:
		pop_frame(ctx);
		return 0;

	case COND_EVAL_ALT: {
		if (f->cond.alt_index >= f->cond.stmt->alts_len) {
			pop_frame(ctx);
			return 0;
		}
		const SS_statement *alt = f->cond.stmt->alternatives[f->cond.alt_index];
		if (alt->type == SS_STMT_ELSE) {
			f->cond.phase = COND_BODY;
			push_block_frame(ctx, alt->block);
			return 0;
		}
		f->cond.phase = COND_CHECK_ALT;
		push_expr_frame(ctx, alt->expression);
		return 0;
	}

	case COND_CHECK_ALT: {
		SS_value res = ctx->result;
		ctx->result = SS_nil_value();
		if (!SS_value_is_boolean(&res)) {
			SS_value_free(&res);
			return -1;
		}
		if (SS_value_bool(&res)) {
			SS_value_free(&res);
			const SS_statement *alt = f->cond.stmt->alternatives[f->cond.alt_index];
			f->cond.phase = COND_BODY;
			push_block_frame(ctx, alt->block);
			return 0;
		}
		SS_value_free(&res);
		f->cond.alt_index++;
		f->cond.phase = COND_EVAL_ALT;
		return 0;
	}

	default:
		return -1;
	}
}

/* ── Context API ───────────────────────────────────────────────────── */

SS_context *SS_context_create(SS_program *p, const char *script_name) {
	const SS_script *script = find_script(p, script_name);
	if (!script)
		return NULL;
	SS_context *ctx = calloc(1, sizeof(SS_context));
	ctx->program = p;
	ctx->result = SS_nil_value();
	push_block_frame(ctx, &script->block);
	return ctx;
}

SS_status SS_context_step(SS_context *ctx) {
	if (ctx->done)
		return SS_STATUS_DONE;
	if (ctx->error)
		return SS_STATUS_ERROR;

	while (ctx->stack_len > 0) {
		frame *top = &ctx->stack[ctx->stack_len - 1];
		int rc;
		switch (top->type) {
		case FRAME_BLOCK:
			rc = step_block(ctx);
			break;
		case FRAME_COND:
			rc = step_cond(ctx);
			break;
		case FRAME_EXPR:
			rc = step_expr(ctx);
			break;
		default:
			rc = -1;
			break;
		}
		if (rc == 1)
			return SS_STATUS_CALL;
		if (rc < 0) {
			ctx->error = true;
			clear_stack(ctx);
			return SS_STATUS_ERROR;
		}
		if (ctx->done)
			return SS_STATUS_DONE;
	}

	ctx->done = true;
	return SS_STATUS_DONE;
}

const SS_call *SS_context_get_call(const SS_context *ctx) {
	return &ctx->pending_call;
}

void SS_context_set_result(SS_context *ctx, SS_value result) {
	ctx->result = result;
}

void SS_context_free(SS_context *ctx) {
	if (!ctx)
		return;
	clear_stack(ctx);
	SS_value_free(&ctx->result);
	free(ctx->stack);
	free(ctx);
}

/* ── Program ────────────────────────────────────────────────────────── */

int SS_program_init(SS_program *p, const char *src) {
	memset(p, 0, sizeof(*p));
	SS_token *tokens = NULL;
	size_t tokens_len = 0;
	if (SS_lex(src, &tokens, &tokens_len) != 0)
		return -1;
	int rc = SS_parse(tokens, tokens_len, &p->scripts, &p->scripts_len);
	SS_tokens_free(tokens, tokens_len);
	return rc;
}

void SS_program_free(SS_program *p) {
	SS_scripts_free(p->scripts, p->scripts_len);
	p->scripts = NULL;
	p->scripts_len = 0;
	for (size_t i = 0; i < p->globals_len; i++) {
		free(p->globals[i].name);
		SS_value_free(&p->globals[i].value);
	}
	free(p->globals);
	p->globals = NULL;
	p->globals_len = 0;
	p->globals_cap = 0;
}

void SS_program_set_global(SS_program *p, const char *name, SS_value v) {
	for (size_t i = 0; i < p->globals_len; i++) {
		if (strcmp(p->globals[i].name, name) == 0) {
			SS_value_free(&p->globals[i].value);
			p->globals[i].value = v;
			return;
		}
	}
	if (p->globals_len == p->globals_cap) {
		p->globals_cap = p->globals_cap ? p->globals_cap * 2 : 8;
		p->globals = realloc(p->globals, p->globals_cap * sizeof(SS_global));
	}
	p->globals[p->globals_len].name = ss_str_dup(name);
	p->globals[p->globals_len].value = v;
	p->globals_len++;
}

const SS_value *SS_program_get_global(const SS_program *p, const char *name) {
	for (size_t i = 0; i < p->globals_len; i++) {
		if (strcmp(p->globals[i].name, name) == 0)
			return &p->globals[i].value;
	}
	return NULL;
}

int SS_program_run(SS_program *p, const char *name, SS_call_fn fn,
	void *userdata) {
	SS_context *ctx = SS_context_create(p, name);
	if (!ctx)
		return -1;
	SS_status s;
	while ((s = SS_context_step(ctx)) == SS_STATUS_CALL) {
		SS_value result = SS_nil_value();
		int rc = fn(SS_context_get_call(ctx), &result, userdata);
		if (rc != 0) {
			SS_value_free(&result);
			SS_context_free(ctx);
			return rc;
		}
		SS_context_set_result(ctx, result);
	}
	SS_context_free(ctx);
	return s == SS_STATUS_DONE ? 0 : -1;
}
