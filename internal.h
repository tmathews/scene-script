// scene_script_internal.h – Shared internal utilities for SceneScript
//
// This header is for internal use by ss_lexer.c, ss_parser.c, and ss_eval.c.
// Do not include from external code.
#pragma once

#include <stdlib.h>
#include <string.h>

#include "scene_script.h"

static inline char *ss_str_dup(const char *s) {
	size_t len = strlen(s);
	char *d = malloc(len + 1);
	if (d)
		memcpy(d, s, len + 1);
	return d;
}

static inline char *ss_str_ndup(const char *s, size_t n) {
	char *d = malloc(n + 1);
	if (d) {
		memcpy(d, s, n);
		d[n] = '\0';
	}
	return d;
}
