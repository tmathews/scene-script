#include "npc_script.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ────────────────────────────────────────────────────────── */

static char *str_dup(const char *s) {
    size_t len = strlen(s);
    char *d = malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

static char *str_ndup(const char *s, size_t n) {
    char *d = malloc(n + 1);
    if (d) { memcpy(d, s, n); d[n] = '\0'; }
    return d;
}

/* ── Value ──────────────────────────────────────────────────────────── */

SS_value SS_nil_value(void) {
    return (SS_value){ .type = SS_VAL_NIL };
}

SS_value SS_bool_value(bool b) {
    return (SS_value){ .type = SS_VAL_BOOL, .boolean = b };
}

SS_value SS_number_value(double n) {
    return (SS_value){ .type = SS_VAL_NUMBER, .number = n };
}

SS_value SS_string_value(const char *s) {
    return (SS_value){ .type = SS_VAL_STRING, .string = str_dup(s) };
}

void SS_value_free(SS_value *v) {
    if (v->type == SS_VAL_STRING) { free(v->string); v->string = NULL; }
    v->type = SS_VAL_NIL;
}

bool SS_value_is_boolean(const SS_value *v) {
    return v->type == SS_VAL_NIL || v->type == SS_VAL_BOOL || v->type == SS_VAL_NUMBER;
}

bool SS_value_bool(const SS_value *v) {
    switch (v->type) {
    case SS_VAL_NIL:    return false;
    case SS_VAL_BOOL:   return v->boolean;
    case SS_VAL_NUMBER: return v->number != 0.0;
    default:         return false;
    }
}

double SS_value_number(const SS_value *v) { return v->number; }
const char *SS_value_string(const SS_value *v) { return v->string; }

int SS_value_sprint(const SS_value *v, char *buf, size_t len) {
    switch (v->type) {
    case SS_VAL_NIL:    return snprintf(buf, len, "(nil)");
    case SS_VAL_BOOL:   return snprintf(buf, len, "%s", v->boolean ? "TRUE" : "FALSE");
    case SS_VAL_NUMBER: return snprintf(buf, len, "%.05f", v->number);
    case SS_VAL_STRING: return snprintf(buf, len, "%s", v->string);
    }
    return snprintf(buf, len, "Invalid Type");
}

/* ── Token helpers ──────────────────────────────────────────────────── */

const char *SS_token_type_str(SS_token_type t) {
    switch (t) {
    case SS_TOK_INVALID: return "Invalid";
    case SS_TOK_SCRIPT:  return "Script";
    case SS_TOK_NUMBER:  return "Number";
    case SS_TOK_STRING:  return "String";
    case SS_TOK_WORD:    return "Word";
    case SS_TOK_MINUS:   return "Minus";
    case SS_TOK_NOT:     return "Not";
    case SS_TOK_AND:     return "And";
    case SS_TOK_OR:      return "Or";
    case SS_TOK_COMMA:   return "Comma";
    case SS_TOK_COLON:   return "Colon";
    case SS_TOK_LPAREN:  return "LParen";
    case SS_TOK_RPAREN:  return "RParen";
    case SS_TOK_INDENT:  return "Indent";
    }
    return "?";
}

/* ── Lexer ──────────────────────────────────────────────────────────── */

static bool is_word_char(char c) {
    return c == '.' || isalpha((unsigned char)c) || isdigit((unsigned char)c);
}

static SS_token_type char_to_token(char c) {
    switch (c) {
    case '!':  return SS_TOK_NOT;
    case '-':  return SS_TOK_MINUS;
    case ',':  return SS_TOK_COMMA;
    case ':':  return SS_TOK_COLON;
    case '(':  return SS_TOK_LPAREN;
    case ')':  return SS_TOK_RPAREN;
    case '\t': return SS_TOK_INDENT;
    }
    return SS_TOK_INVALID;
}

static SS_token token_from_string(const char *s, size_t len) {
    SS_token t = { .type = SS_TOK_INVALID, .literal = str_ndup(s, len) };
    if (len == 3 && memcmp(s, "and", 3) == 0) {
        t.type = SS_TOK_AND;
    } else if (len == 2 && memcmp(s, "or", 2) == 0) {
        t.type = SS_TOK_OR;
    } else {
        char *end;
        strtod(t.literal, &end);
        if (end != t.literal && *end == '\0') {
            t.type = SS_TOK_NUMBER;
        } else {
            bool valid = true;
            for (size_t i = 0; i < len; i++) {
                if (!is_word_char(s[i])) { valid = false; break; }
            }
            if (valid) t.type = SS_TOK_WORD;
        }
    }
    return t;
}

/* Append a token to a dynamic array. */
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
            if (c == '\n') in_comment = false;
            last = c;
            continue;
        }
        if (!in_string && c == '#') {
            in_comment = true;
            last = c;
            continue;
        }

        if (in_string || word_len > 0) {
            bool clear = false, skip = false;
            if (in_string) {
                if (c == '`' && last != '\\') {
                    in_string = false;
                    SS_token t = { .type = SS_TOK_STRING, .literal = str_ndup(word_start, word_len) };
                    push_token(out, out_len, &cap, t);
                    clear = true;
                    skip = true;
                }
            } else if (!is_word_char(c)) {
                push_token(out, out_len, &cap, token_from_string(word_start, word_len));
                clear = true;
            }
            if (clear) { word_start = NULL; word_len = 0; }
            if (skip) { last = c; continue; }
        }

        SS_token_type tt = char_to_token(c);
        if (!in_string && tt != SS_TOK_INVALID) {
            char buf[2] = { c, '\0' };
            SS_token t = { .type = tt, .literal = str_dup(buf) };
            push_token(out, out_len, &cap, t);
        } else if (in_string || is_word_char(c)) {
            if (word_len == 0) word_start = p;
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
    for (size_t i = 0; i < len; i++) free(tokens[i].literal);
    free(tokens);
}

