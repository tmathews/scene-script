#include <stdio.h>

#include "internal.h"

// AST helpers

SS_keyword SS_literal_to_keyword(const char *s) {
	if (strcmp(s, "script") == 0)
		return SS_KEY_SCRIPT;
	if (strcmp(s, "if") == 0)
		return SS_KEY_IF;
	if (strcmp(s, "elif") == 0)
		return SS_KEY_ELIF;
	if (strcmp(s, "else") == 0)
		return SS_KEY_ELSE;
	if (strcmp(s, "end") == 0)
		return SS_KEY_END;
	if (strcmp(s, "run") == 0)
		return SS_KEY_RUN;
	if (strcmp(s, "eq") == 0)
		return SS_KEY_EQ;
	if (strcmp(s, "true") == 0)
		return SS_KEY_TRUE;
	if (strcmp(s, "false") == 0)
		return SS_KEY_FALSE;
	if (strcmp(s, "switch") == 0)
		return SS_KEY_SWITCH;
	if (strcmp(s, "case") == 0)
		return SS_KEY_CASE;
	if (strcmp(s, "constants") == 0)
		return SS_KEY_CONSTANTS;
	return SS_KEY_INVALID;
}

static bool is_prefix_token(const SS_token *t) {
	return t->type == SS_TOK_NOT || t->type == SS_TOK_MINUS;
}

static bool is_infix_token(const SS_token *t) {
	return t->type == SS_TOK_LPAREN || t->type == SS_TOK_MINUS ||
	       t->type == SS_TOK_AND || t->type == SS_TOK_OR ||
	       t->type == SS_TOK_IS || t->type == SS_TOK_GT ||
	       t->type == SS_TOK_GTE || t->type == SS_TOK_LT ||
	       t->type == SS_TOK_LTE;
}

enum {
	SS_PREC_LOWEST,
	SS_PREC_INFIX,
	SS_PREC_COMPARE,
	SS_PREC_SUM,
	SS_PREC_PREFIX,
	SS_PREC_CALL,
};

static unsigned get_prec(const SS_token *t) {
	switch (t->type) {
	case SS_TOK_AND:
	case SS_TOK_OR:
		return SS_PREC_INFIX;
	case SS_TOK_IS:
	case SS_TOK_GT:
	case SS_TOK_GTE:
	case SS_TOK_LT:
	case SS_TOK_LTE:
		return SS_PREC_COMPARE;
	case SS_TOK_MINUS:
		return SS_PREC_SUM;
	case SS_TOK_LPAREN:
		return SS_PREC_CALL;
	default:
		return SS_PREC_LOWEST;
	}
}

static bool is_exp_token(const SS_token *t) {
	SS_keyword kw = SS_literal_to_keyword(t->literal);
	if (kw != SS_KEY_INVALID) {
		return kw == SS_KEY_TRUE || kw == SS_KEY_FALSE;
	}
	switch (t->type) {
	case SS_TOK_LPAREN:
	case SS_TOK_WORD:
	case SS_TOK_STRING:
	case SS_TOK_NUMBER:
		return true;
	default:
		break;
	}
	return is_infix_token(t) || is_prefix_token(t);
}

// Expression alloc helpers

static SS_expression *exp_new(void) {
	SS_expression *e = calloc(1, sizeof(SS_expression));
	return e;
}

static void exp_add_child(SS_expression *e, SS_expression *child) {
	e->children =
		realloc(e->children, (e->children_len + 1) * sizeof(SS_expression *));
	e->children[e->children_len++] = child;
}

static void exp_free(SS_expression *e) {
	if (!e)
		return;
	free(e->value);
	for (size_t i = 0; i < e->children_len; i++)
		exp_free(e->children[i]);
	free(e->children);
	free(e);
}

// Statement / Block alloc helpers

static void block_push_stmt(SS_block *b, SS_statement s) {
	b->stmts = realloc(b->stmts, (b->stmts_len + 1) * sizeof(SS_statement));
	b->stmts[b->stmts_len++] = s;
}

static void stmt_add_alt(SS_statement *s, SS_statement *alt) {
	s->alternatives =
		realloc(s->alternatives, (s->alts_len + 1) * sizeof(SS_statement *));
	s->alternatives[s->alts_len++] = alt;
}

static void stmt_free(SS_statement *s);

static void block_free_contents(SS_block *b) {
	for (size_t i = 0; i < b->stmts_len; i++)
		stmt_free(&b->stmts[i]);
	free(b->stmts);
	b->stmts = NULL;
	b->stmts_len = 0;
}

