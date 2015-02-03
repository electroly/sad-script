SAD-Script
----------

<p align="center">
"S-Expressions and Dashes" or "Simple and Dumb"
</p>

A **dynamically-typed, interpreted, garbage-collected language** with 8 data types: `Int`, `Double`, `Bool`, `String`, `List`, `Function`, `Error`, and `Type`.  There are no semicolons, and whitespace is insignificant.  Identifiers can contain anything except parentheses, braces, brackets, double quotes, colons, backslashes, the lambda symbol (`λ`), and a small handful of reserved keywords.  Identifiers must contain at least one character that isn't a digit and isn't a period, in order to distinguish identifiers from integer and double literals.

Control flow structures are JavaScript-like, with mandatory braces.  Function calls and expressions are Lisp-like, with parentheses around **prefix-style function calls** and brackets around **infix-style function calls**.  The two styles can be mixed and matched to taste.

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

**Partial function application**, **closures**, and **variable argument lists** are supported.  Combining these features allows the standard library to include a `pipe` function which pipes the output of one function call into the next.  This allows a function chaining style that will be familiar to C# programmers who use that language's Enumerable extension methods.  Also like C#, the list manipulation functions operate on **lazy streams** rather than producing a complete list after each call.

	function reversed-odd-numbers (low high) {
		return (pipe [low ... high] (where \x [[x % 2] = 1]) reverse)
	}

**Pattern matching** is supported through the `switch` and `match` keywords.  A list of values (or, usually, the arguments to a method) can be matched by type or by value.

	function fibonacci (n) = match {
	   case 0: 0
	   case 1: 1
	   case Int: [(fibonacci [n - 1]) + (fibonacci [n - 2])]
	   default: (die! "Expected an integer!")
	}

**Multiple return values** from a function are supported in a simple way: a special assignment syntax allows a list to be broken out into separate variables for each element.  The function can return a normal list.

	function returns-list () = (list 1 2 3)
	var (a b c) = (returns-list)  // a=1, b=2, c=3

The interpreter is written in **C89** and compiles cleanly with `-ansi -pedantic -Wall -Wextra -Werror` compiler flags.  

Supported compilers: 
- GCC
- Clang
- Microsoft Visual C++ 2013
- TinyCC (`tcc`)
- LCC-Win (`lc`)
- Borland C++ 5.5 (`bcc32`)
- Open Watcom (`owcc`).  

It has been tested in Windows 7, OS X 10.9, and Debian Linux 7.7.  Both 32-bit and 64-bit builds are supported.  It should work on any other platform supporting ANSI C.  The interpreter can be embedded in client applications simply by including `sad-script.c` and `sad-script.h` file in the project.  No need to link a library or anything difficult.  Since the bindings are in C, they can be accessed easily from any language with a FFI.  For instance .NET can access it via P/Invoke.