/* ── AST helpers ────────────────────────────────────────────────────── */

SS_keyword SS_literal_to_keyword(const char *s) {
    if (strcmp(s, "script") == 0) return SS_KEY_SCRIPT;
    if (strcmp(s, "if") == 0)     return SS_KEY_IF;
    if (strcmp(s, "elif") == 0)   return SS_KEY_ELIF;
    if (strcmp(s, "else") == 0)   return SS_KEY_ELSE;
    if (strcmp(s, "end") == 0)    return SS_KEY_END;
    if (strcmp(s, "run") == 0)    return SS_KEY_RUN;
    if (strcmp(s, "eq") == 0)     return SS_KEY_EQ;
    if (strcmp(s, "true") == 0)   return SS_KEY_TRUE;
    if (strcmp(s, "false") == 0)  return SS_KEY_FALSE;
    return SS_KEY_INVALID;
}

static bool is_prefix_token(const SS_token *t) {
    return t->type == SS_TOK_NOT || t->type == SS_TOK_MINUS;
}

static bool is_infix_token(const SS_token *t) {
    return t->type == SS_TOK_LPAREN || t->type == SS_TOK_MINUS ||
           t->type == SS_TOK_AND    || t->type == SS_TOK_OR;
}

enum {
    SS_PREC_LOWEST,
    SS_PREC_INFIX,
    SS_PREC_SUM,
    SS_PREC_PREFIX,
    SS_PREC_CALL,
};

static unsigned get_prec(const SS_token *t) {
    switch (t->type) {
    case SS_TOK_AND: case SS_TOK_OR: return SS_PREC_INFIX;
    case SS_TOK_MINUS:            return SS_PREC_SUM;
    case SS_TOK_LPAREN:           return SS_PREC_CALL;
    default:                   return SS_PREC_LOWEST;
    }
}

static bool is_exp_token(const SS_token *t) {
    SS_keyword kw = SS_literal_to_keyword(t->literal);
    if (kw != SS_KEY_INVALID) {
        return kw == SS_KEY_TRUE || kw == SS_KEY_FALSE;
    }
    switch (t->type) {
    case SS_TOK_LPAREN: case SS_TOK_WORD: case SS_TOK_STRING: case SS_TOK_NUMBER:
        return true;
    default: break;
    }
    return is_infix_token(t) || is_prefix_token(t);
}

/* ── Expression alloc helpers ───────────────────────────────────────── */

static SS_expression *exp_new(void) {
    SS_expression *e = calloc(1, sizeof(SS_expression));
    return e;
}

static void exp_add_child(SS_expression *e, SS_expression *child) {
    e->children = realloc(e->children, (e->children_len + 1) * sizeof(SS_expression *));
    e->children[e->children_len++] = child;
}