static void stmt_free(SS_statement *s) {
	exp_free(s->expression);
	s->expression = NULL;
	if (s->block) {
		block_free_contents(s->block);
		free(s->block);
		s->block = NULL;
	}
	for (size_t i = 0; i < s->alts_len; i++) {
		stmt_free(s->alternatives[i]);
		free(s->alternatives[i]);
	}
	free(s->alternatives);
	s->alternatives = NULL;
	s->alts_len = 0;
}

// Parser (recursive descent + Pratt)

static int parse_expression(const SS_token *tokens, size_t len, size_t *i,
	unsigned prec, SS_expression **out);

static int parse_literal_exp(const SS_token *tokens, size_t len, size_t *i,
	SS_expression **out) {
	(void)len;
	const SS_token *t = &tokens[*i];
	if (t->type == SS_TOK_COLON || t->type == SS_TOK_INDENT)
		return -1;

	SS_keyword kw = SS_literal_to_keyword(t->literal);
	SS_expression *e = exp_new();
	if (kw == SS_KEY_TRUE || kw == SS_KEY_FALSE) {
		e->type = SS_EXP_BOOL;
	} else if (kw != SS_KEY_INVALID) {
		free(e);
		return -1;
	} else {
		switch (t->type) {
		case SS_TOK_WORD:
			e->type = SS_EXP_WORD;
			break;
		case SS_TOK_STRING:
			e->type = SS_EXP_STRING;
			break;
		case SS_TOK_NUMBER:
			e->type = SS_EXP_NUMBER;
			break;
		default:
			free(e);
			return -1;
		}
	}
	e->value = ss_str_dup(t->literal);
	*out = e;
	return 0;
}

static int parse_prefix_exp(const SS_token *tokens, size_t len, size_t *i,
	SS_expression **out) {
	SS_token_type op = tokens[*i].type;
	(*i)++;
	SS_expression *right = NULL;
	if (parse_expression(tokens, len, i, SS_PREC_PREFIX, &right) != 0)
		return -1;
	(*i)--;
	SS_expression *e = exp_new();
	e->type = SS_EXP_PREFIX;
	e->op = op;
	exp_add_child(e, right);
	*out = e;
	return 0;
}

static int parse_call_exp(const SS_token *tokens, size_t len, size_t *i,
	SS_expression *lit, SS_expression **out) {
	SS_expression *e = exp_new();
	e->type = SS_EXP_CALL;
	exp_add_child(e, lit);
	(*i)++; // past (
	while (*i < len && tokens[*i].type != SS_TOK_RPAREN) {
		if (tokens[*i].type == SS_TOK_COMMA) {
			(*i)++;
			continue;
		}
		SS_expression *arg = NULL;
		if (parse_expression(tokens, len, i, SS_PREC_LOWEST, &arg) != 0) {
			exp_free(e);
			return -1;
		}
		exp_add_child(e, arg);
	}
	(*i)++; // past )
	*out = e;
	return 0;
}

static int parse_infix_exp(const SS_token *tokens, size_t len, size_t *i,
	SS_expression *left, SS_expression **out) {
	if (left->type == SS_EXP_WORD && tokens[*i].type == SS_TOK_LPAREN)
		return parse_call_exp(tokens, len, i, left, out);

	unsigned prec = get_prec(&tokens[*i]);
	SS_token_type op = tokens[*i].type;
	(*i)++;
	SS_expression *right = NULL;
	if (parse_expression(tokens, len, i, prec, &right) != 0)
		return -1;
	SS_expression *e = exp_new();
	e->type = SS_EXP_INFIX;
	e->op = op;
	exp_add_child(e, right);
	exp_add_child(e, left);
	*out = e;
	return 0;
}

static int parse_expression(const SS_token *tokens, size_t len, size_t *i,
	unsigned prec, SS_expression **out) {
	if (*i >= len)
		return -1;
	const SS_token *t = &tokens[*i];
	SS_expression *left = NULL;
	int rc;
	if (is_prefix_token(t)) {
		rc = parse_prefix_exp(tokens, len, i, &left);
	} else {
		rc = parse_literal_exp(tokens, len, i, &left);
	}
	if (rc != 0)
		return rc;

	(*i)++;
	while (*i < len) {
		const SS_token *peek = &tokens[*i];
		if (!is_exp_token(peek) || prec >= get_prec(peek) || !is_infix_token(peek))
			break;
		SS_expression *new_left = NULL;
		if (parse_infix_exp(tokens, len, i, left, &new_left) != 0) {
			exp_free(left);
			return -1;
		}
		left = new_left;
	}
	*out = left;
	return 0;
}

