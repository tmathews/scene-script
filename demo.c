/*
 * demo.c – Interactive sample app for SceneScript (demo.script)
 *
 * Build:  make demo
 * Run:    ./demo
 *
 * Simulates a small game world with maps, items, dialog, and teleportation.
 * Uses the yield-based context API so each call can be handled interactively.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_script.h"

/* ── Game state ─────────────────────────────────────────────────────── */

static int current_map = 1;
static char npc_name[64] = "NPC";
static int last_answer = 0;

typedef struct {
	char name[64];
	int count;
} item_t;

static item_t inventory[16];
static int inventory_count = 0;

static void give_item(const char *name, int count) {
	for (int i = 0; i < inventory_count; i++) {
		if (strcmp(inventory[i].name, name) == 0) {
			inventory[i].count += count;
			return;
		}
	}
	if (inventory_count < 16) {
		snprintf(inventory[inventory_count].name, 64, "%s", name);
		inventory[inventory_count].count = count;
		inventory_count++;
	}
}

/* ── Call handler ───────────────────────────────────────────────────── */

static SS_value handle_call(const SS_call *call) {
	if (strcmp(call->name, "Name") == 0) {
		snprintf(npc_name, sizeof(npc_name), "%s", call->args[0].string);
		printf("  [NPC name set to \"%s\"]\n", npc_name);
		return SS_nil_value();
	}

	if (strcmp(call->name, "OnMap") == 0) {
		int map = (int)call->args[0].number;
		bool result = (current_map == map);
		printf("  [OnMap(%d) → %s  (you are on map %d)]\n", map,
			result ? "true" : "false", current_map);
		return SS_bool_value(result);
	}

	if (strcmp(call->name, "HasItem") == 0) {
		const char *item = call->args[0].string;
		int count = 0;
		for (int i = 0; i < inventory_count; i++)
			if (strcmp(inventory[i].name, item) == 0)
				count = inventory[i].count;
		printf("  [HasItem(\"%s\") → %d]\n", item, count);
		return SS_number_value(count);
	}

	if (strcmp(call->name, "Dialog") == 0) {
		printf("\n  %s: \"%s\"\n\n", npc_name, call->args[0].string);
		printf("  (Press ENTER to continue...)");
		while (getchar() != '\n') {
		}
		return SS_nil_value();
	}

	if (strcmp(call->name, "Prompt") == 0) {
		printf("\n  %s: \"%s\"\n", npc_name, call->args[0].string);
		for (size_t i = 1; i < call->args_len; i++)
			printf("    %zu) %s\n", i, call->args[i].string);
		printf("  Your choice: ");
		int choice = 0;
		if (scanf("%d", &choice) != 1)
			choice = 1;
		while (getchar() != '\n') {
		}
		last_answer = choice;
		printf("  [Answer set to %d]\n", last_answer);
		return SS_nil_value();
	}

	if (strcmp(call->name, "Answer") == 0) {
		int n = (int)call->args[0].number;
		bool result = (last_answer == n);
		printf("  [Answer(%d) → %s]\n", n, result ? "true" : "false");
		return SS_bool_value(result);
	}

	if (strcmp(call->name, "Teleport") == 0) {
		int map = (int)call->args[0].number;
		const char *dest = call->args[1].string;
		current_map = map;
		printf("\n  ✦ Teleported to Map %d (\"%s\") ✦\n\n", map, dest);
		return SS_nil_value();
	}

	if (strcmp(call->name, "OpenShop") == 0) {
		printf("\n  ═══ Shop opened: %s ═══\n", call->args[0].string);
		printf("  (Imagine a shop UI here!)\n\n");
		return SS_nil_value();
	}

	printf("  [Unknown call: %s]\n", call->name);
	return SS_nil_value();
}

/* ── Main ───────────────────────────────────────────────────────────── */

static void print_status(void) {
	printf("┌─────────────────────────────────┐\n");
	printf("│  Map: %d", current_map);
	printf("  │  Items:");
	if (inventory_count == 0)
		printf(" (none)");
	for (int i = 0; i < inventory_count; i++)
		printf(" %s×%d", inventory[i].name, inventory[i].count);
	printf("\n");
	printf("└─────────────────────────────────┘\n\n");
}

int main(int argc, char **argv) {
	const char *script_file = "demo.script";
	if (argc > 1)
		script_file = argv[1];

	/* Read the script file */
	FILE *f = fopen(script_file, "r");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", script_file);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	long flen = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *src = malloc((size_t)flen + 1);
	fread(src, 1, (size_t)flen, f);
	src[flen] = '\0';
	fclose(f);

	/* Parse */
	SS_program prog;
	if (SS_program_init(&prog, src) != 0) {
		fprintf(stderr, "error: failed to parse script\n");
		free(src);
		return 1;
	}
	free(src);

	printf("\n");
	printf("╔═══════════════════════════════════╗\n");
	printf("║   SceneScript Demo – demo.script  ║\n");
	printf("╚═══════════════════════════════════╝\n\n");

	/* Let the player pick starting conditions */
	printf("Which map are you on? (1, 2, or 3): ");
	if (scanf("%d", &current_map) != 1)
		current_map = 1;
	while (getchar() != '\n') {
	}

	printf("Do you have a SecretShopToken? (y/n): ");
	int ch = getchar();
	if (ch == 'y' || ch == 'Y')
		give_item("SecretShopToken", 1);
	while (ch != '\n' && ch != EOF)
		ch = getchar();

	printf("\n");
	print_status();
	printf("--- Running \"Entry\" script ---\n\n");

	/* Run using the context API */
	SS_context *ctx = SS_context_create(&prog, "Entry");
	if (!ctx) {
		fprintf(stderr, "error: script 'Entry' not found\n");
		SS_program_free(&prog);
		return 1;
	}

	SS_status status;
	while ((status = SS_context_step(ctx)) == SS_STATUS_CALL) {
		const SS_call *call = SS_context_get_call(ctx);
		SS_value result = handle_call(call);
		SS_context_set_result(ctx, result);
	}

	if (status == SS_STATUS_ERROR) {
		fprintf(stderr, "\nerror: script execution failed\n");
	}

	printf("--- Script finished ---\n\n");
	print_status();

	SS_context_free(ctx);
	SS_program_free(&prog);
	return 0;
}