static void exp_free(SS_expression *e) {
    if (!e) return;
    free(e->value);
    for (size_t i = 0; i < e->children_len; i++) exp_free(e->children[i]);
    free(e->children);
    free(e);
}

/* ── Statement / Block alloc helpers ────────────────────────────────── */

static void block_push_stmt(SS_block *b, SS_statement s) {
    b->stmts = realloc(b->stmts, (b->stmts_len + 1) * sizeof(SS_statement));
    b->stmts[b->stmts_len++] = s;
}

static void stmt_add_alt(SS_statement *s, SS_statement *alt) {
    s->alternatives = realloc(s->alternatives, (s->alts_len + 1) * sizeof(SS_statement *));
    s->alternatives[s->alts_len++] = alt;
}

static void stmt_free(SS_statement *s);

static void block_free_contents(SS_block *b) {
    for (size_t i = 0; i < b->stmts_len; i++) stmt_free(&b->stmts[i]);
    free(b->stmts);
    b->stmts = NULL;
    b->stmts_len = 0;
}

static void stmt_free(SS_statement *s) {
    exp_free(s->expression);
    s->expression = NULL;
    if (s->block) { block_free_contents(s->block); free(s->block); s->block = NULL; }
    for (size_t i = 0; i < s->alts_len; i++) {
        stmt_free(s->alternatives[i]);
        free(s->alternatives[i]);
    }
    free(s->alternatives);
    s->alternatives = NULL;
    s->alts_len = 0;
}

/* ── Parser (recursive descent + Pratt) ─────────────────────────────── */

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
        case SS_TOK_WORD:   e->type = SS_EXP_WORD;   break;
        case SS_TOK_STRING: e->type = SS_EXP_STRING;  break;
        case SS_TOK_NUMBER: e->type = SS_EXP_NUMBER;  break;
        default: free(e); return -1;
        }
    }
    e->value = str_dup(t->literal);
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
    (*i)++; /* past ( */
    while (*i < len && tokens[*i].type != SS_TOK_RPAREN) {
        if (tokens[*i].type == SS_TOK_COMMA) { (*i)++; continue; }
        SS_expression *arg = NULL;
        if (parse_expression(tokens, len, i, SS_PREC_LOWEST, &arg) != 0) {
            exp_free(e);
            return -1;
        }
        exp_add_child(e, arg);
    }
    (*i)++; /* past ) */
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
    if (*i >= len) return -1;
    const SS_token *t = &tokens[*i];
    SS_expression *left = NULL;
    int rc;
    if (is_prefix_token(t)) {
        rc = parse_prefix_exp(tokens, len, i, &left);
    } else {
        rc = parse_literal_exp(tokens, len, i, &left);
    }
    if (rc != 0) return rc;

    if (*i + 1 >= len) { (*i)++; *out = left; return 0; }

    const SS_token *peek = &tokens[*i + 1];
    (*i)++;
    while (is_exp_token(peek) && prec < get_prec(peek)) {
        if (!is_infix_token(peek)) break;
        SS_expression *new_left = NULL;
        if (parse_infix_exp(tokens, len, i, left, &new_left) != 0) {
            exp_free(left);
            return -1;
        }
        left = new_left;
        if (*i + 1 >= len) break;
        peek = &tokens[*i + 1];
    }
    *out = left;
    return 0;
}

/* forward declarations */
static int parse_block(const SS_token *tokens, size_t len, size_t *i,
                       unsigned indent, SS_block *out);
static int parse_statement(const SS_token *tokens, size_t len, size_t *i,
                           unsigned indent, SS_statement *out);

