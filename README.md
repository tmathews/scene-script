# SceneScript

A small C library for a simple scripting language designed for NPC dialog and
scene logic in video games.

SceneScript is easy to read, write, and integrate. It has basic data types
(boolean, number, string) and simple logic checks. There are no functions,
loops, or structures. Instead you hook up method calls to your application
logic, and return values that can be used in conditionals.

Method arguments are variadic and untyped so you can wire up game code easily
and handle type checking on your side.

You can branch into other scripts with the `run` keyword — handy for large
contextual NPC dialogs. End a branch of logic with the `end` keyword.

SceneScript supports two execution modes: a simple synchronous callback, and a
**yield-based context API** that lets you pause execution mid-script and resume
later — perfect for animated dialog, waiting on player input, or any async game
event.

SceneScript doesn't dictate how you organize your files. Load scripts from a
file, directory, network, or however you like — just pass the source text to
`SS_program_init`.

## Building

```sh
make          # builds libscene_script.a
make test     # builds and runs the test suite
make demo     # builds the interactive demo app
make clean
```

Requires a C11 compiler and `make`.

## Quick Start

### Synchronous (callback)

The simplest way to run a script — provide a callback that handles every call:

```c
#include "scene_script.h"
#include <stdio.h>

int my_handler(const SS_call *call, SS_value *result, void *userdata) {
    printf("Called: %s\n", call->name);
    *result = SS_nil_value();
    return 0;
}

int main(void) {
    const char *src =
        "script Entry:\n"
        "\tDialog(`Hello, adventurer!`)\n"
        "\tif HasItem(`Key`):\n"
        "\t\tDialog(`The door opens...`)\n"
        "\t\tend\n"
        "\tDialog(`Come back when you find the key.`)\n";

    SS_program p = {0};
    SS_program_init(&p, src);
    SS_program_run(&p, "Entry", my_handler, NULL);
    SS_program_free(&p);
    return 0;
}
```

### Yield-based (context API)

For games where you need to pause mid-script (e.g. wait for animated dialog to
finish), use the context API. Each call yields control back to you:

```c
#include "scene_script.h"
#include <stdio.h>

int main(void) {
    const char *src =
        "script Entry:\n"
        "\tDialog(`Hello!`)\n"
        "\tif HasItem(`Key`):\n"
        "\t\tDialog(`The door opens...`)\n";

    SS_program p = {0};
    SS_program_init(&p, src);

    SS_context *ctx = SS_context_create(&p, "Entry");
    SS_status s;
    while ((s = SS_context_step(ctx)) == SS_STATUS_CALL) {
        const SS_call *call = SS_context_get_call(ctx);
        printf("Call: %s\n", call->name);

        // Handle the call, then provide a result
        SS_context_set_result(ctx, SS_nil_value());

        // You can break here, save ctx, and resume later!
    }

    SS_context_free(ctx);
    SS_program_free(&p);
    return 0;
}
```

The context can be held across frames — break out of the loop when you need to
wait for something (animation, player input, network), and call
`SS_context_step` again when you're ready to continue.

## API Overview

### Values

```c
SS_value SS_nil_value(void);
SS_value SS_bool_value(bool b);
SS_value SS_number_value(double n);
SS_value SS_string_value(const char *s);  // copies the string
void     SS_value_free(SS_value *v);      // frees string values
```

Values are tagged unions (`SS_value_type`: `SS_VAL_NIL`, `SS_VAL_BOOL`,
`SS_VAL_NUMBER`, `SS_VAL_STRING`). String values are heap-allocated and must
be freed with `SS_value_free`.

### Program Lifecycle

```c
SS_program p = {0};
SS_program_init(&p, src);                          // parse source text
SS_program_set_global(&p, "PlayerName", SS_string_value("Ada"));
SS_program_run(&p, "Entry", my_callback, userdata); // run a named script
SS_program_free(&p);                                // free everything
```

### Synchronous Callback

When a script calls a function (e.g. `Dialog(\`hello\`)`), your callback is
invoked:

```c
typedef int (*SS_call_fn)(const SS_call *call, SS_value *result, void *userdata);
```

- `call->name` — the function name (e.g. `"Dialog"`)
- `call->args` / `call->args_len` — the arguments as `SS_value` array
- Write the return value into `*result` (default to `SS_nil_value()`)
- Return `0` on success, non-zero on error

### Context API (yield-based)

For non-blocking execution, use the context API:

```c
SS_context *ctx = SS_context_create(&p, "Entry"); // create a context
SS_status s = SS_context_step(ctx);               // advance execution

// When s == SS_STATUS_CALL:
const SS_call *call = SS_context_get_call(ctx);   // inspect the pending call
SS_context_set_result(ctx, some_value);            // provide the return value
// then call SS_context_step(ctx) again to continue

// When s == SS_STATUS_DONE: script finished
// When s == SS_STATUS_ERROR: something went wrong

SS_context_free(ctx);                              // free the context
```

The context holds the entire execution state on an internal stack. You can
pause between any two calls and resume whenever you like — next frame, next
second, or after an animation finishes.

### Globals

Set globals before running a script. Scripts can read them as bare words:

```c
SS_program_set_global(&p, "Difficulty", SS_number_value(3));
```

```
script Entry:
    if Difficulty:
        Dialog(`It's going to be tough.`)
```

## Demo

Run `make demo && ./demo` to try an interactive demo that implements the
functions from [demo.script](./demo.script) — dialog, inventory checks,
prompts, teleportation, and shop opening. See [demo.c](./demo.c) for the full
source.

## Language Reference

### Scripts

A source file contains one or more named scripts. Use dot notation to
namespace scripts by category (e.g. NPC name):

```
script Entry:
    Dialog(`Hello!`)

script Goofbert.Greet:
    Dialog(`Well hello there!`)

script Goofbert.Shop:
    Dialog(`Fancy some warez?`)
```

Namespaced names work everywhere a script name is used, including `run`
and `SS_context_create`.

### Data Types

| Type    | Examples              |
|---------|-----------------------|
| Boolean | `true`, `false`       |
| Number  | `1`, `3.14`, `-5`     |
| String  | `` `hello world` ``   |

Strings are delimited by backticks. Use `\`` to escape a backtick inside a
string.

### Conditionals

```
if HasItem(`Key`):
    Dialog(`You have the key!`)
elif HasItem(`Lockpick`):
    Dialog(`You pick the lock.`)
else:
    Dialog(`The door is locked.`)
```

Conditions support `and`, `or`, `not`, and parenthesised grouping.

### Switch / Case

Use `switch` to match a value against multiple cases. Cases can have
comma-separated values. There is no default or fallthrough — execution
continues after the switch if nothing matches.

```
switch GetMap():
    case 1, 2:
        Dialog(`You are in the forest.`)
    case `town`:
        Dialog(`Welcome to town!`)
```

### Keywords

| Keyword  | Description                          |
|----------|--------------------------------------|
| `if`     | Conditional branch                   |
| `elif`   | Else-if branch                       |
| `else`   | Else branch                          |
| `switch` | Switch on a value                    |
| `case`   | Match arm inside a switch            |
| `run`    | Jump to another script               |
| `end`    | Stop execution of the current branch |
| `true`   | Boolean literal                      |
| `false`  | Boolean literal                      |
| `and`    | Logical AND                          |
| `or`     | Logical OR                           |
| `not`    | Logical NOT (prefix)                 |
| `is`     | Equality (`a is b`)                  |
| `gt`     | Greater than (`a gt b`)              |
| `gte`    | Greater than or equal (`a gte b`)    |
| `lt`     | Less than (`a lt b`)                 |
| `lte`    | Less than or equal (`a lte b`)       |

### Comments

Lines starting with `#` are comments:

```
# This is a comment
Dialog(`Hi!`)  # inline comment
```

### Indentation

Blocks are delimited by tabs. Each nested level requires one additional tab.

Please look at the [demo file](./demo.script) for a fuller example.

Copyright 2023 - 2026 Thomas Mathews
