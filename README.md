SAD-Script
----------

"S-Expressions and Dashes"

"Simple and Dumb"

A dynamically typed, interpreted, garbage collected language with 5 data types: int, double, bool, string, list.  Functions are not first-class citizens and cannot be assigned or passed, but a special form exists for list comprehensions and pipelined operations using pseudo-lambda functions.  No semicolons, and whitespace is insignificant.  Statements are BASIC-like and expressions are Lisp-like, but don't be fooled: lists are implemented as arrays, not cons cells.

Written in C89 and compiles cleanly under `gcc -ansi -pedantic -Wall -Werror`, TinyCC (`tcc`), and MSVC 2013.  The interpreter can be embedded in client applications simply by including one .C and one .H file in the project.  Since the bindings are in C, they can be accessed easily from any language with a FFI.  For instance .NET can access it via P/Invoke.

