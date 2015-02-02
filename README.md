SAD-Script
----------

<p align="center">
"S-Expressions and Dashes" or "Simple and Dumb"
</p>

A dynamically-typed, interpreted, garbage-collected language with 8 data types: Int, Double, Bool, String, List, Function, Error, and Type.  There are no semicolons, and whitespace is insignificant.  Identifiers can contain anything except parentheses, braces, brackets, double quotes, and colons.  Identifiers must contain at least one character that isn't a digit and isn't a period, in order to distinguish identifiers from integer and double literals.

Control flow structures are JavaScript-like, with mandatory braces.  Function calls and expressions are Lisp-like, with parentheses around prefix-style function calls and brackets around infix-style function calls.  The two styles can be mixed and matched to taste.

```
function fizz-buzz (x) {
   for i from 1 to 100 {
      var multiple-of-3 = (zero? [i % 3])
      var multiple-of-5 = (zero? [i % 5])
      
      if [multiple-of-3 and multiple-of-5] {
         (println "FizzBuzz")
      } elseif multiple-of-3 {
         (println "Fizz")
      } elseif multiple-of-5 {
         (println "Buzz")
      } else {
         (println i)
      }
   }
}
```

There is a convenient syntax for lambda functions (with closures), as well as a pipelined query syntax similar to F#'s `|>` operator.  Programmers familiar with C#'s Enumerable extension methods and its conventional method chaining syntax will find this syntax familiar.  Unlike the C# extension methods, the calls in a query are executed eagerly rather than lazily.

```
function reversed-odd-numbers (low high) {
	return (pipe (range low high) (where \x [[x % 2] = 1]) reverse)
}
```

The interpreter is written in C89 and compiles cleanly under `gcc -ansi -pedantic -Wall -Wextra -Werror`, TinyCC (`tcc`), LCC-Win (`lc`), Borland C++ 5.5 (`bcc32`), Open Watcom (`owcc`), `clang`, and MSVC 2013.  It has been tested in Windows 7, OS X 10.9, and Debian Linux 7.7.  It should work on any other platform supporting ANSI C.  The interpreter can be embedded in client applications simply by including `sad-script.c` and `sad-script.h` file in the project.  No need to link a library or anything difficult.  Since the bindings are in C, they can be accessed easily from any language with a FFI.  For instance .NET can access it via P/Invoke.