// forward declarations
static int parse_block(const SS_token *tokens, size_t len, size_t *i,
	unsigned indent, SS_block *out);
static int parse_statement(const SS_token *tokens, size_t len, size_t *i,
	unsigned indent, SS_statement *out);

static int parse_conditional(const SS_token *tokens, size_t len, size_t *i,
	unsigned indent, SS_statement *out) {
	out->type = SS_STMT_IF;
	(*i)++; // past if
	if (parse_expression(tokens, len, i, SS_PREC_LOWEST, &out->expression) != 0)
		return -1;
	if (*i >= len || tokens[*i].type != SS_TOK_COLON)
		return -1;
	out->block = calloc(1, sizeof(SS_block));
	if (parse_block(tokens, len, i, indent, out->block) != 0)
		return -1;

	while (*i + indent < len && tokens[*i + indent].type == SS_TOK_WORD) {
		*i += indent;
		SS_keyword kw = SS_literal_to_keyword(tokens[*i].literal);
		if (kw == SS_KEY_ELIF) {
			(*i)++;
			SS_statement *alt = calloc(1, sizeof(SS_statement));
			alt->type = SS_STMT_ELIF;
			if (parse_expression(tokens, len, i, SS_PREC_LOWEST, &alt->expression) !=
				0) {
				free(alt);
				return -1;
			}
			alt->block = calloc(1, sizeof(SS_block));
			if (parse_block(tokens, len, i, indent, alt->block) != 0) {
				stmt_free(alt);
				free(alt);
				return -1;
			}
			stmt_add_alt(out, alt);
		} else if (kw == SS_KEY_ELSE) {
			(*i)++;
			SS_statement *alt = calloc(1, sizeof(SS_statement));
			alt->type = SS_STMT_ELSE;
			alt->block = calloc(1, sizeof(SS_block));
			if (parse_block(tokens, len, i, indent, alt->block) != 0) {
				stmt_free(alt);
				free(alt);
				return -1;
			}
			stmt_add_alt(out, alt);
			break;
		} else {
			*i -= indent;
			break;
		}
	}
	return 0;
}

static int parse_switch(const SS_token *tokens, size_t len, size_t *i,
	unsigned indent, SS_statement *out) {
	out->type = SS_STMT_SWITCH;
	(*i)++; // past 'switch'
	if (parse_expression(tokens, len, i, SS_PREC_LOWEST, &out->expression) != 0)
		return -1;
	if (*i >= len || tokens[*i].type != SS_TOK_COLON)
		return -1;
	out->block = calloc(1, sizeof(SS_block));
	return parse_block(tokens, len, i, indent, out->block);
}

static int parse_statement(const SS_token *tokens, size_t len, size_t *i,
	unsigned indent, SS_statement *out) {
	memset(out, 0, sizeof(*out));
	(*i)++; // past current indent
	if (*i >= len)
		return -1;

	const SS_token *t = &tokens[*i];
	if (t->type == SS_TOK_INDENT)
		return -1;

	SS_keyword kw = SS_literal_to_keyword(t->literal);
	int rc = 0;
	switch (kw) {
	case SS_KEY_IF:
		rc = parse_conditional(tokens, len, i, indent, out);
		break;
	case SS_KEY_SWITCH:
		rc = parse_switch(tokens, len, i, indent, out);
		break;
	case SS_KEY_CASE: {
		out->type = SS_STMT_CASE;
		(*i)++; // past 'case'
		// Parse comma-separated match values into an EXP_LIST
		SS_expression *list = calloc(1, sizeof(SS_expression));
		list->type = SS_EXP_LIST;
		while (1) {
			SS_expression *val = NULL;
			if (parse_expression(tokens, len, i, SS_PREC_LOWEST, &val) != 0) {
				exp_free(list);
				free(list);
				return -1;
			}
			exp_add_child(list, val);
			if (*i < len && tokens[*i].type == SS_TOK_COMMA) {
				(*i)++; // past comma
			} else {
				break;
			}
		}
		out->expression = list;
		if (*i >= len || tokens[*i].type != SS_TOK_COLON) {
			return -1;
		}
		out->block = calloc(1, sizeof(SS_block));
		rc = parse_block(tokens, len, i, indent, out->block);
		break;
	}
	case SS_KEY_RUN:
		out->type = SS_STMT_RUN;
		(*i)++;
		rc = parse_expression(tokens, len, i, SS_PREC_LOWEST, &out->expression);
		break;
	case SS_KEY_END:
		out->type = SS_STMT_END;
		(*i)++;
		break;
	default:
		out->type = SS_STMT_EXPRESSION;
		rc = parse_expression(tokens, len, i, SS_PREC_LOWEST, &out->expression);
		break;
	}
	if (rc != 0)
		return rc;
	if (out->type == SS_STMT_INVALID)
		return -1;
	return 0;
}

