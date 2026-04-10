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
            /* Remove indent tokens emitted earlier on this comment line */
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

/* ── Evaluator (stack machine) ─────────────────────────────────────── */

typedef enum { FRAME_BLOCK, FRAME_COND, FRAME_EXPR } frame_type;

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
    if (f->type != FRAME_EXPR) return;
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
    if (ctx->stack_len == 0) return;
    ctx->stack_len--;
    frame_cleanup(&ctx->stack[ctx->stack_len]);
}

static void clear_stack(SS_context *ctx) {
    while (ctx->stack_len > 0) pop_frame(ctx);
}

static void push_block_frame(SS_context *ctx, const SS_block *block) {
    frame f = { .type = FRAME_BLOCK,
                .block = { .block = block, .index = 0, .awaiting = false } };
    push_frame(ctx, f);
}

static void push_cond_frame(SS_context *ctx, const SS_statement *stmt) {
    frame f = { .type = FRAME_COND,
                .cond = { .stmt = stmt, .phase = COND_EVAL_MAIN, .alt_index = 0 } };
    push_frame(ctx, f);
}

static void push_expr_frame(SS_context *ctx, const SS_expression *expr) {
    frame f = { .type = FRAME_EXPR,
                .expr = { .expr = expr, .phase = 0 } };
    push_frame(ctx, f);
}

static const SS_script *find_script(const SS_program *p, const char *name) {
    for (size_t i = 0; i < p->scripts_len; i++)
        if (strcmp(p->scripts[i].name, name) == 0)
            return &p->scripts[i];
    return NULL;
}

/* Resolve leaf expression (bool, number, string, word) immediately. */
static int resolve_leaf(SS_context *ctx, const SS_expression *expr, SS_value *out) {
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
        if (!g) return -1;
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
        if (resolve_leaf(ctx, expr, &ctx->result) != 0) return -1;
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
        /* phase 1: child result in ctx->result */
        SS_value child = ctx->result;
        ctx->result = SS_nil_value();
        int rc = -1;
        switch (expr->op) {
        case SS_TOK_MINUS:
            if (child.type != SS_VAL_NUMBER) { SS_value_free(&child); return -1; }
            ctx->result = SS_number_value(child.number * -1.0);
            rc = 0;
            break;
        case SS_TOK_NOT:
            if (!SS_value_is_boolean(&child)) { SS_value_free(&child); return -1; }
            ctx->result = SS_bool_value(!SS_value_bool(&child));
            rc = 0;
            break;
        default:
            break;
        }
        SS_value_free(&child);
        if (rc != 0) return -1;
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
            if (!SS_value_is_boolean(&left)) { SS_value_free(&left); return -1; }
            bool lb = SS_value_bool(&left);
            /* Short-circuit */
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
        /* phase 2: right result in ctx->result */
        SS_value right = ctx->result;
        ctx->result = SS_nil_value();
        SS_value left = f->expr.left;
        f->expr.has_left = false;
        if (!SS_value_is_boolean(&right)) {
            SS_value_free(&left); SS_value_free(&right); return -1;
        }
        bool lb = SS_value_bool(&left);
        bool rb = SS_value_bool(&right);
        SS_value_free(&left);
        SS_value_free(&right);
        switch (expr->op) {
        case SS_TOK_AND: ctx->result = SS_bool_value(lb && rb); break;
        case SS_TOK_OR:  ctx->result = SS_bool_value(lb || rb); break;
        default: return -1;
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

        /* phase 3: resumed after yield, result in ctx->result (set by user) */
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
        if (!script) return -1;
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
        if (!SS_value_is_boolean(&res)) { SS_value_free(&res); return -1; }
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
        if (!SS_value_is_boolean(&res)) { SS_value_free(&res); return -1; }
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
    if (!script) return NULL;
    SS_context *ctx = calloc(1, sizeof(SS_context));
    ctx->program = p;
    ctx->result = SS_nil_value();
    push_block_frame(ctx, &script->block);
    return ctx;
}

SS_status SS_context_step(SS_context *ctx) {
    if (ctx->done) return SS_STATUS_DONE;
    if (ctx->error) return SS_STATUS_ERROR;

    while (ctx->stack_len > 0) {
        frame *top = &ctx->stack[ctx->stack_len - 1];
        int rc;
        switch (top->type) {
        case FRAME_BLOCK: rc = step_block(ctx); break;
        case FRAME_COND:  rc = step_cond(ctx); break;
        case FRAME_EXPR:  rc = step_expr(ctx); break;
        default: rc = -1; break;
        }
        if (rc == 1) return SS_STATUS_CALL;
        if (rc < 0) { ctx->error = true; clear_stack(ctx); return SS_STATUS_ERROR; }
        if (ctx->done) return SS_STATUS_DONE;
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
    if (!ctx) return;
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
    SS_context *ctx = SS_context_create(p, name);
    if (!ctx) return -1;
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
