# 9. References and mutability

References let a function access an existing value without exposing raw pointer
arithmetic. Vortex v0.1 distinguishes shared references from mutable references.

## 9.1 Reference types

```ebnf
reference_type ::=
    "&", [ "mut" ], type ;
```

<a class="vx-rule" id="refs.types.shared-mut" href="#refs.types.shared-mut">refs.types.shared-mut</a> `&T` is a shared reference to `T`. `&mut T` is a mutable reference to `T`.

```vortex
// fragment
&Point
&[f32; 4]
&mut [f32; 4]
```

<a class="vx-rule" id="refs.types.mutability-in-type" href="#refs.types.mutability-in-type">refs.types.mutability-in-type</a> Reference mutability is part of the type.

## 9.2 Reference expressions

<a class="vx-rule" id="refs.expr.prefix-forms" href="#refs.expr.prefix-forms">refs.expr.prefix-forms</a> Prefix `&` creates a shared reference. Prefix `&mut` creates a mutable
reference. A reference expression produces a reference to the storage its
operand names, called the **referent**.

```vortex
// fragment
let value = 10;
let shared = &value;

let mut values = [1.0, 2.0, 3.0, 4.0];
scale(&mut values, 2.0);
```

<a class="vx-rule" id="refs.expr.operand-place" href="#refs.expr.operand-place">refs.expr.operand-place</a> The operand must be a place (9.3), optionally inside parentheses. Any other
operand, such as a literal, a call result or an arithmetic result, is a
semantic error ([record 23](../decisions/references.md#d23)).

## 9.3 Local mutability

<a class="vx-rule" id="refs.mut.default-immutable" href="#refs.mut.default-immutable">refs.mut.default-immutable</a> Bindings are immutable by default:

```vortex
// statements: valid
let limit = 10;
```

<a class="vx-rule" id="refs.mut.mut-enables-assignment" href="#refs.mut.mut-enables-assignment">refs.mut.mut-enables-assignment</a> `mut` permits assignment to the bound storage:

```vortex
// statements: valid
let mut count = 0;
count += 1;
```

<a class="vx-rule" id="refs.mut.type-unaffected" href="#refs.mut.type-unaffected">refs.mut.type-unaffected</a> Mutability does not change the value's base type. It controls whether storage
can be modified.

<a class="vx-rule" id="refs.mut.place-defined" href="#refs.mut.place-defined">refs.mut.place-defined</a> A **place** is an expression that names storage: a name followed by zero or
more index and field suffixes, such as `total`, `grid[row, column]` or
`points[index].x`. The name is the place's **root**. Assignment targets and the
operands of `&` and `&mut` are places.

<a class="vx-rule" id="refs.mut.mutable-place" href="#refs.mut.mutable-place">refs.mut.mutable-place</a> A place is **mutable** only when its root is:

- <a class="vx-rule" id="refs.mut.root-let-mut" href="#refs.mut.root-let-mut">refs.mut.root-let-mut</a> a local variable declared with `let mut`; or
- <a class="vx-rule" id="refs.mut.root-ref-param" href="#refs.mut.root-ref-param">refs.mut.root-ref-param</a> a parameter or `let` binding whose type is `&mut T`. Such a place denotes
  storage reached through the reference (9.4).

<a class="vx-rule" id="refs.mut.immutable-places" href="#refs.mut.immutable-places">refs.mut.immutable-places</a> Every other place is immutable. Parameters are immutable bindings, and v0.1 has
no `mut` parameter syntax. A `let` variable declared without `mut`, a `for`
loop variable, and a place rooted at a name of type `&T` are also immutable.

<a class="vx-rule" id="refs.mut.immutable-error" href="#refs.mut.immutable-error">refs.mut.immutable-error</a> Assigning to an immutable place, with `=` or any compound assignment operator,
is a semantic error. Taking `&mut` of an immutable place is a semantic error.
The diagnostic points at the assignment target or the `&mut` operand and may
add a note at the root's declaration.

Decision record: [Parameters and mutable places](../decisions/references.md#d23).

## 9.4 Shared references

<a class="vx-rule" id="refs.shared.permissions" href="#refs.shared.permissions">refs.shared.permissions</a> A shared reference permits reading its referent. A place rooted at a name of
type `&T` is immutable (9.3): assigning to it, or taking `&mut` of it, is a
semantic error. Any number of shared references to the same variable may exist
at the same time (9.8).

### Using a reference

<a class="vx-rule" id="refs.use.denotes-referent" href="#refs.use.denotes-referent">refs.use.denotes-referent</a> A name whose type is `&T` or `&mut T` denotes its referent, not the reference
itself:

- <a class="vx-rule" id="refs.use.read-value" href="#refs.use.read-value">refs.use.read-value</a> used as a value, the name reads the referent, and the expression has type
  `T`;
- <a class="vx-rule" id="refs.use.index-field-suffixes" href="#refs.use.index-field-suffixes">refs.use.index-field-suffixes</a> index and field suffixes apply to the referent: if `values` has type
  `&mut [f32; 4]`, then `values[index]` is an element of the referenced array;
- <a class="vx-rule" id="refs.use.write-target" href="#refs.use.write-target">refs.use.write-target</a> as the root of an assignment target, the name writes the referent, which
  requires a `&mut` reference (9.3);
- <a class="vx-rule" id="refs.use.no-ref-to-ref" href="#refs.use.no-ref-to-ref">refs.use.no-ref-to-ref</a> `&name` and `&mut name` create a new reference to the same referent, so no
  expression produces a reference to a reference.

<a class="vx-rule" id="refs.use.no-deref-operator" href="#refs.use.no-deref-operator">refs.use.no-deref-operator</a> Vortex v0.1 has no dereference operator `*`. A reference binding cannot be
changed to refer to other storage: assigning to the name writes the referent.

```vortex
// program: valid
fn bump(count: &mut i32) {
    count += 1;
}

fn main() {
    let mut clicks = 0;
    bump(&mut clicks);
    print(clicks); // prints 1
}
```

Decision record: [Reading and writing through references](../decisions/references.md#d40).

## 9.5 Mutable references

<a class="vx-rule" id="refs.mutref.operand-and-write" href="#refs.mutref.operand-and-write">refs.mutref.operand-and-write</a> The operand of `&mut` must be a mutable place (9.3). A mutable reference
permits writing its referent (9.4).

<a class="vx-rule" id="refs.mutref.exclusive-live" href="#refs.mutref.exclusive-live">refs.mutref.exclusive-live</a> While a mutable borrow of a variable is live, the variable may be used only
through that reference (9.8). This prevents overlapping writes and read-write
conflicts.

```vortex
// statements: semantic error
let value = 10;
let invalid = &mut value;
// semantic error: value is immutable
```

## 9.6 Function parameters

References are especially useful for fixed arrays and structs:

```vortex
// items: valid
fn scale(values: &mut [f32; 4], factor: f32) {
    for index in 0..4 {
        values[index] *= factor;
    }
}
```

<a class="vx-rule" id="refs.params.argument-match" href="#refs.params.argument-match">refs.params.argument-match</a> The argument must satisfy the parameter's mutability and type requirements. A
parameter whose type is not a reference type receives a copy of its argument
([Structs](structs.md) 8.6, [Arrays and shapes](arrays.md) 7.7).

<a class="vx-rule" id="refs.params.reference-arg-form" href="#refs.params.reference-arg-form">refs.params.reference-arg-form</a> Because a name of reference type used as a value reads its referent (9.4), the
only expressions of reference type are `&place` and `&mut place`; an argument
for a reference parameter is therefore always written with `&` or `&mut`, and
its type must equal the parameter type. The borrow it makes lasts for the call
(9.8). A function cannot keep a reference after it returns, because v0.1 has no
reference return types, fields or array elements (9.7).

## 9.7 Lifetimes

<a class="vx-rule" id="refs.lifetimes.no-outlive" href="#refs.lifetimes.no-outlive">refs.lifetimes.no-outlive</a> A reference must never outlive the storage it refers to. Vortex v0.1 has no
lifetime syntax and needs no lifetime inference, because a reference type may
appear only:

- <a class="vx-rule" id="refs.lifetimes.param-position" href="#refs.lifetimes.param-position">refs.lifetimes.param-position</a> as the type of a function parameter; or
- <a class="vx-rule" id="refs.lifetimes.let-position" href="#refs.lifetimes.let-position">refs.lifetimes.let-position</a> as the type of a `let` binding declared without `mut`.

<a class="vx-rule" id="refs.lifetimes.type-error-positions" href="#refs.lifetimes.type-error-positions">refs.lifetimes.type-error-positions</a> A reference type anywhere else is a type error: as a function's return type, a
struct field type, an array element type, the referenced type of another
reference, or the type of a `let mut` binding.

<a class="vx-rule" id="refs.lifetimes.expr-positions" href="#refs.lifetimes.expr-positions">refs.lifetimes.expr-positions</a> A reference expression (`&place` or `&mut place`) may appear only as a complete
call argument or as the complete initializer of a `let` declared without
`mut`, in either case optionally inside parentheses. A reference expression
anywhere else is a type error.

<a class="vx-rule" id="refs.lifetimes.referent-outlives" href="#refs.lifetimes.referent-outlives">refs.lifetimes.referent-outlives</a> Under these rules a `let` reference ends with its block, and its referent is a
parameter, a variable declared earlier in the same or an enclosing block, or
storage that a caller owns for the whole call. Every referent therefore
outlives every reference to it. See [record 41](../decisions/references.md#d41).

## 9.8 Aliasing

<a class="vx-rule" id="refs.borrow.defined" href="#refs.borrow.defined">refs.borrow.defined</a> A **borrow** is the part of the program during which a reference made by `&` or
`&mut` is live. The borrowed variable is the root of the operand place.

- <a class="vx-rule" id="refs.borrow.let-duration" href="#refs.borrow.let-duration">refs.borrow.let-duration</a> A borrow made by a `let` initializer lasts from the declaration to the end of
  the enclosing block.
- <a class="vx-rule" id="refs.borrow.call-duration" href="#refs.borrow.call-duration">refs.borrow.call-duration</a> A borrow made by a call argument covers the whole call: the evaluation of
  every argument of that call and the execution of the callee.

<a class="vx-rule" id="refs.borrow.exclusivity" href="#refs.borrow.exclusivity">refs.borrow.exclusivity</a> While a mutable borrow of `v` is live, `v` must not be used except through that
reference: no place rooted at `v` may be read, assigned, passed or borrowed.
While a shared borrow of `v` is live, no place rooted at `v` may be assigned or
borrowed with `&mut`; reading `v` and further shared borrows are allowed.

<a class="vx-rule" id="refs.borrow.per-variable-conflict" href="#refs.borrow.per-variable-conflict">refs.borrow.per-variable-conflict</a> Conflicts are checked per whole variable, so `&mut values[0]` and
`&mut values[1]` conflict. A conflict is a semantic error. In particular,
within one call, a variable borrowed by a `&mut` argument must not appear in
any other argument of that call, before or after it:
`add_into(&mut values, values)` is a semantic error. Decision record:
[Value semantics](../decisions/references.md#d25).

```vortex
// statements: semantic error
let mut values = [1, 2];
let writer = &mut values;
let reader = &values; // semantic error: writer borrows values to the end of the block
```

Enclosing `let writer = &mut values;` in an inner block ends the borrow at that
block's `}`, after which `&values` is valid.

<a class="vx-rule" id="refs.borrow.guarantee" href="#refs.borrow.guarantee">refs.borrow.guarantee</a> These rules guarantee that storage reached through a `&mut` parameter is not
reachable through any other parameter of the same call. Code generation may
rely on that guarantee.

Decision record: [Where references may appear, and how long a borrow lasts](../decisions/references.md#d41).

## 9.9 Excluded pointer behavior

<a class="vx-rule" id="refs.excluded.pointer-behavior" href="#refs.excluded.pointer-behavior">refs.excluded.pointer-behavior</a> Vortex v0.1 has no raw pointers, null references, pointer arithmetic, address casts,
manual allocation, manual deallocation, user-visible unsafe blocks, reference
fields, arrays of references, or reference return types.