static int parse_block(const SS_token *tokens, size_t len, size_t *i,
	unsigned indent, SS_block *out) {
	unsigned count = 0;
	(*i)++; // past :
	while (*i < len) {
		if (tokens[*i].type != SS_TOK_INDENT)
			break;
		count++;
		if (count < indent + 1) {
			(*i)++;
			continue;
		}
		SS_statement stmt;
		if (parse_statement(tokens, len, i, indent + 1, &stmt) != 0)
			return -1;
		block_push_stmt(out, stmt);
		count = 0;
	}
	*i -= count;
	return 0;
}

static int parse_script(const SS_token *tokens, size_t len, size_t *i,
	SS_script *out) {
	memset(out, 0, sizeof(*out));
	if (*i + 2 >= len)
		return -1;
	const SS_token *word = &tokens[*i + 1];
	const SS_token *col = &tokens[*i + 2];
	if (word->type != SS_TOK_WORD)
		return -1;
	if (col->type != SS_TOK_COLON)
		return -1;
	out->name = ss_str_dup(word->literal);
	*i += 2; // past name to :
	return parse_block(tokens, len, i, 0, &out->block);
}

static SS_value token_to_value(const SS_token *t) {
	switch (t->type) {
	case SS_TOK_STRING:
		return SS_string_value(t->literal);
	case SS_TOK_NUMBER: {
		char *end;
		double d = strtod(t->literal, &end);
		(void)end;
		return SS_number_value(d);
	}
	case SS_TOK_WORD:
		if (strcmp(t->literal, "true") == 0)
			return SS_bool_value(true);
		if (strcmp(t->literal, "false") == 0)
			return SS_bool_value(false);
		// fall through
	default:
		return SS_nil_value();
	}
}

static int parse_constants(const SS_token *tokens, size_t len, size_t *i,
	SS_constant **out, size_t *out_len) {
	size_t cap = *out_len;
	(*i)++; // past 'constants'
	if (*i >= len || tokens[*i].type != SS_TOK_COLON)
		return -1;
	(*i)++; // past ':'
	while (*i < len) {
		if (tokens[*i].type != SS_TOK_INDENT)
			break;
		// Count indent depth — must be exactly 1
		unsigned depth = 0;
		while (*i < len && tokens[*i].type == SS_TOK_INDENT) {
			depth++;
			(*i)++;
		}
		if (depth != 1) {
			*i -= depth;
			break;
		}
		// Expect: Name = value
		if (*i + 2 >= len)
			return -1;
		if (tokens[*i].type != SS_TOK_WORD)
			return -1;
		const char *name = tokens[*i].literal;
		(*i)++;
		if (tokens[*i].type != SS_TOK_ASSIGN)
			return -1;
		(*i)++;
		SS_value val = token_to_value(&tokens[*i]);
		(*i)++;

		if (*out_len == cap) {
			cap = cap ? cap * 2 : 8;
			*out = realloc(*out, cap * sizeof(SS_constant));
		}
		SS_constant c;
		c.name = ss_str_dup(name);
		c.value = val;
		(*out)[(*out_len)++] = c;
	}
	return 0;
}

int SS_parse(const SS_token *tokens, size_t tokens_len, SS_script **out,
	size_t *out_len, SS_constant **constants_out, size_t *constants_len) {
	size_t cap = 0;
	*out = NULL;
	*out_len = 0;
	*constants_out = NULL;
	*constants_len = 0;
	size_t i = 0;
	while (i < tokens_len) {
		const SS_token *t = &tokens[i];
		if (t->type != SS_TOK_WORD)
			return -1;
		SS_keyword kw = SS_literal_to_keyword(t->literal);
		if (kw == SS_KEY_CONSTANTS) {
			if (parse_constants(tokens, tokens_len, &i,
					constants_out, constants_len) != 0)
				return -1;
			continue;
		}
		if (kw != SS_KEY_SCRIPT)
			return -1;
		SS_script s;
		if (parse_script(tokens, tokens_len, &i, &s) != 0)
			return -1;
		if (*out_len == cap) {
			cap = cap ? cap * 2 : 4;
			*out = realloc(*out, cap * sizeof(SS_script));
		}
		(*out)[(*out_len)++] = s;
	}
	return 0;
}

void SS_constants_free(SS_constant *constants, size_t len) {
	for (size_t i = 0; i < len; i++) {
		free(constants[i].name);
		SS_value_free(&constants[i].value);
	}
	free(constants);
}

