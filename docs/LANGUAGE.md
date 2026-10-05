# MicroCS language subset

MicroCS compiles a subset of C# with C#-compatible semantics wherever it supports a
feature. Programs that use only the supported subset print the same output as .NET
(`tools/verify_dotnet.sh` checks `t02`, `t05` and `t10` against .NET 8 byte-for-byte).

## Supported
* Top-level statements or `static void Main()`; namespaces/usings are accepted and ignored.
* Classes, structs (reference semantics), interfaces, abstract/virtual/override/`base`,
  static classes, constructors with chaining, properties (auto/init/expression-bodied),
  indexers, operator overloading, enums, erased generics, events and multicast delegates.
* Lambdas and closures, local functions, `params`, default arguments, overloads,
  object/collection initializers, string interpolation with alignment and format specifiers.
* `if/while/do/for/foreach/switch/break/continue/return`, `try/catch/finally`, exception
  filters (`catch (E e) when (...)`), custom exceptions, `using (...)` and `using var`.
* Patterns: `is T x`, `is null`, `is not null`, switch expressions with type, relational
  (`< 5`), `or`, `_` and `when` arms.
* **Phase 2**: `switch` *statements* accept the same patterns in `case` labels, plus `when`
  guards: `case int n when n < 0:`, `case > 5:`, `case Circle c:`, `case null:`.
* **Phase 2**: `ref`, `out` and `in` parameters and arguments: `out var x`, `out int x`,
  `out _`, `ref arr[i]`, `ref obj.Field`, `ref local`; natives `int.TryParse`,
  `double.TryParse`, `bool.TryParse`, `Dictionary<K,V>.TryGetValue`.

## `ref`/`out` semantics (implementation note)
Arguments are passed as a one-element *cell*; the callee works on a local copy that is
initialised from the cell (`ref`) or to the default (`out`) and is written back to the
cell on every `return`; the caller then stores the cell into the target variable.
This is *copy-in/copy-out*. It is indistinguishable from .NET by-reference passing except:
* if the callee throws, assignments it made to the parameter are not visible to the caller;
* if the same variable is passed twice (`Swap(ref a, ref a)`) or the callee also reads the
  variable through another path (closure, field) while it runs, it sees the old value;
* for `ref obj.F` / `ref a[i]` the target expression is evaluated again for the write-back;
* `TryGetValue` on a missing key stores `null` (MicroCS has no static `default(TValue)`).
`out var` declared in a *top-level* statement becomes a global, as in C#. `out`/`ref`
arguments are supported in expression statements, declarations, `if`/`while` conditions,
`return` and expression-bodied members/lambdas; other positions report a compile error.

## Not supported
Named arguments, tuples/deconstruction, `yield`, `async/await`, `goto`, multi-dimensional
arrays (`int[,]`), `ref` locals/returns, reflection, `unsafe`, `dynamic`, inheriting built-in
collections, user-defined generic constraints (parsed and ignored). Generics are erased,
strings are UTF-8 and byte-indexed, LINQ is eager and returns `List`, enums print as
numbers. With `MCS_INT64=0` (default on MCUs) `long` is 32-bit.

## Deliberate MicroCS behaviour (differs from .NET)
* `StackOverflowException` and `OutOfMemoryException`-style failures are catchable/reported
  instead of killing the process; integer constant overflow wraps instead of a compile error.
* `GroupBy` returns a `Dictionary<key, List<item>>`.
* Index-out-of-range messages include the index and length.
