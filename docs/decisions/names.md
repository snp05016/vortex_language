# Names and scopes

These records settle three rules about names: whether a declaration may reuse
a name that is already visible, whether a top-level function or struct can be
used above its declaration, and whether structs and functions share one set of
names. The [Programs and declarations](../specification/declarations.md#36-scopes)
chapter now states the rules; each record below explains why the rule was
chosen and what it changed.

## 2. No shadowing {#d2}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 4.

**Question.** May a declaration reuse a name already visible where it appears?
That is **shadowing**: the new declaration would hide the old one.

**Before this decision.** [Declarations](../specification/declarations.md#36-scopes)
section 3.6 left shadowing open and told code not to depend on it; the tour's
[Names and scopes](../language-tour/10-declarations.md#names-and-scopes)
section agreed. [Guide stage 4](../compiler/guide/stage-4-names-and-scopes.md#shadowing-an-outer-local)
listed three open cases: an inner local reusing an outer name, a local reusing
a parameter name, and a local used in its own initializer.

**Options.**

- Forbid all shadowing.
- Allow it in nested blocks only.
- Also allow a second `let` of a name in one block.

**Elsewhere.** [Zig never lets an identifier hide another][zig-shadow].
[Go allows a redeclaration in an inner block][go-scope], and
[Rust lets a later `let` shadow an earlier variable][rs-let].

**Decision.** A declaration must not have the same name as any other
declaration visible where it appears; doing so is a name error. `print` and
every top-level function and struct are visible everywhere, so no parameter,
local or loop variable may take their names. A function's parameters share one
scope with the locals directly in its body, and a loop variable with the locals
directly in the loop body. A local becomes visible only after its complete
declaration, initializer included. Declarations never visible at the same
point, such as the variables of two consecutive loops, may share a name.

**Why.** Each name then has one meaning at every point. The rule can be
relaxed later without breaking programs; allowing shadowing now and forbidding
it later would break them.

**Consequences.** `let total = total + 1;` never compiles: it either hides a
visible `total` or reads an unknown one.

```vortex
// statements: name error
let total = 5;
{
    let total = 6; // name error: total is already visible
}
```

Guide stage 4 reports the error at the new declaration, with an optional note
at the visible one. Pages changed: the declarations, statements, grammar and
diagnostics chapters, both glossaries, tour chapters 9 and 10, guide stages 4
and 7, the roadmap and the cheat sheet.

## 3. Declaration order {#d3}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N23,
with [record 6](program.md#d6). **Guide stage:** 4 and 7.

**Question.** May a program use a top-level function or struct above its
declaration, for example to let `main` call a function written below it?

**Before this decision.** [Declarations](../specification/declarations.md#31-program-structure)
section 3.1 said functions and structs "may appear in either order", which
only settles what the parser accepts. The tour's
[Function declarations](../language-tour/10-declarations.md#function-declarations)
section allowed a call "after its declaration has been recorded by the
compiler", which reads either way. [Guide stage 4](../compiler/guide/stage-4-names-and-scopes.md#order-of-top-level-declarations)
called the question open, while
[guide stage 7](../compiler/guide/stage-7-functions-and-control-flow.md#calls-and-the-call-stack)
allowed calls from anywhere in the file.

**Options.**

- One file-wide scope for top-level names.
- Declare before use, as in C, which needs prototypes (declarations without
  bodies) that v0.1 does not have.

**Elsewhere.** [A Rust item's scope is its whole module][rs-scopes],
[Go gives top-level names package-block scope][go-scope], and
[Zig container-level variables are order-independent][zig-container].
C++ instead makes [a name usable only after its point of
declaration][cpp-scope].

**Decision.** Every top-level function and struct name is visible throughout
the source file, including above its declaration. A function may call itself,
functions declared later, and functions that call it back (mutual recursion).
Any written type may name a struct declared later. Locals are unaffected
([record 2](#d2)).

**Why.** It matches the existing "either order" sentence, needs no
prototypes, and lets a file put `main` first. The cost: the compiler must know
every top-level name before it resolves a body.

**Consequences.** This program is valid and prints `true`:

```vortex
// program: valid
fn main() {
    print(is_even(4));
}

fn is_even(n: i32) -> bool {
    if n == 0 {
        return true;
    }
    return is_odd(n - 1);
}

fn is_odd(n: i32) -> bool {
    if n == 0 {
        return false;
    }
    return is_even(n - 1);
}
```

Guide stage 4 must accept a top-level name used above its declaration, and
guide stage 7 tests later, recursive and mutually recursive calls. A struct
that contains itself by value is still a type error
([record 45](operators.md#d45)). Pages changed: the declarations chapter, the
grammar, the declarations tour, guide stages 4 and 7, and the cheat sheet.

## 5. One namespace for top-level names {#d5}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 4.

**Question.** Do functions and structs share one **namespace** (a set of names
in which each name has one meaning), or may `struct Point` and `fn Point` both
exist?

**Before this decision.** The [grammar](../specification/grammar.md#struct-definitions-and-fields)
said struct names "must not conflict with another declaration in the same
scope" without naming the scope, and [Structs](../specification/structs.md#81-declaration)
section 8.1 put them in "program type scope", hinting at a separate scope for
types. [Guide stage 4](../compiler/guide/stage-4-names-and-scopes.md#a-struct-and-a-function-with-the-same-name)
left the question open. The tour's
[Additional primitive types](../language-tour/05-types-planned-for-later.md#additional-primitive-types)
section let a struct take a planned name such as `i64`, which
[record 29](lexical.md#d29) now reserves.

**Options.**

- One namespace for functions, structs and `print`.
- Separate namespaces for types and values.

**Elsewhere.** [Rust keeps types and values in separate namespaces][rs-ns],
and [C gives struct tags a name space of their own][c-ns].
In Go, [types and functions share one package block, where no identifier may
be declared twice][go-scope].

**Decision.** Top-level function and struct names form one namespace. Two
top-level declarations must not share a name, whatever their kinds, and none
may be named `print`, which lives in a scope around the program
([record 4](program.md#d4)); a clash is a name error. Fields stay outside it:
each struct's fields form their own namespace, reached through a value, so a
field may reuse a function, struct or local name.

**Why.** `Point` can start a struct expression, `Point { x: 1.0 }`, or a call,
`Point(1.0)`. With one namespace the name leads to the same declaration in
both, so resolving it never depends on the next token, and calling a struct
is reported as a kind error ([record 7](operators.md#d7)).

**Consequences.**

```vortex
// items: name error
struct Point {
    x: f32,
}

fn Point() {} // name error: Point is already declared
```

Guide stage 4 reports the clash at the later declaration, with an optional
note at the earlier one. By [record 2](#d2), a parameter or local cannot reuse
a struct name either. Pages changed: the declarations, grammar and structs
chapters, both glossaries, tour chapters 5 and 10, guide stage 4 and the cheat
sheet.

[zig-shadow]: https://ziglang.org/documentation/0.16.0/#Shadowing
[go-scope]: https://go.dev/ref/spec#Declarations_and_scope
[rs-let]: https://doc.rust-lang.org/reference/statements.html#let-statements
[rs-scopes]: https://doc.rust-lang.org/reference/names/scopes.html#item-scopes
[zig-container]: https://ziglang.org/documentation/0.16.0/#Container-Level-Variables
[cpp-scope]: https://en.cppreference.com/cpp/language/scope
[rs-ns]: https://doc.rust-lang.org/reference/names/namespaces.html#r-names.namespaces.kinds
[c-ns]: https://en.cppreference.com/c/language/name_space
