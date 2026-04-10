# SceneScript

A small C library for a simple scripting language designed for NPC dialog and
scene logic in video games.

SceneScript is easy to read, write, and integrate. It has basic data types
(boolean, number, string) and simple logic checks. There are no functions,
loops, or structures. Instead you hook up method calls to your application
logic via a callback, and return values that can be used in conditionals.

Method arguments are variadic and untyped so you can wire up game code easily
and handle type checking on your side.

You can branch into other scripts with the `run` keyword — handy for large
contextual NPC dialogs. End a branch of logic with the `end` keyword.

SceneScript doesn't dictate how you organize your files. Load scripts from a
file, directory, network, or however you like — just pass the source text to
`SS_program_init`.

## Building

```sh
make          # builds libnpc_script.a
make test     # builds and runs the test suite
make clean
```

Requires a C11 compiler and `make`.

## Quick Start

```c
#include "npc_script.h"
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

### Callback

When a script calls a function (e.g. `Dialog(\`hello\`)`), your callback is
invoked:

```c
typedef int (*SS_call_fn)(const SS_call *call, SS_value *result, void *userdata);
```

- `call->name` — the function name (e.g. `"Dialog"`)
- `call->args` / `call->args_len` — the arguments as `SS_value` array
- Write the return value into `*result` (default to `SS_nil_value()`)
- Return `0` on success, non-zero on error

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

## Language Reference

### Scripts

A source file contains one or more named scripts:

```
script Entry:
    Dialog(`Hello!`)

script Goodbye:
    Dialog(`See you later.`)
```

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

### Keywords

| Keyword | Description                          |
|---------|--------------------------------------|
| `if`    | Conditional branch                   |
| `elif`  | Else-if branch                       |
| `else`  | Else branch                          |
| `run`   | Jump to another script               |
| `end`   | Stop execution of the current branch |
| `true`  | Boolean literal                      |
| `false` | Boolean literal                      |
| `and`   | Logical AND                          |
| `or`    | Logical OR                           |
| `not`   | Logical NOT (prefix)                 |

### Comments

Lines starting with `#` are comments:

```
# This is a comment
Dialog(`Hi!`)  # inline comment
```

### Indentation

Blocks are delimited by tabs. Each nested level requires one additional tab.

Please look at the [demo file](./demo.script) for a fuller example.

Copyright 2023 Thomas Mathews
