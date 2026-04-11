#include <ctype.h>

#include "internal.h"

// Token helpers

const char *SS_token_type_str(SS_token_type t) {
	switch (t) {
	case SS_TOK_INVALID:
		return "Invalid";
	case SS_TOK_SCRIPT:
		return "Script";
	case SS_TOK_NUMBER:
		return "Number";
	case SS_TOK_STRING:
		return "String";
	case SS_TOK_WORD:
		return "Word";
	case SS_TOK_MINUS:
		return "Minus";
	case SS_TOK_NOT:
		return "Not";
	case SS_TOK_AND:
		return "And";
	case SS_TOK_OR:
		return "Or";
	case SS_TOK_IS:
		return "Is";
	case SS_TOK_GT:
		return "Gt";
	case SS_TOK_GTE:
		return "Gte";
	case SS_TOK_LT:
		return "Lt";
	case SS_TOK_LTE:
		return "Lte";
	case SS_TOK_COMMA:
		return "Comma";
	case SS_TOK_COLON:
		return "Colon";
	case SS_TOK_ASSIGN:
		return "Assign";
	case SS_TOK_LPAREN:
		return "LParen";
	case SS_TOK_RPAREN:
		return "RParen";
	case SS_TOK_INDENT:
		return "Indent";
	}
	return "?";
}

// Lexer

static bool is_word_char(char c) {
	return c == '.' || isalpha((unsigned char)c) || isdigit((unsigned char)c);
}

static SS_token_type char_to_token(char c) {
	switch (c) {
	case '!':
		return SS_TOK_NOT;
	case '-':
		return SS_TOK_MINUS;
	case ',':
		return SS_TOK_COMMA;
	case ':':
		return SS_TOK_COLON;
	case '=':
		return SS_TOK_ASSIGN;
	case '(':
		return SS_TOK_LPAREN;
	case ')':
		return SS_TOK_RPAREN;
	case '\t':
		return SS_TOK_INDENT;
	}
	return SS_TOK_INVALID;
}

static SS_token token_from_string(const char *s, size_t len) {
	SS_token t = {.type = SS_TOK_INVALID, .literal = ss_str_ndup(s, len)};
	if (len == 3 && memcmp(s, "and", 3) == 0) {
		t.type = SS_TOK_AND;
	} else if (len == 2 && memcmp(s, "or", 2) == 0) {
		t.type = SS_TOK_OR;
	} else if (len == 2 && memcmp(s, "is", 2) == 0) {
		t.type = SS_TOK_IS;
	} else if (len == 2 && memcmp(s, "gt", 2) == 0) {
		t.type = SS_TOK_GT;
	} else if (len == 3 && memcmp(s, "gte", 3) == 0) {
		t.type = SS_TOK_GTE;
	} else if (len == 2 && memcmp(s, "lt", 2) == 0) {
		t.type = SS_TOK_LT;
	} else if (len == 3 && memcmp(s, "lte", 3) == 0) {
		t.type = SS_TOK_LTE;
	} else {
		char *end;
		strtod(t.literal, &end);
		if (end != t.literal && *end == '\0') {
			t.type = SS_TOK_NUMBER;
		} else {
			bool valid = true;
			for (size_t i = 0; i < len; i++) {
				if (!is_word_char(s[i])) {
					valid = false;
					break;
				}
			}
			if (valid)
				t.type = SS_TOK_WORD;
		}
	}
	return t;
}

static void push_token(SS_token **arr, size_t *len, size_t *cap, SS_token t) {
	if (*len == *cap) {
		*cap = *cap ? *cap * 2 : 16;
		*arr = realloc(*arr, *cap * sizeof(SS_token));
	}
	(*arr)[(*len)++] = t;
}

int SS_lex(const char *src, SS_token **out, size_t *out_len) {
	size_t cap = 0;
	*out = NULL;
	*out_len = 0;

	bool in_string = false, in_comment = false;
	const char *word_start = NULL;
	size_t word_len = 0;
	char last = 0;

	for (const char *p = src; *p; p++) {
		char c = *p;
		if (in_comment) {
			if (c == '\n')
				in_comment = false;
			last = c;
			continue;
		}
		if (!in_string && c == '#') {
			in_comment = true;
			// Remove indent tokens emitted earlier on this comment line
			while (*out_len > 0 && (*out)[*out_len - 1].type == SS_TOK_INDENT) {
				free((*out)[*out_len - 1].literal);
				(*out_len)--;
			}
			last = c;
			continue;
		}

		if (in_string || word_len > 0) {
			bool clear = false, skip = false;
			if (in_string) {
				if (c == '`' && last != '\\') {
					in_string = false;
					SS_token t = {.type = SS_TOK_STRING,
						.literal = ss_str_ndup(word_start, word_len)};
					push_token(out, out_len, &cap, t);
					clear = true;
					skip = true;
				}
			} else if (!is_word_char(c)) {
				push_token(out, out_len, &cap, token_from_string(word_start, word_len));
				clear = true;
			}
			if (clear) {
				word_start = NULL;
				word_len = 0;
			}
			if (skip) {
				last = c;
				continue;
			}
		}

		SS_token_type tt = char_to_token(c);
		if (!in_string && tt != SS_TOK_INVALID) {
			char buf[2] = {c, '\0'};
			SS_token t = {.type = tt, .literal = ss_str_dup(buf)};
			push_token(out, out_len, &cap, t);
		} else if (in_string || is_word_char(c)) {
			if (word_len == 0)
				word_start = p;
			word_len++;
		} else if (c == '`') {
			in_string = true;
			word_start = p + 1;
			word_len = 0;
		}
		last = c;
	}
	if (word_len > 0) {
		push_token(out, out_len, &cap, token_from_string(word_start, word_len));
	}
	return 0;
}

void SS_tokens_free(SS_token *tokens, size_t len) {
	for (size_t i = 0; i < len; i++)
		free(tokens[i].literal);
	free(tokens);
}
