#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_script.h"

static int tests_run, tests_passed, tests_failed;

#define ASSERT(cond, ...)        \
	do {                         \
		if (!(cond)) {           \
			printf("  FAIL: ");  \
			printf(__VA_ARGS__); \
			printf("\n");        \
			tests_failed++;      \
			return;              \
		}                        \
	} while (0)

#define RUN_TEST(fn)                     \
	do {                                 \
		tests_run++;                     \
		printf("%-50s ", #fn);           \
		fn();                            \
		if (tests_failed == prev_fail) { \
			tests_passed++;              \
			printf("PASS\n");            \
		}                                \
		prev_fail = tests_failed;        \
	} while (0)

/* ── helpers ────────────────────────────────────────────────────────── */

static char *read_file(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(sz + 1);
	fread(buf, 1, sz, f);
	buf[sz] = '\0';
	fclose(f);
	return buf;
}

/* Parse first script from a source string, return 0 on success. */
static int parse_first(const char *src, SS_script **out, SS_script **all, size_t *all_len) {
	SS_token *tokens;
	size_t tokens_len;
	if (SS_lex(src, &tokens, &tokens_len) != 0)
		return -1;
	int rc = SS_parse(tokens, tokens_len, all, all_len);
	SS_tokens_free(tokens, tokens_len);
	if (rc != 0)
		return rc;
	*out = &(*all)[0];
	return 0;
}

/* ── Lexer tests ────────────────────────────────────────────────────── */

static void test_lexer(void) {
	char *buf = read_file("./demo.script");
	ASSERT(buf != NULL, "Could not read demo.script");
	SS_token *tokens;
	size_t len;
	int rc = SS_lex(buf, &tokens, &len);
	ASSERT(rc == 0, "Lexer returned error");
	ASSERT(len > 0, "Lexer produced no tokens");
	SS_tokens_free(tokens, len);
	free(buf);
}

/* ── Parser tests ───────────────────────────────────────────────────── */

static void test_parse_empty_script(void) {
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first("script Entry:", &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_basic_expression(void) {
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first("script Entry:\n\t`i am string`", &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_run(void) {
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first("script Entry:\n\trun OtherScript", &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_end(void) {
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first("script Entry:\n\tend", &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_call_empty(void) {
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first("script Entry:\n\tHelloWorld()", &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_call_one_arg(void) {
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first("script Entry:\n\tHelloWorld(1)", &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_call_multi_args(void) {
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first("script Entry:\n\tHelloWorld(1, `hello`, true)", &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_bad_indent_short(void) {
	SS_script *s, *all;
	size_t n;
	int rc = parse_first("script Entry:\n\tHelloWorld()\nend", &s, &all, &n);
	ASSERT(rc != 0, "Should have errored with too short indent");
}

static void test_parse_bad_indent_long(void) {
	SS_script *s, *all;
	size_t n;
	int rc = parse_first("script Entry:\n\t\tHelloWorld()\t\tend", &s, &all, &n);
	ASSERT(rc != 0, "Should have errored with too long indent");
}

static void test_parse_multiline_string(void) {
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first("script Entry:\n\t`multiline\nstring\nshould\nwork.`", &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_number(void) {
	const char *src = "\nscript Entry:\n\tif 1.1:\n\t\tend\n";
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first(src, &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_conditional(void) {
	const char *src = "\nscript Entry:\n\tif 1:\n\t\tend\n";
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first(src, &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_conditional_empty(void) {
	const char *src = "\nscript Entry:\n\tif 1:\n";
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first(src, &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_conditional_nested(void) {
	const char *src =
		"\nscript Entry:\n"
		"\tif 1:\n"
		"\t\tif 2:\n"
		"\t\t\tend\n"
		"\t\telif 3:\n"
		"\telse:\n"
		"\t\t3\n";
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first(src, &s, &all, &n) == 0, "Parse failed");
	ASSERT(s->block.stmts_len == 1, "Expected 1 statement, got %zu", s->block.stmts_len);
	ASSERT(s->block.stmts[0].type == SS_STMT_IF, "Expected SS_STMT_IF");
	ASSERT(s->block.stmts[0].alts_len == 1, "Expected 1 alternative, got %zu", s->block.stmts[0].alts_len);
	ASSERT(s->block.stmts[0].alternatives[0]->type == SS_STMT_ELSE, "Expected SS_STMT_ELSE");
	SS_scripts_free(all, n);
}

static void test_parse_conditional_multi(void) {
	const char *src =
		"\nscript Entry:\n"
		"\tif 1:\n"
		"\t\trun HelloThere\n"
		"\tif 2:\n"
		"\t\tend\n";
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first(src, &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_conditional_else(void) {
	const char *src =
		"\nscript Entry:\n"
		"\tif 1:\n"
		"\t\tHelloJane()\n"
		"\telse:\n"
		"\t\tHelloBob()\n";
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first(src, &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_conditional_elif(void) {
	const char *src =
		"\nscript Entry:\n"
		"\tif 1:\n"
		"\t\tHelloJane()\n"
		"\telif 2:\n"
		"\t\tHelloBob()\n";
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first(src, &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

static void test_parse_conditional_if_chain(void) {
	const char *src =
		"\nscript Entry:\n"
		"\tif 1:\n"
		"\t\tHelloJane()\n"
		"\telif 2:\n"
		"\t\tHelloBob()\n"
		"\telse:\n"
		"\t\tHelloWorld()\n"
		"\t\tend\n";
	SS_script *s, *all;
	size_t n;
	ASSERT(parse_first(src, &s, &all, &n) == 0, "Parse failed");
	SS_scripts_free(all, n);
}

/* ── Evaluator tests ────────────────────────────────────────────────── */

typedef struct {
	int hello_count;
	SS_call last_call;
	SS_value last_args[8];
	int last_args_len;
} test_eval_state;

static int test_call_fn(const SS_call *call, SS_value *result, void *userdata) {
	test_eval_state *st = userdata;
	if (strcmp(call->name, "HelloWorld") == 0)
		st->hello_count++;
	/* Save last call info */
	st->last_call.name = call->name;
	st->last_args_len = (int)call->args_len;
	for (size_t i = 0; i < call->args_len && i < 8; i++) {
		/* Copy values; duplicate strings */
		if (call->args[i].type == SS_VAL_STRING) {
			st->last_args[i] = SS_string_value(call->args[i].string);
		} else {
			st->last_args[i] = call->args[i];
		}
	}
	*result = SS_nil_value();
	return 0;
}

static void free_test_state(test_eval_state *st) {
	for (int i = 0; i < st->last_args_len; i++) {
		SS_value_free(&st->last_args[i]);
	}
}

static void test_eval_simple(void) {
	const char *src = "script Entry:\n\ttrue";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int rc = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(rc == 0, "Run failed");
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_eval_call(void) {
	const char *src = "script Entry:\n\tHelloWorld()";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int rc = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(rc == 0, "Run failed");
	ASSERT(st.hello_count == 1, "Expected HelloWorld called 1 time, got %d", st.hello_count);
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_eval_conditional(void) {
	const char *src =
		"\nscript Entry:\n"
		"\tHelloWorld()\n"
		"\tif 1:\n"
		"\t\tHelloWorld()\n"
		"\telse:\n"
		"\t\ttrue\n"
		"\tif false:\n"
		"\t\tend\n"
		"\telse:\n"
		"\t\tHelloWorld()\n"
		"\tif HelloWorld():\n"
		"\t\tHelloWorld()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int rc = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(rc == 0, "Run failed");
	ASSERT(st.hello_count == 4, "Expected HelloWorld called 4 times, got %d", st.hello_count);
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_eval_conditional_and_or(void) {
	const char *src =
		"\nscript Entry:\n"
		"\tif false:\n"
		"\t\tHelloWorld()\n"
		"\tif 1 and 2:\n"
		"\t\tHelloWorld()\n"
		"\tif false or 0:\n"
		"\t\tHelloWorld()\n"
		"\telse:\n"
		"\t\tHelloWorld()";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int rc = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(rc == 0, "Run failed");
	ASSERT(st.hello_count == 2, "Expected HelloWorld called 2 times, got %d", st.hello_count);
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_eval_comparators(void) {
	/* is: equality */
	const char *src_is =
		"script Entry:\n"
		"\tif 3 is 3:\n"
		"\t\tHelloWorld()\n"
		"\tif 3 is 4:\n"
		"\t\tHelloWorld()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src_is) == 0, "Init failed (is)");
	test_eval_state st = {0};
	int rc = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(rc == 0, "Run failed (is)");
	ASSERT(st.hello_count == 1, "is: expected 1 call, got %d", st.hello_count);
	free_test_state(&st);
	SS_program_free(&p);

	/* gt, gte, lt, lte */
	const char *src_cmp =
		"script Entry:\n"
		"\tif 5 gt 3:\n"
		"\t\tHelloWorld()\n"
		"\tif 3 gt 5:\n"
		"\t\tHelloWorld()\n"
		"\tif 5 gte 5:\n"
		"\t\tHelloWorld()\n"
		"\tif 3 lt 5:\n"
		"\t\tHelloWorld()\n"
		"\tif 5 lt 3:\n"
		"\t\tHelloWorld()\n"
		"\tif 5 lte 5:\n"
		"\t\tHelloWorld()\n";
	ASSERT(SS_program_init(&p, src_cmp) == 0, "Init failed (cmp)");
	memset(&st, 0, sizeof(st));
	rc = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(rc == 0, "Run failed (cmp)");
	/* 5>3=yes, 3>5=no, 5>=5=yes, 3<5=yes, 5<3=no, 5<=5=yes → 4 calls */
	ASSERT(st.hello_count == 4, "cmp: expected 4 calls, got %d", st.hello_count);
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_eval_comparator_string_is(void) {
	const char *src =
		"script Entry:\n"
		"\tif `hello` is `hello`:\n"
		"\t\tHelloWorld()\n"
		"\tif `hello` is `world`:\n"
		"\t\tHelloWorld()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int rc = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(rc == 0, "Run failed");
	ASSERT(st.hello_count == 1, "string is: expected 1 call, got %d", st.hello_count);
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_eval_comparator_with_calls(void) {
	/* Comparators should work with call return values via context API */
	const char *src =
		"script Entry:\n"
		"\tif GetLevel() gte 5:\n"
		"\t\tDialog(`high level`)\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	SS_context *ctx = SS_context_create(&p, "Entry");
	ASSERT(ctx != NULL, "Context create failed");

	/* GetLevel() call */
	SS_status s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL, "Expected CALL for GetLevel");
	ASSERT(strcmp(SS_context_get_call(ctx)->name, "GetLevel") == 0, "Expected GetLevel");
	SS_context_set_result(ctx, SS_number_value(10));

	/* Dialog call in the true branch */
	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL, "Expected CALL for Dialog");
	ASSERT(strcmp(SS_context_get_call(ctx)->name, "Dialog") == 0, "Expected Dialog");
	SS_context_set_result(ctx, SS_nil_value());

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE");

	SS_context_free(ctx);
	SS_program_free(&p);
}

static void test_eval_comparator_combined(void) {
	/* Comparators combined with and/or */
	const char *src =
		"script Entry:\n"
		"\tif 5 gt 3 and 2 lt 4:\n"
		"\t\tHelloWorld()\n"
		"\tif 5 lt 3 or 2 is 2:\n"
		"\t\tHelloWorld()\n"
		"\tif 5 lt 3 and 2 is 2:\n"
		"\t\tHelloWorld()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int rc = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(rc == 0, "Run failed");
	/* (5>3 && 2<4)=yes, (5<3 || 2==2)=yes, (5<3 && 2==2)=no → 2 calls */
	ASSERT(st.hello_count == 2, "combined: expected 2 calls, got %d", st.hello_count);
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_eval_call_arg_types(void) {
	const char *src = "script Entry:\n\tHelloWorld(`String`, 1337, 256.01, false)";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int rc = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(rc == 0, "Run failed");
	ASSERT(st.last_args_len == 4, "Expected 4 args, got %d", st.last_args_len);
	ASSERT(st.last_args[0].type == SS_VAL_STRING, "Arg 0 should be string, got %d", st.last_args[0].type);
	ASSERT(strcmp(st.last_args[0].string, "String") == 0, "Arg 0 value mismatch");
	ASSERT(st.last_args[1].type == SS_VAL_NUMBER, "Arg 1 should be number, got %d", st.last_args[1].type);
	ASSERT(st.last_args[1].number == 1337.0, "Arg 1 value mismatch: %f", st.last_args[1].number);
	ASSERT(st.last_args[2].type == SS_VAL_NUMBER, "Arg 2 should be number, got %d", st.last_args[2].type);
	ASSERT(fabs(st.last_args[2].number - 256.01) < 0.001, "Arg 2 value mismatch: %f", st.last_args[2].number);
	ASSERT(st.last_args[3].type == SS_VAL_BOOL, "Arg 3 should be bool, got %d", st.last_args[3].type);
	ASSERT(st.last_args[3].boolean == false, "Arg 3 should be false");
	free_test_state(&st);
	SS_program_free(&p);
}

/* ── Context (yield) tests ──────────────────────────────────────────── */

static void test_context_basic(void) {
	/* A script with one call should yield once, then finish. */
	const char *src = "script Entry:\n\tDialog(`hello`)";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	SS_context *ctx = SS_context_create(&p, "Entry");
	ASSERT(ctx != NULL, "Context create failed");

	SS_status s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL, "Expected CALL, got %d", s);
	const SS_call *call = SS_context_get_call(ctx);
	ASSERT(strcmp(call->name, "Dialog") == 0, "Expected Dialog, got %s", call->name);
	ASSERT(call->args_len == 1, "Expected 1 arg");
	ASSERT(call->args[0].type == SS_VAL_STRING, "Arg should be string");
	ASSERT(strcmp(call->args[0].string, "hello") == 0, "Arg value mismatch");

	SS_context_set_result(ctx, SS_nil_value());
	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE, got %d", s);

	SS_context_free(ctx);
	SS_program_free(&p);
}

static void test_context_multi_calls(void) {
	/* Multiple sequential calls yield one at a time. */
	const char *src =
		"script Entry:\n"
		"\tA()\n"
		"\tB()\n"
		"\tC()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	SS_context *ctx = SS_context_create(&p, "Entry");

	const char *expected[] = {"A", "B", "C"};
	for (int i = 0; i < 3; i++) {
		SS_status s = SS_context_step(ctx);
		ASSERT(s == SS_STATUS_CALL, "Call %d: expected CALL", i);
		ASSERT(strcmp(SS_context_get_call(ctx)->name, expected[i]) == 0,
			"Call %d: expected %s", i, expected[i]);
		SS_context_set_result(ctx, SS_nil_value());
	}

	SS_status s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE");

	SS_context_free(ctx);
	SS_program_free(&p);
}

static void test_context_conditional_call(void) {
	/* Call inside a conditional should yield. */
	const char *src =
		"script Entry:\n"
		"\tif HasItem(`key`):\n"
		"\t\tDialog(`unlocked`)\n"
		"\telse:\n"
		"\t\tDialog(`locked`)\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	SS_context *ctx = SS_context_create(&p, "Entry");

	/* First yield: HasItem call in the condition */
	SS_status s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL, "Expected CALL for HasItem");
	ASSERT(strcmp(SS_context_get_call(ctx)->name, "HasItem") == 0, "Expected HasItem");
	SS_context_set_result(ctx, SS_bool_value(true));

	/* Second yield: Dialog in the true branch */
	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL, "Expected CALL for Dialog");
	ASSERT(strcmp(SS_context_get_call(ctx)->name, "Dialog") == 0, "Expected Dialog");
	ASSERT(strcmp(SS_context_get_call(ctx)->args[0].string, "unlocked") == 0, "Expected unlocked");
	SS_context_set_result(ctx, SS_nil_value());

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE");

	SS_context_free(ctx);

	/* Now test the false branch */
	ctx = SS_context_create(&p, "Entry");
	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL, "Expected CALL for HasItem (2nd run)");
	SS_context_set_result(ctx, SS_bool_value(false));

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL, "Expected CALL for Dialog (else)");
	ASSERT(strcmp(SS_context_get_call(ctx)->args[0].string, "locked") == 0, "Expected locked");
	SS_context_set_result(ctx, SS_nil_value());

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE (2nd run)");

	SS_context_free(ctx);
	SS_program_free(&p);
}

static void test_context_and_or_calls(void) {
	/* Calls inside and/or expressions should yield, with short-circuit. */
	const char *src =
		"script Entry:\n"
		"\tif A() and B():\n"
		"\t\tC()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");

	/* Test 1: A() returns true → B() is evaluated → B() returns true → C() executes */
	SS_context *ctx = SS_context_create(&p, "Entry");
	SS_status s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL && strcmp(SS_context_get_call(ctx)->name, "A") == 0, "Expected A");
	SS_context_set_result(ctx, SS_bool_value(true));

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL && strcmp(SS_context_get_call(ctx)->name, "B") == 0, "Expected B");
	SS_context_set_result(ctx, SS_bool_value(true));

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL && strcmp(SS_context_get_call(ctx)->name, "C") == 0, "Expected C");
	SS_context_set_result(ctx, SS_nil_value());

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE");
	SS_context_free(ctx);

	/* Test 2: A() returns false → short-circuit, B() not called, C() not called */
	ctx = SS_context_create(&p, "Entry");
	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL && strcmp(SS_context_get_call(ctx)->name, "A") == 0, "Expected A (2)");
	SS_context_set_result(ctx, SS_bool_value(false));

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE after short-circuit");
	SS_context_free(ctx);

	SS_program_free(&p);
}

static void test_context_run_keyword(void) {
	/* The 'run' keyword should switch to another script. */
	const char *src =
		"script Entry:\n"
		"\tA()\n"
		"\trun Other\n"
		"\tB()\n"
		"\nscript Other:\n"
		"\tC()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	SS_context *ctx = SS_context_create(&p, "Entry");

	SS_status s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL && strcmp(SS_context_get_call(ctx)->name, "A") == 0, "Expected A");
	SS_context_set_result(ctx, SS_nil_value());

	/* After 'run Other', B() is skipped, C() is called */
	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL && strcmp(SS_context_get_call(ctx)->name, "C") == 0, "Expected C, not B");
	SS_context_set_result(ctx, SS_nil_value());

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE");

	SS_context_free(ctx);
	SS_program_free(&p);
}

static void test_context_end_keyword(void) {
	/* The 'end' keyword should stop execution immediately. */
	const char *src =
		"script Entry:\n"
		"\tA()\n"
		"\tend\n"
		"\tB()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	SS_context *ctx = SS_context_create(&p, "Entry");

	SS_status s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL && strcmp(SS_context_get_call(ctx)->name, "A") == 0, "Expected A");
	SS_context_set_result(ctx, SS_nil_value());

	/* end → DONE, B() never called */
	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE after end");

	SS_context_free(ctx);
	SS_program_free(&p);
}

static void test_context_deferred_resume(void) {
	/* Simulate saving and resuming context across "frames" (game loop style). */
	const char *src =
		"script Entry:\n"
		"\tDialog(`line 1`)\n"
		"\tDialog(`line 2`)\n"
		"\tDialog(`line 3`)\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	SS_context *ctx = SS_context_create(&p, "Entry");

	const char *expected[] = {"line 1", "line 2", "line 3"};
	for (int i = 0; i < 3; i++) {
		SS_status s = SS_context_step(ctx);
		ASSERT(s == SS_STATUS_CALL, "Frame %d: expected CALL", i);
		ASSERT(strcmp(SS_context_get_call(ctx)->args[0].string, expected[i]) == 0,
			"Frame %d: expected '%s'", i, expected[i]);
		/* Simulate deferred resume — context is held, result provided later */
		SS_context_set_result(ctx, SS_nil_value());
	}

	SS_status s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE after all dialogs");

	SS_context_free(ctx);
	SS_program_free(&p);
}

/* ── Switch tests ───────────────────────────────────────────────────── */

static void test_parse_switch(void) {
	const char *src =
		"script Entry:\n"
		"\tswitch 1:\n"
		"\t\tcase 1, 2:\n"
		"\t\t\tDialog(`one or two`)\n"
		"\t\tcase 3:\n"
		"\t\t\tDialog(`three`)\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	ASSERT(p.scripts_len == 1, "Expected 1 script");

	const SS_block *blk = &p.scripts[0].block;
	ASSERT(blk->stmts_len == 1, "Expected 1 statement (switch)");
	ASSERT(blk->stmts[0].type == SS_STMT_SWITCH, "Expected SWITCH");

	const SS_block *cases = blk->stmts[0].block;
	ASSERT(cases->stmts_len == 2, "Expected 2 cases");
	ASSERT(cases->stmts[0].type == SS_STMT_CASE, "Case 0 type");
	ASSERT(cases->stmts[1].type == SS_STMT_CASE, "Case 1 type");

	/* First case has 2 match values */
	ASSERT(cases->stmts[0].expression->type == SS_EXP_LIST, "Case 0 expr type");
	ASSERT(cases->stmts[0].expression->children_len == 2, "Case 0 has 2 values");

	/* Second case has 1 match value */
	ASSERT(cases->stmts[1].expression->children_len == 1, "Case 1 has 1 value");

	SS_program_free(&p);
}

static void test_eval_switch(void) {
	const char *src =
		"script Entry:\n"
		"\tswitch 2:\n"
		"\t\tcase 1:\n"
		"\t\t\tA()\n"
		"\t\tcase 2, 3:\n"
		"\t\t\tB()\n"
		"\t\tcase 4:\n"
		"\t\t\tC()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int result = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(result == 0, "Run failed");
	ASSERT(strcmp(st.last_call.name, "B") == 0, "Expected B, got %s", st.last_call.name);
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_eval_switch_no_match(void) {
	const char *src =
		"script Entry:\n"
		"\tswitch 99:\n"
		"\t\tcase 1:\n"
		"\t\t\tA()\n"
		"\t\tcase 2:\n"
		"\t\t\tB()\n"
		"\tDialog(`after`)\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int result = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(result == 0, "Run failed");
	ASSERT(strcmp(st.last_call.name, "Dialog") == 0, "Expected Dialog");
	ASSERT(strcmp(st.last_args[0].string, "after") == 0, "Expected 'after'");
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_eval_switch_string(void) {
	const char *src =
		"script Entry:\n"
		"\tswitch `hello`:\n"
		"\t\tcase `world`:\n"
		"\t\t\tA()\n"
		"\t\tcase `hello`, `hi`:\n"
		"\t\t\tB()\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	test_eval_state st = {0};
	int result = SS_program_run(&p, "Entry", test_call_fn, &st);
	ASSERT(result == 0, "Run failed");
	ASSERT(strcmp(st.last_call.name, "B") == 0, "Expected B");
	free_test_state(&st);
	SS_program_free(&p);
}

static void test_context_switch(void) {
	const char *src =
		"script Entry:\n"
		"\tswitch GetMap():\n"
		"\t\tcase 1:\n"
		"\t\t\tDialog(`map one`)\n"
		"\t\tcase 2:\n"
		"\t\t\tDialog(`map two`)\n";
	SS_program p;
	ASSERT(SS_program_init(&p, src) == 0, "Init failed");
	SS_context *ctx = SS_context_create(&p, "Entry");

	/* First yield: GetMap() call */
	SS_status s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL, "Expected CALL for GetMap");
	ASSERT(strcmp(SS_context_get_call(ctx)->name, "GetMap") == 0, "Expected GetMap");
	SS_context_set_result(ctx, SS_number_value(2));

	/* Second yield: Dialog call from matched case */
	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_CALL, "Expected CALL for Dialog");
	ASSERT(strcmp(SS_context_get_call(ctx)->name, "Dialog") == 0, "Expected Dialog");
	ASSERT(strcmp(SS_context_get_call(ctx)->args[0].string, "map two") == 0,
		"Expected 'map two'");
	SS_context_set_result(ctx, SS_nil_value());

	s = SS_context_step(ctx);
	ASSERT(s == SS_STATUS_DONE, "Expected DONE");

	SS_context_free(ctx);
	SS_program_free(&p);
}

/* ── main ───────────────────────────────────────────────────────────── */

int main(void) {
	int prev_fail = 0;
	printf("\n=== scene_script tests ===\n\n");

	/* Lexer */
	RUN_TEST(test_lexer);

	/* Parser */
	RUN_TEST(test_parse_empty_script);
	RUN_TEST(test_parse_basic_expression);
	RUN_TEST(test_parse_run);
	RUN_TEST(test_parse_end);
	RUN_TEST(test_parse_call_empty);
	RUN_TEST(test_parse_call_one_arg);
	RUN_TEST(test_parse_call_multi_args);
	RUN_TEST(test_parse_bad_indent_short);
	RUN_TEST(test_parse_bad_indent_long);
	RUN_TEST(test_parse_multiline_string);
	RUN_TEST(test_parse_number);
	RUN_TEST(test_parse_conditional);
	RUN_TEST(test_parse_conditional_empty);
	RUN_TEST(test_parse_conditional_nested);
	RUN_TEST(test_parse_conditional_multi);
	RUN_TEST(test_parse_conditional_else);
	RUN_TEST(test_parse_conditional_elif);
	RUN_TEST(test_parse_conditional_if_chain);

	/* Evaluator */
	RUN_TEST(test_eval_simple);
	RUN_TEST(test_eval_call);
	RUN_TEST(test_eval_conditional);
	RUN_TEST(test_eval_conditional_and_or);
	RUN_TEST(test_eval_comparators);
	RUN_TEST(test_eval_comparator_string_is);
	RUN_TEST(test_eval_comparator_with_calls);
	RUN_TEST(test_eval_comparator_combined);
	RUN_TEST(test_eval_call_arg_types);

	/* Context (yield) */
	RUN_TEST(test_context_basic);
	RUN_TEST(test_context_multi_calls);
	RUN_TEST(test_context_conditional_call);
	RUN_TEST(test_context_and_or_calls);
	RUN_TEST(test_context_run_keyword);
	RUN_TEST(test_context_end_keyword);
	RUN_TEST(test_context_deferred_resume);

	/* Switch */
	RUN_TEST(test_parse_switch);
	RUN_TEST(test_eval_switch);
	RUN_TEST(test_eval_switch_no_match);
	RUN_TEST(test_eval_switch_string);
	RUN_TEST(test_context_switch);

	printf("\n%d tests run, %d passed, %d failed\n\n", tests_run, tests_passed, tests_failed);
	return tests_failed > 0 ? 1 : 0;
}