static int parse_conditional(const SS_token *tokens, size_t len, size_t *i,
                             unsigned indent, SS_statement *out) {
    out->type = SS_STMT_IF;
    (*i)++; /* past if */
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
            if (parse_expression(tokens, len, i, SS_PREC_LOWEST, &alt->expression) != 0) {
                free(alt); return -1;
            }
            alt->block = calloc(1, sizeof(SS_block));
            if (parse_block(tokens, len, i, indent, alt->block) != 0) {
                stmt_free(alt); free(alt); return -1;
            }
            stmt_add_alt(out, alt);
        } else if (kw == SS_KEY_ELSE) {
            (*i)++;
            SS_statement *alt = calloc(1, sizeof(SS_statement));
            alt->type = SS_STMT_ELSE;
            alt->block = calloc(1, sizeof(SS_block));
            if (parse_block(tokens, len, i, indent, alt->block) != 0) {
                stmt_free(alt); free(alt); return -1;
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

static int parse_statement(const SS_token *tokens, size_t len, size_t *i,
                           unsigned indent, SS_statement *out) {
    memset(out, 0, sizeof(*out));
    (*i)++; /* past current indent */
    if (*i >= len) return -1;

    const SS_token *t = &tokens[*i];
    if (t->type == SS_TOK_INDENT) return -1;

    SS_keyword kw = SS_literal_to_keyword(t->literal);
    int rc = 0;
    switch (kw) {
    case SS_KEY_IF:
        rc = parse_conditional(tokens, len, i, indent, out);
        break;
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
    if (rc != 0) return rc;
    if (out->type == SS_STMT_INVALID) return -1;
    return 0;
}

static int parse_block(const SS_token *tokens, size_t len, size_t *i,
                       unsigned indent, SS_block *out) {
    unsigned count = 0;
    (*i)++; /* past : */
    while (*i < len) {
        if (tokens[*i].type != SS_TOK_INDENT) break;
        count++;
        if (count < indent + 1) { (*i)++; continue; }
        SS_statement stmt;
        if (parse_statement(tokens, len, i, indent + 1, &stmt) != 0) return -1;
        block_push_stmt(out, stmt);
        count = 0;
    }
    *i -= count;
    return 0;
}

static int parse_script(const SS_token *tokens, size_t len, size_t *i,
                        SS_script *out) {
    memset(out, 0, sizeof(*out));
    if (*i + 2 >= len) return -1;
    const SS_token *word = &tokens[*i + 1];
    const SS_token *col  = &tokens[*i + 2];
    if (word->type != SS_TOK_WORD) return -1;
    if (col->type != SS_TOK_COLON) return -1;
    out->name = str_dup(word->literal);
    *i += 2; /* past name to : */
    return parse_block(tokens, len, i, 0, &out->block);
}

int SS_parse(const SS_token *tokens, size_t tokens_len,
              SS_script **out, size_t *out_len) {
    size_t cap = 0;
    *out = NULL;
    *out_len = 0;
    size_t i = 0;
    while (i < tokens_len) {
        const SS_token *t = &tokens[i];
        if (t->type != SS_TOK_WORD) return -1;
        if (SS_literal_to_keyword(t->literal) != SS_KEY_SCRIPT) return -1;
        SS_script s;
        if (parse_script(tokens, tokens_len, &i, &s) != 0) return -1;
        if (*out_len == cap) {
            cap = cap ? cap * 2 : 4;
            *out = realloc(*out, cap * sizeof(SS_script));
        }
        (*out)[(*out_len)++] = s;
    }
    return 0;
}

void SS_scripts_free(SS_script *scripts, size_t len) {
    for (size_t i = 0; i < len; i++) {
        free(scripts[i].name);
        block_free_contents(&scripts[i].block);
    }
    free(scripts);
}

/* ── Print / Debug ──────────────────────────────────────────────────── */

int SS_sprint_expression(const SS_expression *e, char *buf, size_t len) {
    if (!e) return snprintf(buf, len, "(null)");

    bool literal = (e->type == SS_EXP_BOOL || e->type == SS_EXP_NUMBER ||
                    e->type == SS_EXP_STRING || e->type == SS_EXP_WORD);
    if (literal) return snprintf(buf, len, "(%s)", e->value);

    switch (e->type) {
    case SS_EXP_CALL: {
        int n = snprintf(buf, len, "(%s(", e->children[0]->value);
        for (size_t i = 1; i < e->children_len; i++) {
            n += SS_sprint_expression(e->children[i],
                                       buf ? buf + n : NULL,
                                       len > (size_t)n ? len - n : 0);
            n += snprintf(buf ? buf + n : NULL,
                          len > (size_t)n ? len - n : 0, ", ");
        }
        n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, "))");
        return n;
    }
    case SS_EXP_INFIX: {
        /* children[1] = left, children[0] = right */
        int n = snprintf(buf, len, "(");
        n += SS_sprint_expression(e->children[1],
                                   buf ? buf + n : NULL,
                                   len > (size_t)n ? len - n : 0);
        n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0,
                      " %s ", SS_token_type_str(e->op));
        n += SS_sprint_expression(e->children[0],
                                   buf ? buf + n : NULL,
                                   len > (size_t)n ? len - n : 0);
        n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, ")");
        return n;
    }
    case SS_EXP_PREFIX: {
        int n = snprintf(buf, len, "(%s ", SS_token_type_str(e->op));
        n += SS_sprint_expression(e->children[0],
                                   buf ? buf + n : NULL,
                                   len > (size_t)n ? len - n : 0);
        n += snprintf(buf ? buf + n : NULL, len > (size_t)n ? len - n : 0, ")");
        return n;
    }
    default:
        return snprintf(buf, len, "Unknown Expression(%d:%s)", e->type, e->value ? e->value : "");
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
            for (unsigned j = 0; j < indent - 1; j++) printf("\t");
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
    case SS_STMT_INVALID:
        printf("Invalid Statement\n");
        break;
    }
}

