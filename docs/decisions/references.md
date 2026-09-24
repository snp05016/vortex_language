# References and mutability

These records settle how Vortex v0.1 handles change and indirection: which
storage a program may modify, what copying a value means, how a program reads
and writes through a reference, and where references may appear and for how
long. The [References and mutability](../specification/references.md) chapter
and the chapters it links to now state the rules; each record below explains
why the rule was chosen and what it changed.

## 23. Parameters and mutable places {#d23}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5.

**Question.** May a function assign to its parameters, and which assignment
targets are mutable?

**Before this decision.** [Declarations](../specification/declarations.md)
3.2 never said whether a parameter can change; the
[tour](../language-tour/10-declarations.md) only noted that there is no `mut`
parameter syntax. [References and mutability](../specification/references.md)
9.3 and [Statements](../specification/statements.md) 6.4 required "mutable
storage" without defining it for fields, elements, or storage behind a `&mut`
reference.

**Options.**

- Immutable parameters, as in Swift and Zig.
- Parameters as mutable local copies, as in C.
- Immutable unless declared `mut`, as in Rust.

**Elsewhere.** Swift parameters are
[constants; changing one is a compile-time error][sw-inout]. Zig parameters are
[immutable][zig-params], which lets Zig pass an aggregate by copy or by
reference. Rust calls assignment targets [place expressions][rs-place].

