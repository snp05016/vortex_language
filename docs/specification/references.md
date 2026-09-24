# 9. References and mutability

References let a function access an existing value without exposing raw pointer
arithmetic. Vortex v0.1 distinguishes shared references from mutable references.

## 9.1 Reference types

```ebnf
reference_type ::=
    "&", [ "mut" ], type ;
```

`&T` is a shared reference to `T`. `&mut T` is a mutable reference to `T`.

```vortex
// fragment
&Point
&[f32; 4]
&mut [f32; 4]
```

Reference mutability is part of the type.

## 9.2 Reference expressions

Prefix `&` creates a shared reference. Prefix `&mut` creates a mutable
reference. A reference expression produces a reference to the storage its
operand names, called the **referent**.

```vortex
// fragment
let value = 10;
let shared = &value;

let mut values = [1.0, 2.0, 3.0, 4.0];
scale(&mut values, 2.0);
```

The operand must be a place (9.3), optionally inside parentheses. Any other
operand, such as a literal, a call result or an arithmetic result, is a
semantic error ([record 23](../decisions/references.md#d23)).

## 9.3 Local mutability

Bindings are immutable by default:

```vortex
// statements: valid
let limit = 10;
```

`mut` permits assignment to the bound storage:

```vortex
// statements: valid
let mut count = 0;
count += 1;
```

Mutability does not change the value's base type. It controls whether storage
can be modified.

A **place** is an expression that names storage: a name followed by zero or
more index and field suffixes, such as `total`, `grid[row, column]` or
`points[index].x`. The name is the place's **root**. Assignment targets and the
operands of `&` and `&mut` are places.

A place is **mutable** only when its root is:

- a local variable declared with `let mut`; or
- a parameter or `let` binding whose type is `&mut T`. Such a place denotes
  storage reached through the reference (9.4).

Every other place is immutable. Parameters are immutable bindings, and v0.1 has
no `mut` parameter syntax. A `let` variable declared without `mut`, a `for`
loop variable, and a place rooted at a name of type `&T` are also immutable.

Assigning to an immutable place, with `=` or any compound assignment operator,
is a semantic error. Taking `&mut` of an immutable place is a semantic error.
The diagnostic points at the assignment target or the `&mut` operand and may
add a note at the root's declaration.

Decision record: [Parameters and mutable places](../decisions/references.md#d23).

## 9.4 Shared references

A shared reference permits reading its referent. A place rooted at a name of
type `&T` is immutable (9.3): assigning to it, or taking `&mut` of it, is a
semantic error. Any number of shared references to the same variable may exist
at the same time (9.8).

### Using a reference

A name whose type is `&T` or `&mut T` denotes its referent, not the reference
itself:

- used as a value, the name reads the referent, and the expression has type
  `T`;
- index and field suffixes apply to the referent: if `values` has type
  `&mut [f32; 4]`, then `values[index]` is an element of the referenced array;
- as the root of an assignment target, the name writes the referent, which
  requires a `&mut` reference (9.3);
- `&name` and `&mut name` create a new reference to the same referent, so no
  expression produces a reference to a reference.

Vortex v0.1 has no dereference operator `*`. A reference binding cannot be
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

The operand of `&mut` must be a mutable place (9.3). A mutable reference
permits writing its referent (9.4).

While a mutable borrow of a variable is live, the variable may be used only
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

The argument must satisfy the parameter's mutability and type requirements. A
parameter whose type is not a reference type receives a copy of its argument
([Structs](structs.md) 8.6, [Arrays and shapes](arrays.md) 7.7).

Because a name of reference type used as a value reads its referent (9.4), the
only expressions of reference type are `&place` and `&mut place`; an argument
for a reference parameter is therefore always written with `&` or `&mut`, and
its type must equal the parameter type. The borrow it makes lasts for the call
(9.8). A function cannot keep a reference after it returns, because v0.1 has no
reference return types, fields or array elements (9.7).

## 9.7 Lifetimes

A reference must never outlive the storage it refers to. Vortex v0.1 has no
lifetime syntax and needs no lifetime inference, because a reference type may
appear only:

- as the type of a function parameter; or
- as the type of a `let` binding declared without `mut`.

A reference type anywhere else is a type error: as a function's return type, a
struct field type, an array element type, the referenced type of another
reference, or the type of a `let mut` binding.

A reference expression (`&place` or `&mut place`) may appear only as a complete
call argument or as the complete initializer of a `let` declared without
`mut`, in either case optionally inside parentheses. A reference expression
anywhere else is a type error.

Under these rules a `let` reference ends with its block, and its referent is a
parameter, a variable declared earlier in the same or an enclosing block, or
storage that a caller owns for the whole call. Every referent therefore
outlives every reference to it. See [record 41](../decisions/references.md#d41).

## 9.8 Aliasing

A **borrow** is the part of the program during which a reference made by `&` or
`&mut` is live. The borrowed variable is the root of the operand place.

- A borrow made by a `let` initializer lasts from the declaration to the end of
  the enclosing block.
- A borrow made by a call argument covers the whole call: the evaluation of
  every argument of that call and the execution of the callee.

While a mutable borrow of `v` is live, `v` must not be used except through that
reference: no place rooted at `v` may be read, assigned, passed or borrowed.
While a shared borrow of `v` is live, no place rooted at `v` may be assigned or
borrowed with `&mut`; reading `v` and further shared borrows are allowed.

Conflicts are checked per whole variable, so `&mut values[0]` and
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

These rules guarantee that storage reached through a `&mut` parameter is not
reachable through any other parameter of the same call. Code generation may
rely on that guarantee.

Decision record: [Where references may appear, and how long a borrow lasts](../decisions/references.md#d41).

## 9.9 Excluded pointer behavior

Vortex v0.1 has no raw pointers, null references, pointer arithmetic, address casts,
manual allocation, manual deallocation, user-visible unsafe blocks, reference
fields, arrays of references, or reference return types.