static void print_block(const SS_block *b, unsigned indent) {
    for (size_t i = 0; i < b->stmts_len; i++) {
        for (unsigned j = 0; j < indent; j++) printf("\t");
        print_statement(&b->stmts[i], indent + 1);
    }
}

void SS_print_script(const SS_script *s) {
    printf("script %s:\n", s->name);
    print_block(&s->block, 1);
}

/* ── Evaluator ──────────────────────────────────────────────────────── */

typedef struct {
    SS_program *program;
    SS_call_fn  call_fn;
    void        *userdata;
} eval_ctx;

static int eval_expression(eval_ctx *ctx, const SS_expression *exp, SS_value *out);
static int eval_block(eval_ctx *ctx, const SS_block *block, bool *done);

static int eval_prefix(eval_ctx *ctx, const SS_expression *exp, SS_value *out) {
    SS_value right;
    if (eval_expression(ctx, exp->children[0], &right) != 0) return -1;
    switch (exp->op) {
    case SS_TOK_MINUS:
        if (right.type != SS_VAL_NUMBER) { SS_value_free(&right); return -1; }
        *out = SS_number_value(right.number * -1.0);
        SS_value_free(&right);
        return 0;
    case SS_TOK_NOT:
        if (!SS_value_is_boolean(&right)) { SS_value_free(&right); return -1; }
        *out = SS_bool_value(!SS_value_bool(&right));
        SS_value_free(&right);
        return 0;
    default:
        SS_value_free(&right);
        return -1;
    }
}

static int eval_infix(eval_ctx *ctx, const SS_expression *exp, SS_value *out) {
    /* children[1] = left, children[0] = right */
    SS_value left, right;
    if (eval_expression(ctx, exp->children[1], &left) != 0) return -1;
    if (!SS_value_is_boolean(&left)) { SS_value_free(&left); return -1; }
    if (eval_expression(ctx, exp->children[0], &right) != 0) {
        SS_value_free(&left); return -1;
    }
    if (!SS_value_is_boolean(&right)) {
        SS_value_free(&left); SS_value_free(&right); return -1;
    }
    bool lb = SS_value_bool(&left);
    bool rb = SS_value_bool(&right);
    SS_value_free(&left);
    SS_value_free(&right);
    switch (exp->op) {
    case SS_TOK_AND: *out = SS_bool_value(lb && rb); return 0;
    case SS_TOK_OR:  *out = SS_bool_value(lb || rb); return 0;
    default: return -1;
    }
}

static int eval_call(eval_ctx *ctx, const SS_expression *exp, SS_value *out) {
    size_t argc = exp->children_len - 1;
    SS_value *args = NULL;
    if (argc > 0) args = calloc(argc, sizeof(SS_value));
    for (size_t i = 0; i < argc; i++) {
        if (eval_expression(ctx, exp->children[i + 1], &args[i]) != 0) {
            for (size_t j = 0; j < i; j++) SS_value_free(&args[j]);
            free(args);
            return -1;
        }
    }
    SS_call call = {
        .name     = exp->children[0]->value,
        .args     = args,
        .args_len = argc,
    };
    SS_value result = SS_nil_value();
    int rc = ctx->call_fn(&call, &result, ctx->userdata);
    for (size_t i = 0; i < argc; i++) SS_value_free(&args[i]);
    free(args);
    if (rc != 0) return rc;
    *out = result;
    return 0;
}

