# 5. Types and language rules

<p class="page-intro">This stage gives every expression a type and checks the rules that grammar cannot see. It is the last point where a bad program can be stopped before the compiler starts producing code.</p>

By the end of [stage 4](stage-4-names-and-scopes.md), every name in the
program points at exactly one declaration. The program is grammatically
correct and its names make sense. That is still not enough. The line
`let ready: bool = 10;` parses perfectly and every name in it resolves, yet it
asks a true-or-false variable to hold the number ten. Somebody has to notice.

This stage is that somebody. It sits at the top of the left slope in the
[overview's mountain figure](index.md#the-shape-of-the-whole-thing), just below
the peak labelled "Checked program". Everything the compiler learns here is
handed down to [stage 6](stage-6-first-machine-code.md), and the back end is
allowed to trust it. If a rule is not checked here, nothing later is going to
check it for you.

It is also the stage where Vortex keeps its main promise. The roadmap puts it
in one sentence: invalid programs must be "rejected before code generation with
clear explanations".

## What this stage is for

The job has three parts, and the [architecture page](../architecture.md) gives
each one its own pass.

The **type checker** works out the type of every expression and checks that
operators, assignments, calls and returns are given values of the right types.
The **semantic checker** checks rules that depend on context rather than on
types alone: whether a variable may be changed, whether references are used
safely, whether `break` is inside a loop, and whether a function always
returns a value. The **constant evaluator** works out values that must be
known before the program runs: array sizes, which the type checker evaluates
as it resolves each array type, and the results of checked operations whose
operands are all constants.

The architecture page also says what each of these must not do. The type
checker must not "change source meaning for optimization". The semantic
checker must not "hide unsupported constructs". The constant evaluator must
not try to run arbitrary program behavior. Hold on to those three sentences.
They are the boundaries of this stage.

Every rule you check here is a **static rule**, which the
[glossary](../../specification/glossary.md) defines as a rule that can be
checked before the program runs. Its opposite is a **dynamic rule**, which
involves values known only while the program is running. Dividing by a number
the user typed in is a dynamic question. Dividing by the literal `0` is a
static one. Dynamic rules belong to [stage 9](stage-9-runtime-safety.md); this
stage handles everything that can be decided by reading the source.

## Words for this stage

type
: A label that says what kind of value something is, and therefore what can be
  done with it. `i32` values can be added; `bool` values can be combined with
  `&&`. Every Vortex expression has exactly one type, known before the program
  runs.

primitive type
: A type built into the language and named by a keyword, such as `i32`, `f32`
  or `bool`.

type checking
: Working out the type of every expression and checking that each operation,
  call, assignment and return uses types that fit together.

type inference
: Working out a type the programmer did not write. In Vortex this happens only
  for local variables, from their starting values.

initializer
: The expression after `=` in a `let` declaration. It gives the variable its
  first value, and its type is where inference gets its answer.

literal
: A value written directly in the source, such as `10`, `0.5`, `true` or
  `"Vortex"`.

default type
: The type a literal gets when nothing around it asks for a different one.
  Whole-number literals default to `i32`; decimal literals default to `f32`.

compatible
: Allowed to be used together under the type rules. Vortex v0.1 has no
  automatic conversions, so compatible means "the same type", once each
  literal has its type ([decision](../../decisions/numbers.md#d30)).

mutable
: Allowed to change after it is created. In Vortex a place is mutable only if
  its root variable was declared with `let mut` or is a `&mut` reference;
  parameters are immutable. The opposite is **immutable**.

place
: A name followed by any index and field suffixes, such as `points[i].x`: what
  an assignment targets and what `&` borrows. The name is its root.

reference
: A safe way to reach an existing value without copying it. A **shared
  reference** (`&T`) lets you read the value. A **mutable reference**
  (`&mut T`) also lets you change it.

addressable
: Naming a real storage place that a reference can point at: a variable, a
  struct field, or an array element. A temporary result such as `a + b` is not
  addressable.

control-flow path
: One possible route through a function's body, following one choice at every
  `if`, loop and `return`.

constant evaluation
: Working out the value of an expression while compiling, because the language
  needs that value before the program runs. Array sizes and checked
  operations on constants are the v0.1 cases.

dimension
: One size in an array's shape. `[f32; 4, 8]` has two dimensions, 4 and 8.

static rule
: A rule the compiler can check by reading the program, before it runs.

dynamic rule
: A rule that depends on values known only while the program runs.

ill-formed
: Breaking at least one required language rule. An ill-formed program must be
  rejected. A program that breaks none is **well-formed**.

negative test
: A test that feeds the compiler a program that should be rejected, and passes
  only if the compiler rejects it for the right reason at the right place.

## What a type is

A variable is a named box, and the type says what shape of thing fits in the
box. It also says what you are allowed to do with the contents. You can
multiply two `i32` values. You cannot take the remainder of two `f32` values
in v0.1, and you cannot ask whether a `String` is less than another `String`.
None of these facts is visible to the parser. They come from the types.

Vortex v0.1 has a short, fixed list of types, given in the
[types chapter](../../specification/types-and-values.md) and in Milestone 5 of
the roadmap.

| Type | What it holds | A literal that has it by default |
| --- | --- | --- |
| `void` | Nothing. Used only as "this function returns no value" | none |
| `bool` | `true` or `false` | `true` |
| `char` | One Unicode scalar value (one character, as Unicode numbers them) | `'A'` |
| `i32` | A signed 32-bit whole number | `10` |
| `u32` | An unsigned 32-bit whole number | none, needs context |
| `usize` | A size or index; implementation-defined width, 64 bits on every v0.1 target | none, needs context |
| `f32` | A 32-bit floating-point number | `0.5` |
| `f64` | A 64-bit floating-point number | none, needs context |
| `String` | UTF-8 text | `"Vortex"` |
| `[T; d1, d2, ...]` | A fixed-size array of `T` with the given dimensions | none |
| a struct name | A value with named fields, such as `Point` | none |
| `&T`, `&mut T` | A shared or mutable reference to a `T` | none |

Two entries need a note. `void` is not a value at all: it may appear only as a
function's return type, and a call to a `void` function only as a statement of
its own ([decision](../../decisions/operators.md#d44)). And array types carry
their dimensions inside them, so `[f32; 4]` and `[f32; 2, 2]` are different
types even though both hold four numbers.

A type is **equal** to another under simple rules, again from the
types chapter. Primitive types are equal when they are the same keyword. Two
struct types are equal when name resolution found the same declaration.
Reference types must match in both the referenced type and the `mut`. Array
types must match in element type, number of dimensions, and every dimension
after constant evaluation. That last clause matters: `[f32; 2 + 2]` and
`[f32; 4]` are the same type, but only once somebody has worked out that
`2 + 2` is `4`.

## How types travel up an expression

The type checker works on the syntax tree built in
[stage 3](stage-3-parser-and-tree.md). The types start at the leaves and move
upward. A name gets its type from its declaration, found in stage 4. A literal
gets its type from its surroundings: the other operand of its operator, or the
type its position expects, and otherwise a default that depends on its
spelling. Each operator then looks at the types of its children and decides
two things: whether it accepts them at all, and what type its own result has.

<figure class="vx-figure">
<svg viewBox="0 0 760 340" role="img" aria-labelledby="t5-flow-title t5-flow-desc">
<title id="t5-flow-title">Types moving up two expression trees</title>
<desc id="t5-flow-desc">On the left, the tree for let total equals count times 2 plus 1. The leaves count, 2 and 1 each have type i32, the multiplication and addition produce i32, and total is inferred as i32. On the right, the tree for let bad equals count times 2.5. The leaves have types i32 and f32, the multiplication has no rule for that pair, and the declaration never gets a type.</desc>
<text class="vx-mono" x="20" y="22">let total = count * 2 + 1;</text>
<text class="vx-mono" x="420" y="22">let bad = count * 2.5;</text>
<line class="vx-line" x1="395" y1="8" x2="395" y2="330"/>
<line class="vx-line" x1="190" y1="118" x2="190" y2="77"/>
<line class="vx-line" x1="110" y1="193" x2="180" y2="152"/>
<line class="vx-line" x1="270" y1="193" x2="200" y2="152"/>
<line class="vx-line" x1="60" y1="268" x2="100" y2="227"/>
<line class="vx-line" x1="160" y1="268" x2="120" y2="227"/>
<rect class="vx-box-strong" x="135" y="43" width="110" height="34" rx="4"/>
<text class="vx-mono" x="190" y="65" text-anchor="middle">let total</text>
<rect class="vx-box" x="165" y="118" width="50" height="34" rx="4"/>
<text class="vx-mono" x="190" y="140" text-anchor="middle">+</text>
<rect class="vx-box" x="85" y="193" width="50" height="34" rx="4"/>
<text class="vx-mono" x="110" y="215" text-anchor="middle">*</text>
<rect class="vx-box" x="245" y="193" width="50" height="34" rx="4"/>
<text class="vx-mono" x="270" y="215" text-anchor="middle">1</text>
<rect class="vx-box" x="20" y="268" width="80" height="34" rx="4"/>
<text class="vx-mono" x="60" y="290" text-anchor="middle">count</text>
<rect class="vx-box" x="135" y="268" width="50" height="34" rx="4"/>
<text class="vx-mono" x="160" y="290" text-anchor="middle">2</text>
<text class="vx-text-accent vx-seq" style="--vx-i: 0; --vx-n: 4" x="60" y="322" text-anchor="middle">i32</text>
<text class="vx-text-accent vx-seq" style="--vx-i: 0; --vx-n: 4" x="160" y="322" text-anchor="middle">i32</text>
<text class="vx-text-accent vx-seq" style="--vx-i: 0; --vx-n: 4" x="270" y="246" text-anchor="middle">i32</text>
<text class="vx-text-accent vx-seq" style="--vx-i: 1; --vx-n: 4" x="78" y="215" text-anchor="end">i32</text>
<text class="vx-text-accent vx-seq" style="--vx-i: 2; --vx-n: 4" x="222" y="140">i32</text>
<text class="vx-text-accent vx-seq" style="--vx-i: 3; --vx-n: 4" x="252" y="65">i32</text>
<text class="vx-text-muted" x="205" y="290">2 and 1 take i32 from count</text>
<line class="vx-line" x1="575" y1="118" x2="575" y2="77"/>
<line class="vx-line" x1="500" y1="193" x2="565" y2="152"/>
<line class="vx-line" x1="650" y1="193" x2="585" y2="152"/>
<rect class="vx-box-strong" x="520" y="43" width="110" height="34" rx="4"/>
<text class="vx-mono" x="575" y="65" text-anchor="middle">let bad</text>
<text class="vx-text-muted" x="638" y="65">gets no type</text>
<rect class="vx-box-bad vx-pulse" x="550" y="118" width="50" height="34" rx="4"/>
<text class="vx-mono" x="575" y="140" text-anchor="middle">*</text>
<rect class="vx-box" x="460" y="193" width="80" height="34" rx="4"/>
<text class="vx-mono" x="500" y="215" text-anchor="middle">count</text>
<rect class="vx-box" x="625" y="193" width="50" height="34" rx="4"/>
<text class="vx-mono" x="650" y="215" text-anchor="middle">2.5</text>
<text class="vx-text-accent" x="500" y="246" text-anchor="middle">i32</text>
<text class="vx-text-accent" x="650" y="246" text-anchor="middle">f32</text>
<text class="vx-text" x="575" y="285" text-anchor="middle">type error</text>
<text class="vx-text-muted" x="575" y="305" text-anchor="middle">no * rule takes an i32 and an f32</text>
</svg>
<figcaption>Figure 1. Types start at the leaves and move up. On the left, <code>count</code> is an <code>i32</code> parameter, the literals take <code>i32</code> from their typed neighbour <code>count</code>, each operator produces an <code>i32</code>, and <code>total</code> is inferred from the top. On the right, the multiplication receives an <code>i32</code> and an <code>f32</code>. Vortex has no automatic conversion between them, so checking stops at that node with a type error.</figcaption>
</figure>

Notice what the right-hand tree does not do. It does not quietly turn
`count` into a floating-point number to make the multiplication work. The
types chapter is blunt about this: "Vortex v0.1 has no implicit conversions
between types." If the programmer wants a floating-point result, they write the
conversion themselves as `f32(count)`. The parser already knows this is a
cast, because `f32` is a keyword ([decision](../../decisions/numbers.md#d1));
the checker decides whether this conversion is allowed
([cast rules](../../decisions/numbers.md#d27)).

### Literals and the types around them

A whole-number literal with nothing around it becomes an `i32`, and a decimal
literal becomes an `f32`. Vortex chose `f32` rather than `f64` as the decimal
default on purpose, because of its focus on numerical work
([types chapter, section 4.4](../../specification/types-and-values.md#44-floating-point-values)).

"Nothing around it" is the important phrase. The
[literal typing rule](../../specification/types-and-values.md#literal-typing)
looks at three things, in order
([decision](../../decisions/numbers.md#d31)):

1. The literal's **peer**: in `count + 1`, the other operand already has a
   type, so `1` takes the type of `count`.
2. The **expected type** of its position: in `let height: usize = 64;` the
   written type asks for a `usize`, and a parameter type, a return type or an
   assignment target asks in the same way. The request passes through
   operators, so in `let total: u32 = 2 * 3;` both literals are `u32`.
3. Otherwise, the default: `i32` or `f32`.

A literal never changes kind. In `let ratio: f32 = 1;` the whole number `1`
cannot become an `f32`, so it stays an `i32` and the declaration is a type
error. The rule is local: the checker never looks at later statements to type
a literal.

Once a literal has its type, its value must fit. In `let count: u32 = -1;` the
context asks for an unsigned number and the value is negative, so the program
has a type error. The minus sign is its own operator, not part of the literal,
but when it is written directly before a literal the check uses the negated
value ([decision](../../decisions/numbers.md#d32)). This matters at the edges
of a type's range, and it is a good place for a negative test and a positive
test side by side.

Once literal typing has chosen a numeric literal's final type, convert the
literal's spelling (the digits as written, which its token kept) into that
type in one step. A float literal converted to `f64` first and then narrowed
to `f32` is rounded twice, and can differ from the correctly rounded `f32`;
the specification asks for one rounding
([record 32](../../decisions/numbers.md#d32)). An integer spelling is
range-checked against its final type, so a literal too large for any integer
type is still reported as a type error at the literal.
[Record 53](../../decisions/documentation.md#d53) shows a literal
representation that keeps this possible.

### Inferring the type of a local variable

When a `let` has no written type, the variable takes the type of its
initializer. The types chapter gives the four basic cases:

```vortex
// statements: valid
let count = 10;       // i32
let weight = 0.5;     // f32
let ready = false;    // bool
let name = "Vortex"; // String
```

Inference does not make Vortex a dynamically typed language. Once `count` is an
`i32`, it is an `i32` for the rest of its life. Assigning `0.5` to it later is
a type error, not a change of type.

Inference has limits, and each one is a rule to check. A declaration with no
initializer, `let x;`, is already rejected by the grammar. An empty array
literal `[]` is not accepted in v0.1 because there is no element to infer the
element type from. A call to a `void` function cannot be an initializer,
because a `void` call may appear only as an expression statement. Parameters,
struct fields and function return types are never inferred. They must be
written.

## The checks, one kind at a time

Milestone 5 lists four places where types must be checked: operators,
assignments, function arguments and return values. The
[diagnostics chapter](../../specification/diagnostics.md) groups all of them
under one category, the **type error**.

**Operators.** Each operator accepts certain operand types.
`+ - * /` need two compatible numbers. `%` needs integers. The bitwise
operators and shifts need integers. `&&`, `||` and `!` need `bool`. `==` and
`!=` need two values of the same `bool`, `char`, integer or floating-point
type, and `<`, `<=`, `>` and `>=` two integers or two floating-point values of
the same type. Strings, arrays and structs cannot be compared at all
([decision](../../decisions/operators.md#d35)). Every comparison produces a
`bool`. `a & b == c` groups as `a & (b == c)`, a type error because `&` needs
integers; the message should suggest `(a & b) == c`
([decision](../../decisions/operators.md#d37)). The
[expressions chapter](../../specification/expressions.md) has the full table.

**Conditions.** The condition of an `if` or a `while` must be a `bool`. Vortex
does not treat numbers or strings as true or false, so `if 1 { ... }` is a
type error.

**Loop ranges.** The two endpoints of a `for` loop's range must have the same
integer type after literal typing, and the loop variable takes that type. In
`for i in 0..count` with `count: usize`, the literal `0` becomes a `usize` and
so does `i`; a `u32` start with a `usize` end is a type error
([decision 13](../../decisions/statements.md#d13)).

**Ranges.** A range may appear only as the iterable of a `for` loop, and a
`for` loop's iterable must be a range: `let span = 0..10;` and
`for v in values { }` are both type errors. The parser accepted both, so this
stage is the first that can reject them
([decision 36](../../decisions/statements.md#d36)).

**Assignments and initializers.** The value on the right must be compatible
with the storage on the left, or with the written type in a `let`. Compound
assignment such as `total += value;` must also pass the check for the
underlying `+`.

**Calls.** The number of arguments must match the number of parameters, and
each argument must be compatible with its parameter's type. The built-in
`print` is the exception: it takes one or more arguments, each a `bool`,
`char`, integer, float or `String` value, and a literal argument keeps its
default type
([Programs and declarations 3.9](../../specification/declarations.md#39-built-in-functions),
[decision](../../decisions/program.md#d4)).

**Struct expressions and kinds.** A struct expression must initialize every
field of its struct exactly once, in any order; a missing, unknown or repeated
field is a type error. A name used as the wrong kind of declaration is a type
error too: a struct or variable used as a callee, a function or variable used
as a type or as a struct-expression name, and a function or struct used as a
value, such as `let f = add;`. Stage 4 recorded each declaration's kind for
this check ([decision](../../decisions/operators.md#d7)).

**Struct declarations.** A struct must not contain itself by value, directly or
through other structs or array element types, because its size would be
infinite. `struct Node { next: Node }` is a type error, and so is the pair
`struct A { b: B }` and `struct B { items: [A; 2] }`. Check this before any
layout is computed ([decision](../../decisions/operators.md#d45)).

**Returns.** `return expression;` must give a value compatible with the
function's declared result type. A `void` function must not return a value,
and a non-`void` function must not use a bare `return;`. All three mistakes
are type errors ([decision 8](../../decisions/statements.md#d8)).

```vortex
// program: type error
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}

fn main() {
    let ready: bool = 10;          // type error: initializer is not a bool
    let total = add(1, true);      // type error: second argument is not an i32
    let name = "Vortex" % 2;       // type error: % needs integers
}
```

Each of those three lines should produce its own diagnostic, pointing at its
own span. The first error must not hide the second.

## Mutability

In Vortex, a place cannot change unless its root was declared with `let mut`
or is a `&mut` reference, and parameters never change. This is a
**semantic error** rather than a type error, because the types are fine. The
problem is permission.

```vortex
// statements: semantic error
let limit = 10;
limit = 20;          // semantic error: limit is immutable

let mut count = 0;
count += 1;          // fine
```

The check applies to every way of changing storage: plain assignment, compound
assignment, assigning to an array element or a struct field, and taking a
mutable reference. To check a target, find its root name and look at that
declaration ([decision](../../decisions/references.md#d23)). The diagnostics
chapter asks for the error to point at the assignment target, with an optional
note at the declaration that lacks `mut`. That note is what turns a correct
message into a helpful one.

A `for` loop variable is immutable too: `i = 0;` inside
`for i in 0..4 { ... }` is a semantic error.

`mut` belongs to the storage, not to the type. The
[references chapter](../../specification/references.md) says so directly:
mutability "does not change the value's base type". A `let mut x = 5;` is
still an `i32`.

## Shared and mutable references

A reference lets a function work on a value without copying it, which matters
a great deal for large arrays. Vortex v0.1 has two kinds, and the rules that
connect them are short.

A shared reference, `&value`, allows reading. You cannot change anything
through it. Any number of shared references to the same value may exist at
once.

Reading and writing through a reference need no operator. When a name has
type `&T` or `&mut T`, your checker gives the name, used as a value, the type
`T`, applies index and field suffixes to what it refers to, and treats a place
rooted at a `&mut` name as mutable. v0.1 has no `*`
([decision](../../decisions/references.md#d40)).

A mutable reference, `&mut value`, allows changing the value. It needs mutable
storage underneath, so `&mut` of a variable declared without `mut` is an
error. And from the point where a mutable reference is created until the end
of its block, or for the whole call when it is an argument, the variable may
be used only through that reference.

```vortex
// program: semantic error
fn main() {
    let value = 10;
    let changed = &mut value;      // semantic error: value is not mut
}
```

```vortex
// program: semantic error
fn main() {
    let mut values = [1.0, 2.0];
    let writer = &mut values;
    let reader = &values; // semantic error: writer borrows values to the end of the block
    writer[0] = 5.0;
    print(reader[0]);
}
```

A call borrows too: `add_into(&mut values, values)` is rejected because
`values` appears in another argument of a call that borrows it as `&mut`
([decision](../../decisions/references.md#d25)).

The operand of `&` or `&mut` must also be addressable. `&(a + b)` asks for a
reference to a temporary result that has nowhere to live, and v0.1 rejects it.

The rules are small enough to check exactly
([decision](../../decisions/references.md#d41)). A reference may appear only
as a parameter type or as a `let` binding without `mut`; a `let` borrow lasts
until the end of its block and an argument borrow for the whole call;
conflicts are per whole variable. Nothing here needs lifetime inference: walk
each block, record the borrows made in it, and drop them at its closing brace.
Rejecting a program these rules accept is as much a bug as accepting one they
reject.

## Rules about the shape of control flow

Two of the Milestone 5 rules are about where control can go rather than about
values.

### Break and continue outside loops

`break;` and `continue;` are only meaningful inside a loop. The parser accepts
them anywhere a statement may appear, because the grammar allows it, and the
[statements chapter](../../specification/statements.md) leaves the rejection to
semantic analysis. The checker needs to know, at every `break` and `continue`,
whether some enclosing statement is a loop. A plain block `{ ... }` or an `if`
does not count.

```vortex
// program: semantic error
fn main() {
    if true {
        break;       // semantic error: no enclosing loop
    }
}
```

### Every path must return a value

A function declared with a result type, such as `-> i32`, promises its caller
a value. The statements chapter turns that into a rule: the body of a
non-`void` function must end in a terminating statement.

A control-flow path is one possible route through the function body. Every
`if` splits the route in two. A `return` ends it. The idea behind the rule is
that no route may reach the closing brace of the function without passing a
`return`.

```vortex
// items: semantic error
fn sign(value: i32) -> i32 {
    if value > 0 {
        return 1;
    } else if value < 0 {
        return -1;
    }
}
```

Each `return` here is correct, and the function is still wrong. Figure 2 shows
why.

<figure class="vx-figure">
<svg viewBox="0 0 760 270" role="img" aria-labelledby="t5-paths-title t5-paths-desc">
<title id="t5-paths-title">The three paths through the sign function</title>
<desc id="t5-paths-desc">The first test, value greater than zero, leads on true to return 1. On false it leads to a second test, value less than zero, which on true leads to return minus 1. On false the second test leads to the end of the body, marked as an error because no value is returned.</desc>
<rect class="vx-box" x="200" y="20" width="200" height="36" rx="4"/>
<text class="vx-mono" x="300" y="43" text-anchor="middle">value &gt; 0 ?</text>
<rect class="vx-box" x="200" y="100" width="200" height="36" rx="4"/>
<text class="vx-mono" x="300" y="123" text-anchor="middle">value &lt; 0 ?</text>
<rect class="vx-box-strong" x="500" y="100" width="200" height="36" rx="4"/>
<text class="vx-mono" x="600" y="123" text-anchor="middle">return 1;</text>
<rect class="vx-box-strong" x="500" y="180" width="200" height="36" rx="4"/>
<text class="vx-mono" x="600" y="203" text-anchor="middle">return -1;</text>
<rect class="vx-box-bad vx-pulse" x="200" y="180" width="200" height="36" rx="4"/>
<text class="vx-text" x="300" y="203" text-anchor="middle">end of body</text>
<path class="vx-line" d="M400 38 L600 38 L600 100"/>
<polygon class="vx-arrowhead" points="595,92 600,100 605,92"/>
<text class="vx-text-muted" x="480" y="30">true</text>
<line class="vx-line" x1="300" y1="56" x2="300" y2="100"/>
<polygon class="vx-arrowhead" points="295,92 300,100 305,92"/>
<text class="vx-text-muted" x="308" y="82">false</text>
<path class="vx-line" d="M400 118 L450 118 L450 198 L500 198"/>
<polygon class="vx-arrowhead" points="492,193 500,198 492,203"/>
<text class="vx-text-muted" x="408" y="110">true</text>
<line class="vx-line" x1="300" y1="136" x2="300" y2="180"/>
<polygon class="vx-arrowhead" points="295,172 300,180 305,172"/>
<text class="vx-text-muted" x="308" y="162">false</text>
<text class="vx-text-muted" x="300" y="240" text-anchor="middle">value == 0 arrives here with no value to return</text>
</svg>
<figcaption>Figure 2. The paths through <code>sign</code>. Two of the three routes end at a <code>return</code>. The third, taken when <code>value</code> is zero, runs off the end of the body. The compiler must reject the function and point at that missing path, even though no single line in it is wrong.</figcaption>
</figure>

The fix is to add a final `return 0;` after the `if`, or to turn the chain
into an `if ... else if ... else` in which every branch returns. Both versions
should appear in your tests as accepted programs, next to the rejected one.

[Decision 9](../../decisions/statements.md#d9) makes the rule structural. A
`return` terminates. A block terminates when its last statement does. An `if`
terminates only when it has an `else` and both branches terminate. Nothing
else does: not `while`, not `for`, not even `while true`. The checker never
looks at the value of a condition, so a function whose body ends in a
`while true` loop that returns from inside is rejected, and the fix is a
`return` after the loop. Code after a `return` is valid, but it still counts
as the last statement of its block.

## Array dimensions and constant evaluation

An array type carries its sizes: `[f32; 4, 8]` is four rows of eight. The
grammar lets each size be a full expression, so `[f32; 2 + 2, 8]` is legal
syntax. The [arrays chapter](../../specification/arrays.md#72-dimension-rules)
then requires every dimension to be an integer constant expression: integer
literals, each optionally preceded by `-`, combined with `+`, `-`, `*`, `/`,
`%` and parentheses, with no names and no calls
([decision](../../decisions/arrays.md#d11)). Its value, computed with checked
`usize` arithmetic, must be at least 1, and the whole array must fit the size
limit the compiler supports.

Working out that `2 + 2` is `4` is constant evaluation. It is a small, careful
calculator inside the compiler that runs only on the expressions the language
needs early. The architecture page draws its boundary: it must not "evaluate
names, calls, or other runtime behavior".

It has a second job. When every operand that decides a checked operation is an
integer constant expression, as in `10 / 0`, `u32(-1)` or `2147483647 + 1`,
the evaluator must perform the check now, and a failure is a
constant-evaluation error ([record 39](../../decisions/diagnostics.md#d39)). A
name or a call is never constant, so `value / divisor` waits for stage 9 even
when `divisor` holds zero.

A dimension can fail in five ways, each with a dimension-specific message: it
uses a name or a call, it is not an integer, its arithmetic goes negative or
overflows, the array is too large, or it is zero. The diagnostics chapter puts
all of them in one category, the **constant-evaluation error**.

```vortex
// program: constant-evaluation error
fn make(size: usize) {
    let a: [f32; size] = [0.0; size];     // runtime-dependent: size is a parameter
}

fn main() {
    let n = 4;
    let b: [f32; 2.5] = [0.0; 2.5];       // non-integer
    let c: [f32; 2 - 5] = [0.0; 2 - 5];   // negative
    let d: [f32; 65536, 65536, 65536, 65536] = [0.0; 65536, 65536, 65536, 65536];
    // overflowing: on a 64-bit target the total size does not fit
    let e: [f32; n] = [0.0; n];           // uses a name, even though n is immutable
    let z: [f32; 4 - 4] = [0.0; 4 - 4];   // zero extent
}
```

The runtime-dependent case needs a clear message, because the programmer
usually meant something reasonable. A parameter does not make an array
resizable in v0.1. An immutable local does not help either: v0.1 has no named
constants. The diagnostics chapter asks for a message "explaining that a fixed
dimension depends on a runtime parameter", which is a much better hint than a
generic "expected constant".

Dimensions also appear in repeat expressions such as `[0.0; 2 + 2]`, and the
same rules apply there. Once evaluated, the dimensions become part of the type,
which is how `[f32; 2 + 2]` and `[f32; 4]` come out equal.

### Indexes are checked here too

Milestone 5 also says "check array dimensions and indexes". The arrays chapter
gives the static part of that. Each index must have an integer type (`i32`,
`u32` or `usize`), and the indices of one access may differ in type. The
number of indexes must equal the rank, so `matrix[row]` on a two-dimensional
array is a type error in v0.1, which has no partial indexing. An array of
arrays takes one suffix per level, as in `grid[1][0]`
([decision](../../decisions/arrays.md#d47)). And when an index is an integer
constant expression outside its extent, the compiler must reject the program
with a constant-evaluation error
([decision](../../decisions/arrays.md#d12)):

```vortex
// statements: constant-evaluation error
let values = [10, 20, 30];
let selected = values[3];      // constant-evaluation error: index 3 is past the end
```

Any other index, even a variable whose value you can see, is a dynamic rule,
and the compiler must accept the program
([record 39](../../decisions/diagnostics.md#d39)). It is left for
[stage 9](stage-9-runtime-safety.md), which adds the bounds check to the
generated code.

### Zero-length arrays are rejected {#the-zero-length-array-is-an-open-decision}

What about `[f32; 0]`? Every extent must be at least 1, so it is a
constant-evaluation error. So is a zero reached by calculation, such as
`[f32; 4 - 4]`, a zero in any position, as in `[f32; 0, 4]`, and a repeat
array such as `[0.0; 0]` ([decision](../../decisions/arrays.md#d10)). Because
no array is empty, indexing never meets an array without a valid index. The
check belongs in this stage, not in the grammar: the parser accepts `[f32; 0]`
exactly as it accepts `[f32; 4]`.

## The gate

Put the pieces together and this stage behaves like a gate. A resolved program
comes in from stage 4. Either it passes every check and moves on as a checked
program, or it stops here with one or more diagnostics.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-labelledby="t5-gate-title t5-gate-desc">
<title id="t5-gate-title">The rule-check gate</title>
<desc id="t5-gate-desc">A resolved program flows into a gate labelled types and rules, which contains type checking, semantic checks and constant evaluation. Well-formed programs flow on to the right as a checked program ready for lowering. Ill-formed programs stop below the gate in one of three boxes: a type error, a semantic error, or a constant-evaluation error, each with an example.</desc>
<rect class="vx-box" x="20" y="40" width="150" height="56" rx="4"/>
<text class="vx-text" x="95" y="64" text-anchor="middle">Resolved program</text>
<text class="vx-text-muted" x="95" y="84" text-anchor="middle">from stage 4</text>
<rect class="vx-box-accent" x="300" y="20" width="160" height="96" rx="4"/>
<text class="vx-text" x="380" y="48" text-anchor="middle">Types and rules</text>
<text class="vx-text-muted" x="380" y="70" text-anchor="middle">type checking</text>
<text class="vx-text-muted" x="380" y="87" text-anchor="middle">semantic checks</text>
<text class="vx-text-muted" x="380" y="104" text-anchor="middle">constant evaluation</text>
<rect class="vx-box-strong" x="590" y="40" width="150" height="56" rx="4"/>
<text class="vx-text" x="665" y="64" text-anchor="middle">Checked program</text>
<text class="vx-text-muted" x="665" y="84" text-anchor="middle">on to lowering</text>
<line class="vx-flow" x1="170" y1="68" x2="292" y2="68"/>
<polygon class="vx-arrowhead" points="292,63 300,68 292,73"/>
<line class="vx-flow" x1="460" y1="68" x2="582" y2="68"/>
<polygon class="vx-arrowhead" points="582,63 590,68 582,73"/>
<path class="vx-line" d="M380 116 L380 150 M150 150 L610 150 M150 150 L150 182 M380 150 L380 182 M610 150 L610 182"/>
<polygon class="vx-arrowhead" points="145,182 150,190 155,182"/>
<polygon class="vx-arrowhead" points="375,182 380,190 385,182"/>
<polygon class="vx-arrowhead" points="605,182 610,190 615,182"/>
<text class="vx-text-muted" x="392" y="142">ill-formed programs stop here</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box-bad" x="40" y="190" width="220" height="80" rx="4"/>
<text class="vx-text" x="150" y="214" text-anchor="middle">Type error</text>
<text class="vx-mono" x="150" y="238" text-anchor="middle">let ready: bool = 10;</text>
<text class="vx-text-muted" x="150" y="258" text-anchor="middle">initializer is not a bool</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect class="vx-box-bad" x="270" y="190" width="220" height="80" rx="4"/>
<text class="vx-text" x="380" y="214" text-anchor="middle">Semantic error</text>
<text class="vx-mono" x="380" y="238" text-anchor="middle">limit = 20;</text>
<text class="vx-text-muted" x="380" y="258" text-anchor="middle">limit was not declared mut</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect class="vx-box-bad" x="500" y="190" width="220" height="80" rx="4"/>
<text class="vx-text" x="610" y="214" text-anchor="middle">Constant-evaluation error</text>
<text class="vx-mono" x="610" y="238" text-anchor="middle">[f32; size]</text>
<text class="vx-text-muted" x="610" y="258" text-anchor="middle">size known only at run time</text>
</g>
</svg>
<figcaption>Figure 3. This stage as a gate. Only well-formed programs flow on to lowering. Ill-formed ones stop here, each with a diagnostic in one of the three categories that the diagnostics chapter assigns to this stage.</figcaption>
</figure>

The gate has a direction. The architecture page says lowering must not "accept
programs rejected by earlier phases". So there is no such thing as a program
that fails a check here but is compiled anyway with a warning. If the gate
says no, stage 6 never sees the program.

## A negative test for every rule

Milestone 5 ends its list with "add a negative test for every rule". The
diagnostics chapter says what each of those tests should contain: one minimal
rejected program, the expected category, a check that the error points at the
right piece of source, and "a nearby accepted program that proves the test is
not rejecting a broader valid form".

That last part is easy to skip and costly to skip. A checker that rejects
every `&mut` would pass every negative test about mutable references. Only
the accepted neighbour shows that it is rejecting the right thing. The table
pairs each rule with the smallest program that breaks it and the smallest
change that fixes it.

| Rule | Rejected | Category | Accepted neighbour |
| --- | --- | --- | --- |
| Initializer matches written type | `let ready: bool = 10;` | type | `let ready: bool = true;` |
| Unsigned value is not negative | `let count: u32 = -1;` | type | `let count: u32 = 1;` |
| Operator accepts its operands | `let bad = count * 2.5;` | type | `let good = f32(count) * 2.5;` |
| `%` needs integers | `let r = 7.0 % 2.0;` | type | `let r = 7 % 2;` |
| Only scalars compare | `a == b` on two arrays | type | `a[0] == b[0]` |
| `&` binds looser than `==` | `flags & mask == 0` | type | `(flags & mask) == 0` |
| Condition is a `bool` | `if 1 { }` | type | `if 1 == 1 { }` |
| Range endpoints share one type | `for i in first..last` with `first: u32`, `last: usize` | type | both endpoints `u32` |
| Range only as a `for` iterable | `let span = 0..10;` | type | `for i in 0..10 { }` |
| `for` iterates over a range | `for v in values { }` | type | `for i in 0..3 { }` |
| Argument count and types | `add(1, true)` | type | `add(1, 2)` |
| `print` has arguments of printable types | `print();` | type | `print(1);` |
| Every field exactly once | `Point { x: 1.0 }` | type | `Point { x: 1.0, y: 2.0 }` |
| A struct is not a function | `Point(1.0, 2.0)` | type | `Point { x: 1.0, y: 2.0 }` |
| A function is not a value | `let f = add;` | type | `let f = add(1, 2);` |
| `void` only as a return type | `fn f(x: void) {}` | type | `fn f(x: i32) {}` |
| `void` call only as a statement | `let done = log();` | type | `log();` |
| No struct contains itself | `struct Node { next: Node }` | type | `struct Node { value: i32 }` |
| Return value matches result type | `return true;` in `-> i32` | type | `return 1;` |
| No bare return in non-`void` | `return;` in `-> i32` | type | `return 0;` |
| No value returned from `void` | `return 1;` in `main` | type | `return;` |
| Every path returns | `sign` from Figure 2 | semantic | `sign` with a final `return 0;` |
| Assign only mutable storage | `limit = 20;` | semantic | `let mut limit = 10;` first |
| Parameters are immutable | `value = 0;` where `value` is a parameter | semantic | `let mut result = value;` then `result = 0;` |
| Loop variable is immutable | `i = 0;` inside `for i in 0..4` | semantic | `let mut j = i;` then `j = 0;` |
| `&mut` needs mutable storage | `&mut value` on a plain `let` | semantic | `&mut value` on a `let mut` |
| No other use while mutably borrowed | the `writer` and `reader` example | semantic | put `writer` and its use in an inner block, then create `reader` after it |
| No other use of a variable passed as `&mut` in the same call | `add_into(&mut values, values)` | semantic | `let copy = values;` then `add_into(&mut values, copy)` |
| References only in parameters and immutable `let` | `fn first(values: &[i32; 2]) -> &i32` | type | return `i32` instead |
| `&` needs an addressable operand | `&(a + b)` | semantic | `&a` |
| `break` only in a loop | `break;` in a plain `if` | semantic | `break;` inside `while` |
| `continue` only in a loop | `continue;` at top of `main` | semantic | `continue;` inside `for` |
| Dimension uses no names or calls | `[f32; size]` | constant | `[f32; 4]` |
| Dimension is an integer | `[f32; 2.5]` | constant | `[f32; 2]` |
| Dimension is not negative | `[f32; 2 - 5]` | constant | `[f32; 5 - 2]` |
| Dimension is at least 1 | `[f32; 4 - 4]` | constant | `[f32; 4]` |
| Array size does not overflow | four dimensions of 65536 | constant | `[f32; 256, 256]` |
| Index has an integer type | `values[1.0]` | type | `values[1]` |
| Index count matches rank | `matrix[row]` on a 2-D array | type | `matrix[row, column]` |
| Constant index is in bounds | `values[3]` on three elements | constant | `values[2]` |
| Constant divisor is not zero | `let q = 10 / 0;` | constant | `let q = 10 / 2;` |
| Constant cast fits | `let n = u32(-1);` | constant | `let n = u32(1);` |
| Constant arithmetic fits | `let big = 2147483647 + 1;` | constant | `let big = 2147483646 + 1;` |

Every row has one required category. The return rows follow
[decision 8](../../decisions/statements.md#d8): every `return` that disagrees
with the function's return type is a type error, and a missing return path is
a semantic error. An out-of-bounds constant index is always a
constant-evaluation error, and the same access through a variable always
compiles ([record 39](../../decisions/diagnostics.md#d39)). The reference rows
are fixed by the references chapter.

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- A type for every expression: the back end needs to know what each value is.
- All of the v0.1 types, including arrays with evaluated shapes, structs and
  both kinds of reference: Milestone 5 lists them.
- Local type inference from the initializer, and literal typing: peer
  operand, then expected type, then the `i32` and `f32` defaults, with a range
  check on every literal
  ([types chapter](../../specification/types-and-values.md#literal-typing)).
- Checks on operators, conditions, assignments, call arguments and return
  values: these are the four places Milestone 5 names, plus conditions.
- The static cast rules: a cast operand must have a numeric type, and a cast
  whose operand is a constant expression is evaluated here, where a failure is
  a constant-evaluation error ([decision](../../decisions/numbers.md#d27)).
  The runtime checks for other casts belong to
  [stage 9](stage-9-runtime-safety.md).
- The mutability check on every form of assignment and on `&mut`: bindings are
  immutable by default.
- The reference rules exactly as the references chapter states them: where
  references may appear, lexical borrows, and whole-variable conflicts.
- The terminating-body check for non-`void` functions.
- Rejection of `break` and `continue` outside loops.
- Evaluation of every array dimension, with separate diagnostics for a name or
  call, a non-integer, a negative or overflowing result, an array that is too
  large, and a zero extent.
- Rejection of zero extents, written or computed, as constant-evaluation
  errors.
- Constant evaluation of every checked operation whose deciding operands are
  integer constant expressions, with a constant-evaluation error when the
  check fails.
- A negative test and an accepted neighbour for every rule.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Runtime checks for division by zero, overflow and out-of-bounds indexes whose
  values are unknown: these are dynamic rules for
  [stage 9](stage-9-runtime-safety.md).
- An ownership or lifetime system beyond lexical borrows: v0.1 needs none,
  because references cannot be returned or stored.
- Constant folding as an optimization: the roadmap lists it after v0.1.
  Constant evaluation of dimensions is a language rule, not an optimization.
- Named compile-time constants, generics, slices, vectors and runtime-sized
  arrays: none of them are v0.1 features.
- Warnings, such as for a discarded value: the spec allows them but does not
  require them, and a warning never changes whether a program passes the gate
  ([Diagnostics 10.1](../../specification/diagnostics.md#101-required-diagnostic-data)).
- Final diagnostic wording: the diagnostics chapter treats exact prose as an
  implementation detail.

</div>
</div>

## What you do not need yet

The right-hand column above is the list. The common thread is that this stage
checks rules the specification has already written down. Questions that
earlier drafts of this guide left open, such as zero-length arrays,
reachability and the reference model, are now settled in the
[decision records](../../decisions/index.md), so the job is to check the
written rule and test it. It is not the place to design the rest of the
language.

## How you know it is finished

The roadmap's completion condition for
[Milestone 5](../../roadmap.md#milestone-5-types-mutability-and-control-flow-checks)
is: "invalid programs are rejected before code generation with clear
explanations." In practice that means:

- every rule on the milestone list has a negative test and an accepted
  neighbour, and all of them pass;
- every diagnostic names its category and points at the right span;
- a zero extent, written or computed, is rejected with a constant-evaluation
  error;
- all tests from milestones 0 to 4 still pass.

The roadmap also gives two dimension examples that must behave as their
labels say ([decision 28](../../decisions/documentation.md#d28)):

```vortex
// statements: valid
let matrix: [f32; 2 + 2, 8] = [0.0; 2 + 2, 8]; // valid: constant expressions
```

```vortex
// items: constant-evaluation error
fn invalid(rows: usize) {
    let matrix: [f32; rows, 8] = [0.0; rows, 8];
    // invalid in v0.1: rows is only known at runtime
}
```

## Traps

**Deciding meaning in the parser.** It is tempting to reject
`[f32; 0]` or `break;` outside a loop while parsing, because the parser sees
them first. The roadmap and the statements chapter both say no. The parser
checks form; this stage checks meaning. Mixing them makes both harder to test.

**Treating constant evaluation as an optimization.** Constant folding, which
the roadmap postpones until after v0.1, is about making code faster and may be
skipped. Evaluating array dimensions is about deciding what the type even is,
and cannot be skipped. They may share a calculator, but they are different
jobs with different rules. The same holds for checked operations with
constant operands: evaluating them decides whether the program is accepted.

**Comparing array types before their dimensions are evaluated.** If you
compare `[f32; 2 + 2]` and `[f32; 4]` by looking at the expressions, they
differ. By the spec, they are equal. Evaluate each dimension when you resolve
the array type, so type equality always compares numbers
([decision](../../decisions/arrays.md#d52)).

**Adding helpful conversions.** Letting `i32 * f32` quietly become an `f32`
feels friendly. The spec forbids it, and every such rule you add is one the
specification never agreed to.

**Checking a literal without its minus sign.** The smallest `i32`,
`-2147483648`, is a unary minus in front of the literal `2147483648`, which on
its own is one too large for `i32`. The types chapter checks the negated value
when the minus is written directly before the literal
([decision](../../decisions/numbers.md#d32)). Checking the bare literal first
rejects a valid program.

**Looking at the last line instead of the last statement.** A `return x;` on
the last line of a function can sit inside a loop, or inside an `if` without
`else`, and then the body does not terminate. The rule asks whether the body's
last statement terminates and, for an `if`, whether both branches do.

**Letting one error cause a flood.** Once an expression has failed its type
check, everything that uses it has no meaningful type either. Reporting a
second and third error about the same mistake buries the real one. The
diagnostics chapter's goal of "useful independent errors" applies here as much
as in the parser. The suggested default,
[implementation choice I11](../../decisions/implementation.md#i11), gives a
name whose declaration failed to parse an unknown type that accepts every use,
so nothing further is reported about it.

**Approximating the borrow rules.** The reference rules are exact, so both
directions are bugs. A checker that ends a borrow at its last use accepts
programs Vortex rejects, and so does one that tracks array elements
separately; a checker that rejects more than the rules say breaks valid
programs.

## How others teach this stage

**Kaleidoscope** has almost nothing to say here, and that is informative. Its
only data type is a 64-bit floating-point number, so "the language doesn't
require type declarations" and there is no type checker to write.[^kal1] That
choice keeps the tutorial short. Vortex cannot make it, because its types
carry the array shapes that the whole matrix-multiplication goal depends on.

***Crafting Interpreters*** builds Lox, a dynamically typed language, so it has
no static type checker either. Its overview chapter still places type checking
where Vortex puts it, as part of static analysis for languages that have
static types.[^ci-map] The closest match to this stage is the chapter
"Resolving and Binding". It adds a separate pass that walks the tree once,
without running it, and uses that pass to report errors such as returning
from top-level code.[^ci-resolve] That is the same shape as the Vortex check
for `break` outside a loop: a context rule, found by walking the tree before
anything runs.

**Nora Sandler's *Writing a C Compiler*** takes the opposite order to Vortex.
Part I builds a compiler for C with only `int`, including loops and functions,
and the other types arrive one chapter at a time in Part II, "Types Beyond
Int".[^sandler-book] That order gets code running early. Vortex checks the
whole type system before generating any code, because rejecting bad programs
early is the point of the language.

**The Rust compiler** is a useful picture of where this can grow. Its
developer guide describes type checking on a tree form close to the source,
and a later representation, MIR, that "is used for borrow checking and other
important dataflow-based checks".[^rustc-overview] Rust's borrow checker is far
larger than anything v0.1 needs. The Vortex reference rules are a small,
deliberately cautious subset of the same idea.

For more depth on type checking in general, the classic textbooks by Aho and
colleagues[^dragon] and by Appel[^appel] both treat it at length.

[^kal1]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 1, "Kaleidoscope Introduction and the Lexer". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl01.html>
[^ci-map]: Robert Nystrom, *Crafting Interpreters*, chapter "A Map of the Territory". <https://craftinginterpreters.com/a-map-of-the-territory.html>
[^ci-resolve]: Robert Nystrom, *Crafting Interpreters*, chapter "Resolving and Binding". <https://craftinginterpreters.com/resolving-and-binding.html>
[^sandler-book]: Nora Sandler, *Writing a C Compiler: Build a Real Programming Language from Scratch*, No Starch Press, 2024. <https://nostarch.com/writing-c-compiler>
[^rustc-overview]: Rust Compiler Development Guide, "Overview of the compiler". <https://rustc-dev-guide.rust-lang.org/overview.html>
[^dragon]: Alfred V. Aho, Monica S. Lam, Ravi Sethi and Jeffrey D. Ullman, *Compilers: Principles, Techniques, and Tools*, 2nd edition, Addison-Wesley, 2006.
[^appel]: Andrew W. Appel, *Modern Compiler Implementation in ML*, Cambridge University Press, 1998.