void SS_scripts_free(SS_script *scripts, size_t len) {
	for (size_t i = 0; i < len; i++) {
		free(scripts[i].name);
		block_free_contents(&scripts[i].block);
	}
	free(scripts);
}

// Print / Debug

int SS_sprint_expression(const SS_expression *e, char *buf, size_t len) {
	if (!e)
		return snprintf(buf, len, "(null)");

	bool literal = (e->type == SS_EXP_BOOL || e->type == SS_EXP_NUMBER ||
					e->type == SS_EXP_STRING || e->type == SS_EXP_WORD);
	if (literal)
		return snprintf(buf, len, "(%s)", e->value);

	switch (e->type) {
	case SS_EXP_CALL: {
		int n = snprintf(buf, len, "(%s(", e->children[0]->value);
		for (size_t i = 1; i < e->children_len; i++) {
			n += SS_sprint_expression(e->children[i], buf ? buf + n : NULL,
				len > (size_t)n ? len - n : 0);
			n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, ", ");
		}
		n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, "))");
		return n;
	}
	case SS_EXP_INFIX: {
		// children[1] = left, children[0] = right
		int n = snprintf(buf, len, "(");
		n += SS_sprint_expression(e->children[1], buf ? buf + n : NULL,
			len > (size_t)n ? len - n : 0);
		n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, " %s ",
			SS_token_type_str(e->op));
		n += SS_sprint_expression(e->children[0], buf ? buf + n : NULL,
			len > (size_t)n ? len - n : 0);
		n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, ")");
		return n;
	}
	case SS_EXP_PREFIX: {
		int n = snprintf(buf, len, "(%s ", SS_token_type_str(e->op));
		n += SS_sprint_expression(e->children[0], buf ? buf + n : NULL,
			len > (size_t)n ? len - n : 0);
		n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, ")");
		return n;
	}
	case SS_EXP_LIST: {
		int n = snprintf(buf, len, "[");
		for (size_t i = 0; i < e->children_len; i++) {
			if (i > 0)
				n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, ", ");
			n += SS_sprint_expression(e->children[i], buf ? buf + n : NULL,
				len > (size_t)n ? len - n : 0);
		}
		n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, "]");
		return n;
	}
	default:
		return snprintf(buf, len, "Unknown Expression(%d:%s)", e->type,
			e->value ? e->value : "");
	}
}

static void print_block(const SS_block *b, unsigned indent);

static void print_statement(const SS_statement *s, unsigned indent) {
	char expbuf[512];
	switch (s->type) {
	case SS_STMT_END:
		printf("end\n");
		break;
	case SS_STMT_RUN:
		printf("run ");
		SS_sprint_expression(s->expression, expbuf, sizeof(expbuf));
		printf("%s\n", expbuf);
		break;
	case SS_STMT_IF:
		SS_sprint_expression(s->expression, expbuf, sizeof(expbuf));
		printf("if %s:\n", expbuf);
		print_block(s->block, indent);
		for (size_t i = 0; i < s->alts_len; i++) {
			for (unsigned j = 0; j < indent - 1; j++)
				printf("\t");
			print_statement(s->alternatives[i], indent);
		}
		break;
	case SS_STMT_ELIF:
		SS_sprint_expression(s->expression, expbuf, sizeof(expbuf));
		printf("elif %s:\n", expbuf);
		print_block(s->block, indent);
		break;
	case SS_STMT_ELSE:
		printf("else:\n");
		print_block(s->block, indent);
		break;
	case SS_STMT_EXPRESSION:
		SS_sprint_expression(s->expression, expbuf, sizeof(expbuf));
		printf("%s\n", expbuf);
		break;
	case SS_STMT_SWITCH:
		SS_sprint_expression(s->expression, expbuf, sizeof(expbuf));
		printf("switch %s:\n", expbuf);
		print_block(s->block, indent);
		break;
	case SS_STMT_CASE:
		SS_sprint_expression(s->expression, expbuf, sizeof(expbuf));
		printf("case %s:\n", expbuf);
		print_block(s->block, indent);
		break;
	case SS_STMT_INVALID:
		printf("Invalid Statement\n");
		break;
	}
}

static void print_block(const SS_block *b, unsigned indent) {
	for (size_t i = 0; i < b->stmts_len; i++) {
		for (unsigned j = 0; j < indent; j++)
			printf("\t");
		print_statement(&b->stmts[i], indent + 1);
	}
}

void SS_print_script(const SS_script *s) {
	printf("script %s:\n", s->name);
	print_block(&s->block, 1);
}