static int eval_expression(eval_ctx *ctx, const SS_expression *exp, SS_value *out) {
    *out = SS_nil_value();
    switch (exp->type) {
    case SS_EXP_BOOL:
        *out = SS_bool_value(strcmp(exp->value, "true") == 0);
        return 0;
    case SS_EXP_NUMBER: {
        char *end;
        double d = strtod(exp->value, &end);
        *out = SS_number_value(d);
        return 0;
    }
    case SS_EXP_STRING:
        *out = SS_string_value(exp->value);
        return 0;
    case SS_EXP_WORD: {
        const SS_value *g = SS_program_get_global(ctx->program, exp->value);
        if (!g) return -1;
        /* copy the value; if it's a string we must duplicate */
        if (g->type == SS_VAL_STRING) {
            *out = SS_string_value(g->string);
        } else {
            *out = *g;
        }
        return 0;
    }
    case SS_EXP_CALL:
        return eval_call(ctx, exp, out);
    case SS_EXP_PREFIX:
        return eval_prefix(ctx, exp, out);
    case SS_EXP_INFIX:
        return eval_infix(ctx, exp, out);
    default:
        return -1;
    }
}

static int eval_conditional(eval_ctx *ctx, const SS_statement *st, bool *done) {
    SS_value res;
    if (eval_expression(ctx, st->expression, &res) != 0) return -1;
    if (!SS_value_is_boolean(&res)) { SS_value_free(&res); return -1; }
    if (SS_value_bool(&res)) {
        SS_value_free(&res);
        return eval_block(ctx, st->block, done);
    }
    SS_value_free(&res);
    for (size_t i = 0; i < st->alts_len; i++) {
        const SS_statement *alt = st->alternatives[i];
        if (alt->type == SS_STMT_ELSE) {
            return eval_block(ctx, alt->block, done);
        }
        SS_value ares;
        if (eval_expression(ctx, alt->expression, &ares) != 0) return -1;
        if (!SS_value_is_boolean(&ares)) { SS_value_free(&ares); return -1; }
        if (SS_value_bool(&ares)) {
            SS_value_free(&ares);
            return eval_block(ctx, alt->block, done);
        }
        SS_value_free(&ares);
    }
    *done = false;
    return 0;
}

static int eval_statement(eval_ctx *ctx, const SS_statement *st, bool *done);

static int eval_run(eval_ctx *ctx, const char *name) {
    const SS_script *script = NULL;
    for (size_t i = 0; i < ctx->program->scripts_len; i++) {
        if (strcmp(ctx->program->scripts[i].name, name) == 0) {
            script = &ctx->program->scripts[i];
            break;
        }
    }
    if (!script) return -1;
    bool d;
    return eval_block(ctx, &script->block, &d);
}

static int eval_statement(eval_ctx *ctx, const SS_statement *st, bool *done) {
    *done = false;
    switch (st->type) {
    case SS_STMT_EXPRESSION: {
        SS_value v;
        int rc = eval_expression(ctx, st->expression, &v);
        SS_value_free(&v);
        return rc;
    }
    case SS_STMT_END:
        *done = true;
        return 0;
    case SS_STMT_RUN:
        *done = true;
        return eval_run(ctx, st->expression->value);
    case SS_STMT_IF:
        return eval_conditional(ctx, st, done);
    default:
        return -1;
    }
}

static int eval_block(eval_ctx *ctx, const SS_block *block, bool *done) {
    *done = false;
    for (size_t i = 0; i < block->stmts_len; i++) {
        if (eval_statement(ctx, &block->stmts[i], done) != 0) return -1;
        if (*done) return 0;
    }
    return 0;
}

/* ── Program ────────────────────────────────────────────────────────── */

int SS_program_init(SS_program *p, const char *src) {
    memset(p, 0, sizeof(*p));
    SS_token *tokens = NULL;
    size_t tokens_len = 0;
    if (SS_lex(src, &tokens, &tokens_len) != 0) return -1;
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
    p->globals[p->globals_len].name = str_dup(name);
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

int SS_program_run(SS_program *p, const char *name,
                    SS_call_fn fn, void *userdata) {
    eval_ctx ctx = { .program = p, .call_fn = fn, .userdata = userdata };
    return eval_run(&ctx, name);
}
