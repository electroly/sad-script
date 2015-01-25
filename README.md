SAD-Script
----------

<p align="center">
"S-Expressions and Dashes" or "Simple and Dumb"
</p>

A dynamically-typed, interpreted, garbage-collected language with 5 data types: int, double, bool, string, list.  There are no semicolons, and whitespace is insignificant.  Identifiers can contain anything except parentheses, braces, brackets, double quotes, and colons.  Identifiers must contain at least one character that isn't a digit and isn't a period, in order to distinguish identifiers from integer and double literals.

Control flow structures are JavaScript-like, with mandatory braces.  Function calls and expressions are Lisp-like, with parentheses around prefix-style function calls and brackets around infix-style function calls.  The two styles can be mixed and matched to taste.

```
function fizz-buzz (x) {
   for i from 1 to 100 {
      if [(zero? [i % 3]) and (zero? [i % 5])] {
         (print "FizzBuzz\n")
      } elseif (zero? [i % 3]) {
         (print "Fizz\n")
      } elseif (zero? [i % 5]) {
         (print "Buzz\n")
      } else {
         (print (to-string i))
      }
   }
}
```

There is a convenient syntax for lambda functions (with closures), as well as a pipelined query syntax similar to F#'s `|>` operator.  Programmers familiar with C#'s Enumerable extension methods and its conventional method chaining syntax will find this syntax familiar.  Unlike the C# extension methods, the calls in a query are executed eagerly rather than lazily.

```
function reversed-even-numbers (low high) {
	return query (range low high -> where :x (zero? [x % 2]) -> reverse)
}
```

The interpreter is written in C89 and compiles cleanly under `gcc -ansi -pedantic -Wall -Werror`, TinyCC (`tcc`), and MSVC 2013.  The interpreter can be embedded in client applications simply by including one `.c` and one `.h` file in the project.  No need to link a library or anything difficult.  Since the bindings are in C, they can be accessed easily from any language with a FFI.  For instance .NET can access it via P/Invoke.
