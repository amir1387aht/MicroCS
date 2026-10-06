# Language reference — the MicroCS subset of C#

MicroCS compiles a subset of C# with **C#-compatible semantics wherever it supports a
feature**. Programs that stay inside the subset print the same output as .NET:
`tools/verify_dotnet.sh` checks `t02`, `t05`, `t10`, `t11`, `t13` and `examples/tour.cs`
against .NET 8 byte-for-byte.

**Legend:** ✅ supported · ⚠️ supported with a documented difference · ❌ not supported

## Program structure
| Feature | | Notes |
|---|:---:|---|
| Top-level statements, `static void Main()` | ✅ | `args` via the host |
| `namespace`, `using` directives | ✅ | accepted and ignored (one global namespace) |
| Classes, static classes, nested types | ✅ | |
| `struct` | ⚠️ | **reference semantics** (assignment does not copy) |
| Interfaces, `abstract`/`virtual`/`override`/`sealed`/`base` | ✅ | default interface methods ❌ |
| Constructors, chaining (`: this(..)`, `: base(..)`), static constructors | ✅ | |
| Properties (auto, `init`, expression-bodied, get/set bodies), indexers | ✅ | |
| Operator overloading | ✅ | user-defined conversion operators (`implicit`/`explicit operator`) ❌ |
| Enums (incl. explicit values, flags arithmetic) | ⚠️ | print as numbers |
| Generics (classes, methods, constraints) | ⚠️ | **erased**: constraints parsed and ignored, `default(T)` is `null` |
| Events, delegates, multicast, `Action`/`Func` | ✅ | |
| Records, primary constructors, `required`, file-scoped types | ❌ | |

## Statements
| Feature | | Notes |
|---|:---:|---|
| `if` `while` `do` `for` `foreach` `break` `continue` `return` | ✅ | `foreach` over arrays, lists, strings, dictionaries, sets, `GetEnumerator()` |
| `switch` statements: constants, type patterns, relational, `and`/`or`, `when` | ✅ | `case int n when n < 0:`, `case > 5 and < 8:`, `case null:` |
| `try` / `catch` / `finally`, exception filters `when` | ✅ | |
| `throw`, rethrow `throw;`, `throw` expressions | ✅ | |
| `using (...)`, `using var` | ✅ | calls `Dispose()` |
| Local functions (incl. `static`), recursion | ✅ | |
| `goto`, labeled statements, `yield`, `async`/`await`, `lock`, `fixed`, `unsafe` | ❌ | |
| Declarations as embedded statements (`if (c) int x = 1;`) | ❌ | compile error, as in C# (CS1023) |

## Expressions
| Feature | | Notes |
|---|:---:|---|
| Arithmetic, bitwise, shifts, `>>>`, compound assignment, `++`/`--` | ✅ | `int` is 32-bit; `long` is 64-bit only with `MCS_INT64=1` |
| `checked(...)` / `unchecked(...)` | ⚠️ | accepted; arithmetic always wraps |
| `?:`, `??`, `??=`, `?.`, `?[]`, null-forgiving `!` | ✅ | |
| `is T x`, `is null`, `is not null`, `as`, casts | ✅ | |
| `is` with relational / `and` / `or` / `not` patterns | ✅ | `t is < 0 or > 100`, `c is >= 'a' and <= 'z'` |
| Switch expressions with all pattern forms above, `_` | ✅ | |
| Lambdas, closures (by-reference capture), method groups | ✅ | |
| String interpolation with alignment and format specifiers | ✅ | `$"{x,8:F2}"`, `X4`, `D3`, `N`, `P`, `E` |
| Object & collection initializers, target-typed `new()` | ✅ | |
| Arrays, jagged arrays `int[][]`, array initializers | ✅ | multi-dimensional `int[,]` ❌ |
| `nameof`, `typeof`, `sizeof(builtin)`, `default`, `default(T)` | ✅ | |
| Nullable value types `int?`: `.HasValue`, `.Value`, `.GetValueOrDefault()` | ✅ | `null`-able at run time (values are boxed uniformly) |
| **Tuples** `(1, "a")`, named `(x: 1, y: 2)`, tuple types `(int x, int y)` | ✅ | up to 7 elements; structural `==`, usable as dictionary keys |
| Tuple element name projection `var t = (a, b); t.a` | ✅ | C# 7.1 rules (duplicates / reserved names dropped) |
| **Deconstruction** `var (a, b) = t;`, `(a, b) = (b, a);`, `_` discards | ✅ | tuples, `KeyValuePair`, `foreach (var (k, v) in dict)` |
| **Index from end** `x[^1]`, **ranges** `x[1..^1]`, `x[..3]`, `x[2..]` | ✅ | strings, arrays; on `List<T>` as a MicroCS extension |
| `ref` / `out` / `in` parameters and arguments, `out var`, `out _` | ⚠️ | copy-in/copy-out, see below |
| Named arguments `F(x: 1)` | ❌ | |
| `ref` locals and returns, `stackalloc`, pointers, `dynamic`, reflection | ❌ | |
| Raw string literals `"""..."""`, UTF-8 literals `"x"u8` | ❌ | verbatim `@"..."` ✅ |

