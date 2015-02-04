Sad-Script
----------

<p align="center">
<a href="http://electroly.com/sad/try#b8edde764ff206b6bb56aa0cb92b4277"><b>Try it now in your browser!</b></a>
</p>

<p align="center">
<a href="http://electroly.com/sad/sad-script.zip"><b>Download the latest build (with source)!</b></a>
</p>

A **dynamically-typed, interpreted, garbage-collected language** with 8 data types: `Int`, `Double`, `Bool`, `String`, `List`, `Function`, `Error`, and `Type`.  There are no semicolons, and whitespace is insignificant.  Identifiers can contain anything except parentheses, braces, brackets, double quotes, colons, backslashes, the lambda symbol (`λ`), and a small handful of reserved keywords.  Identifiers must contain at least one character that isn't a digit and isn't a period, in order to distinguish identifiers from integer and double literals.  Variables must be declared with `var` before they are used.  A default value must always be provided.

Control flow structures are JavaScript-like, with mandatory braces.  Function calls and expressions are Lisp-like, with parentheses around **prefix-style function calls** and brackets around **infix-style function calls**.  The two styles are equivalent and can be mixed and matched to taste.

```
var a = 5
var x = [6 + a]   // calling the "+" function with infix notation
var y = (+ 6 a)   // calling the "+" function with prefix notation
```

**Pattern matching** is supported through the `switch` and `match` keywords.  A list of values (or, implicitly, the arguments to the containing function) can be matched by type or by value.

```
for i from 1 to 100 {
  var multiple-of-3 = (zero? [i % 3])
  var multiple-of-5 = (zero? [i % 5])
  
  (println match multiple-of-3 multiple-of-5 {
  	case true true: "FizzBuzz"
  	case true false: "Fizz"
  	case false true: "Buzz"
  	default: i
  })
}
```

**Partial function application**, **closures**, and **variable argument lists** are supported.  Combining these features allows the standard library to include a `pipe` function which pipes the output of one function call into the next.  This allows a function chaining style that will be familiar to C# programmers who use that language's `Enumerable` extension methods.  Also like C#, the list manipulation functions operate on **lazy sequences** (with support for infinite sequences) rather than producing a complete list after each call.

```
function fibonacci (n) = match {
   case 0: 0
   case 1: 1
   default: [(fibonacci [n - 1]) + (fibonacci [n - 2])]
}

function even? (x) = (zero? [x % 2])

var first-twenty-even-terms = (pipe [0 ...] (map fibonacci) (filter even?) (take 20))
```

**Multiple return values** from a function are supported in a simple way: a special assignment syntax allows a list to be broken out into separate variables for each element.  The function can return a normal list.

```
function returns-list () = (list 1 2 3)
var (a b c) = (returns-list)  // a=1, b=2, c=3
```

The interpreter is written in **ANSI C** (C89) and compiles cleanly with `-ansi -pedantic -Wall -Wextra -Werror` compiler flags.  A wide variety of compilers are supported: `gcc`, `clang`, Microsoft Visual C++ 2013, Emscripten (`emcc`), TinyCC (`tcc`), LCC-Win (`lc`), Borland C++ 5.5 (`bcc32`), Open Watcom (`owcc`).  Both 32-bit and 64-bit builds are supported.

It has been tested in Windows 7, OS X 10.9, and Debian Linux 7.7.  The interpreter can be embedded in client applications simply by including `sad-script.c` and `sad-script.h` file in the client project or Makefile.  No need to build or link a separate library.  Since the bindings are in C, they can be accessed easily from any language with a FFI.  For instance .NET can access it via P/Invoke.