**Decision.** A **place** is a name followed by zero or more index and field
suffixes, such as `grid[i, j]` or `points[i].x`; the name is its **root**. A
place is mutable only when its root is a variable declared with `let mut`, or
a parameter or `let` binding of type `&mut T`. Every other place is immutable,
including every other parameter and every `for` loop variable
([record 13](statements.md#d13)). Assigning to an immutable place, with `=` or
a compound operator, is a semantic error, and so is taking `&mut` of one.

**Why.** One definition covers every assignment form and `&mut`, and it is
checked from declarations alone. Immutable parameters also let a compiler pass
a large array by address unnoticed ([record 25](#d25)).

**Consequences.** To change a parameter's value, copy it into a `let mut`
local first:

```vortex
// items: semantic error
fn clamp(value: i32) -> i32 {
    if value < 0 {
        value = 0;       // semantic error: value is a parameter
    }
    return value;
}
```

Stage 5 checks every assignment, compound assignment and `&mut`, pointing at
the target. Pages changed: References 9.2, 9.3 and 9.5; Declarations 3.2;
Statements 6.4; Structs 8.5; Arrays 7.7; the grammar; Diagnostics 10.2; the
glossaries, tour and cheat sheet; guide stage 5.

## 25. Value semantics {#d25}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5, 8.

**Question.** Do assignment, argument passing and return copy a whole array or
struct, and may one call receive a variable both by value and as `&mut`?

**Before this decision.** [Structs](../specification/structs.md) 8.6 called
structs value types but left "the exact copy, move, and parameter-passing ABI"
to the implementation, and [Arrays](../specification/arrays.md) only implied
copying. Guide [stage 8](../compiler/guide/stage-8-data-in-memory.md) let an
array parameter be passed by copy or by hidden address, which a call such as
`add_into(&mut values, values)` could tell apart.

**Options.**

- Copy every value, with no moves.
- Moves that make the source unusable (Rust, for types that are not `Copy`).
- Arrays passed by address (C array parameters).

**Elsewhere.** In Go, [arrays are values][go-effective-arrays]: assignment
copies every element, and a function receives a copy. Swift structures are
[value types][sw-value], and Swift
[forbids conflicting access to in-out parameters][sw-conflict].

**Decision.** Initializing a variable, assigning, passing an argument to a
parameter whose type is not a reference type, and returning a value each copy
the whole value. There are no moves: the source stays usable and unchanged.
Within one call, a variable borrowed by a `&mut` argument must not appear in
any other argument of that call; doing so is a semantic error.

**Why.** Copying is what a reader predicts. The call rule
means a callee never sees a by-value argument change while it runs, so a
compiler may pass a large array by address unnoticed. The rule is
conservative; relaxing it later breaks no program.

**Consequences.**

```vortex
// program: semantic error
fn add_into(target: &mut [i32; 2], source: [i32; 2]) {
    target[0] += source[0];
}

fn main() {
    let mut values = [1, 2];
    add_into(&mut values, values);   // semantic error: values is also borrowed as &mut
}
```

Passing a copy made first (`let copy = values;`) is valid. Stage 5 checks the
call rule, which [record 41](#d41) generalizes; stage 8 implements copies and
documents the passing method. Pages changed: Structs 8.6; Arrays 7.7;
References 9.6 and 9.8; the tour's declarations chapter; guide stages 5 and 8.

## 40. Reading and writing through references {#d40}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N10.
**Guide stage:** 5, 8.

**Question.** How does a program read the value behind a `&i32`, or write
through a `&mut i32`?

**Before this decision.** [References and mutability](../specification/references.md)
9.2 to 9.4 said a shared reference "permits reading" but not how, and no
dereference operator exists. Examples such as `values[index] *= factor` on a
`&mut [f32; 4]` parameter relied on an unstated rule.

**Options.**

- (a) Implicit dereference and no rebinding, like C++ references.
- (b) An explicit `*` operator, as in Rust and C.
- (c) References only as parameters, like Swift's in-out parameters.

**Elsewhere.** A C++ reference is
[an alias to an already-existing object][cpp-ref]. Rust uses an explicit
[`*` operator][rs-deref] and
[dereferences automatically][rs-autoderef] for field access. Swift marks an
argument for an [in-out parameter][sw-inout] with `&` at the call site.

**Decision.** A name whose type is `&T` or `&mut T` denotes the storage the
reference refers to, its **referent**. Used as a value, the name reads the
referent and has type `T`. Index and field suffixes apply to the referent. As
the root of an assignment target, the name writes the referent, which requires
`&mut` ([record 23](#d23)). `&name` and `&mut name` create a new reference to
the same referent. Vortex v0.1 has no `*` operator, and a reference can never
be changed to refer to other storage.

**Why.** Existing examples stay valid and the grammar gains no operator.
Borrows stay visible, because a reference argument is always written `&v` or
`&mut v` ([record 41](#d41)).

**Consequences.**

```vortex
// program: valid
fn bump(count: &mut i32) {
    count += 1;
}

fn main() {
    let mut clicks = 0;
    bump(&mut clicks);
    print(clicks);       // prints 1
}
```

Stage 5 gives a reference name used as a value its referenced type, stage 8
emits a load or a store, and the parser needs nothing new. Pages changed: References 9.2 and
9.4; Expressions; Types and values 4.9; the grammar; the glossaries, tour and
cheat sheet; parser design; guide stage 5.

## 41. Where references may appear, and how long a borrow lasts {#d41}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N20.
**Guide stage:** 5, 8.

**Question.** Where may a reference be kept, and how long does a borrow last?

**Before this decision.** [References and mutability](../specification/references.md)
9.7 and 9.8 told implementations to "reject cases they cannot prove safe", so
validity depended on the compiler, against
[Conformance](../specification/conformance.md) 1.3. Guide
[stage 5](../compiler/guide/stage-5-types-and-rules.md) accepted a program
valid only if a borrow ends at its last use.

**Options.**

- Lexical borrows, whole-variable conflicts, references only in parameters and
  immutable `let` bindings.
- Borrows computed from control flow (Rust).

**Elsewhere.** Rust's [NLL RFC][rs-nll] replaced lifetimes tied to lexical
scopes with lifetimes based on the control-flow graph. C's
[`restrict`][c-restrict] is an unchecked promise of no aliasing.

**Decision.**

- A reference type may appear only as a parameter type or as the type of a
  `let` binding without `mut`, and a reference expression only as a whole call
  argument or `let` initializer. Anywhere else, such as a return type, field,
  array element or referenced type, either is a type error.
- A `let` borrow lasts to the end of its block; an argument borrow covers the
  whole call, other arguments included.
- While `&mut v` is live, `v` must be used only through it; while `&v` is
  live, `v` must not be assigned or borrowed with `&mut`.
- Conflicts are per whole variable (any place rooted at `v` counts) and are
  semantic errors.

**Why.** The rules need only block structure, not lifetime inference, so
every compiler accepts the same programs. References cannot dangle, and code
generation's no-overlap assumption becomes a checked guarantee.

**Consequences.**

```vortex
// statements: semantic error
let mut values = [1, 2];
let writer = &mut values;
let reader = &values;   // semantic error: writer borrows values to the end of the block
```

An inner block around `writer` fixes it. Stage 5 checks both rules; stage 8
relies on them. Pages changed: References 9.5 to 9.9; the other spec chapters
that mention references; the glossaries, tour, cheat sheet, parser design,
roadmap and philosophy page; guide stages 5, 8 and 9.

[sw-inout]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/functions#In-Out-Parameters
[zig-params]: https://ziglang.org/documentation/0.16.0/#Pass-by-value-Parameters
[rs-place]: https://doc.rust-lang.org/reference/expressions.html#place-expressions-and-value-expressions
[go-effective-arrays]: https://go.dev/doc/effective_go#arrays
[sw-value]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/classesandstructures#Structures-and-Enumerations-Are-Value-Types
[sw-conflict]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/memorysafety#Conflicting-Access-to-In-Out-Parameters
[cpp-ref]: https://en.cppreference.com/cpp/language/reference
[rs-deref]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#the-dereference-operator
[rs-autoderef]: https://doc.rust-lang.org/reference/expressions/field-expr.html#automatic-dereferencing
[rs-nll]: https://rust-lang.github.io/rfcs/2094-nll.html
[c-restrict]: https://en.cppreference.com/c/language/restrict