## `ref` / `out` semantics (implementation note)
Arguments are passed as a one-element *cell*; the callee works on a local copy that is
initialised from the cell (`ref`) or to the default (`out`) and is written back to the
cell on every `return`; the caller then stores the cell into the target variable.
This *copy-in/copy-out* is indistinguishable from .NET by-reference passing except:
* if the callee throws, assignments it made to the parameter are not visible to the caller;
* if the same variable is passed twice (`Swap(ref a, ref a)`) or the callee also reads the
  variable through another path (closure, field) while it runs, it sees the old value;
* for `ref obj.F` / `ref a[i]` the target expression is evaluated again for the write-back;
* `TryGetValue` / `TryPop` / `TryDequeue` on a miss store `null` (no static `default(TValue)`).

`out var` declared in a *top-level* statement becomes a global, as in C#. `out`/`ref`
arguments are supported in expression statements, declarations, `if`/`while` conditions,
`return` and expression-bodied members/lambdas; other positions report a compile error.

## Tuples (implementation note)
Tuples are immutable-by-convention instances of a hidden `ValueTuple` class per
(arity, names) pair; element names are aliases of `Item1..Item7`. Because types are erased,
names travel with the *value*: the compiler re-labels a tuple when it is stored into a
variable, field, property, parameter or `out` parameter, used in an object initializer, or
returned from a method/property whose declared type has element names. Member names are
found by a pre-scan of the source, so declaration order does not matter; the lookup is by
member *name*, so two unrelated members with the same name and different tuple element
names in one program may get the wrong labels (positional `ItemN` access is always correct). Two tuples are equal when their elements are equal, regardless of names.
`(a, b) = (b, a)` with a tuple literal on the right compiles to temporaries — no allocation.

## Types and values
* `int`, `uint`, `short`, `byte`, `sbyte`, `char`, `bool`, `double`, `float`, `string`,
  `object`; `long`/`ulong` are 64-bit with `MCS_INT64=1` (default on the host) and 32-bit
  otherwise. `float` and `double` share one representation (`MCS_FLOAT_DOUBLE`).
* Strings are UTF-8 and **byte-indexed** (`s.Length` counts bytes; `foreach` yields code points).
* LINQ is **eager** and returns `List<T>`; `GroupBy` returns `Dictionary<key, List<item>>`.
* Value-type semantics for `struct` are not implemented — structs are references.

## Deliberate MicroCS behaviour (differs from .NET)
* `StackOverflowException` is catchable; out-of-memory ends the run with `MCS_ERR_MEMORY`
  instead of killing the process.
* Integer constant overflow wraps instead of a compile error; `checked` does not trap.
* `GroupBy` returns a `Dictionary<key, List<item>>`.
* Index-out-of-range messages include the index and length.
* Enums print as their numeric value.
* A top-level variable may share its name with a pattern variable declared later
  (C# reports CS0128).
