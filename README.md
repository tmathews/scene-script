# NPC Script

Is a very simple programming language written to assist writing scripts for 
NPC logic in my video games (that are written in Golang).

It is designed to be easy to read, write, and hook into your code. There are
basic data types (boolean, number, string) and simple logic checks. There are
no functions, loops, structures, etc. Instead you hook up method calls to
internal application logic to perform what is needed and returns a value that
can be in a conditional.

Method arguments are variadic and untyped so you can make calls to your Go code
easily and do the appropriate handling there. This allows for easy
implementation of game code from the scripting side.

You can branch off into other scripts by calling the `run` keyword. This is 
handy for writing large contextual NPC logics. Alternatively you can end a 
branch of logic with the `end` keyword.

NPC Script doesn't dictate how you should organize your files. Instead the
programmer must do this. This allows you to decide if you want to work with
scripts from a file, files, a directory, multiple directories, recursively, or
even from a network. That decision is up to you.

It's designed to be used with goroutines so that you can pause the run context
and resume it later. This is handy when you need to perform in game actions 
such as rendering out text letter by letter or wait for other animations, etc.

Additionally i18 is built into the language first hand with the `$` operator.
This allows you to write a separate dictionary for all your localized strings
and swap them at runtime without affecting your NPC scripts.

Please look at the [demo file](./demo.script) for an example. Future work may
include switch statements (as they are really handy).

Copyright 2023 Thomas Mathews
