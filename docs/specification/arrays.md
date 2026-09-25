# 7. Arrays and shapes

An array is a fixed-size, homogeneous value. Its element type, rank, and
dimensions are part of its type and are known before storage layout is
generated.

## 7.1 Array types

```ebnf
array_type ::=
    "[", type, ";",
    expression, { ",", expression },
    "]" ;
```

Examples:

```vortex
// fragment
[f32; 4]
[f32; 4, 4]
[[i32; 2]; 3]
&mut [f32; 4 * 4]
```

<a class="vx-rule" id="arrays.types.const-dims" href="#arrays.types.const-dims">arrays.types.const-dims</a> Each dimension must be an integer constant expression (see
[7.2](#72-dimension-rules)). A type such as `[f32; rows * columns]` parses, but
v0.1 rejects it with a constant-evaluation error because `rows` and `columns`
are names.

## 7.2 Dimension rules

<a class="vx-rule" id="arrays.dims.const" href="#arrays.dims.const">arrays.dims.const</a> Every dimension is parsed as a complete expression and preserved in the AST.
Each dimension must be an integer constant expression
([Expressions, 5.12](expressions.md#512-constant-expressions)): integer
literals, each optionally preceded by unary `-`, combined only with
parentheses and the binary operators `+`, `-`, `*`, `/` and `%`. A name is
never a constant expression, even when it names an immutable variable with a
literal initializer, and a call is never one.

<a class="vx-rule" id="arrays.dims.evaluation" href="#arrays.dims.evaluation">arrays.dims.evaluation</a> The compiler checks this form first: a dimension that is not an integer
constant expression is a constant-evaluation error, whatever the types of its
parts. The literals of a dimension have type `usize`, so a literal that does
not fit `usize`, such as `-1`, is a type error, as in any other `usize`
context. The dimension is then evaluated with checked `usize` arithmetic; it is
a constant-evaluation error when an operation leaves the range of `usize` or
divides by zero, or when the total size of the array exceeds the
implementation's documented limit. Names in a dimension are resolved before
this check, so an undeclared name is a name error. See
[decision 11](../decisions/arrays.md#d11).

<a class="vx-rule" id="arrays.dims.eval-timing" href="#arrays.dims.eval-timing">arrays.dims.eval-timing</a> The compiler evaluates each dimension when it resolves the array type, before
it compares types, lays out storage, or generates code; a failure is a
constant-evaluation error whichever phase finds it. See
[decision 52](../decisions/arrays.md#d52).

The grammar deliberately does not restrict dimensions to literal tokens:

```vortex
// statements: valid
let row: [f32; 2 + 2] = [0.0; 2 + 2];
let tile: [f32; 2 * 2, 8 / 2] = [0.0; 2 * 2, 8 / 2];
```

Runtime parameters do not make an array runtime-sized:

```vortex
// items: constant-evaluation error
fn make(size: usize) {
    let values: [f32; size] = [0.0; size];
    // constant-evaluation error: size is a name, not a constant expression
}
```

The reasons for naming the category are in
[record 51](../decisions/documentation.md#d51).

<a class="vx-rule" id="arrays.dims.nonzero" href="#arrays.dims.nonzero">arrays.dims.nonzero</a> Every extent must be at least 1. A dimension whose value is zero, whether
written (`[f32; 0]`) or computed (`[f32; 4 - 4]`), is a constant-evaluation
error. The rule applies to every dimension of a multidimensional type, to the
element types of nested arrays such as `[[f32; 0]; 3]`, and to repeat arrays
such as `[0.0; 0]`. See [decision 10](../decisions/arrays.md#d10).

## 7.3 Element-list construction

```ebnf
array_element_list ::=
    expression, { ",", expression }, [ "," ] ;
```

<a class="vx-rule" id="arrays.elements.order" href="#arrays.elements.order">arrays.elements.order</a> An element-list array contains one or more expressions in source order:

```vortex
// fragment
[1.0, 2.0, 3.0]
[first(), second(), third()]
```

<a class="vx-rule" id="arrays.elements.same-type" href="#arrays.elements.same-type">arrays.elements.same-type</a> All elements must have the same type after
[literal typing](types-and-values.md#literal-typing): a literal element takes
the expected element type when the array's type is expected, and its default
type otherwise, so `[count, 1]` with a `u32` `count` needs a written array
type. Nested arrays must have equal element types and shapes
([decision record](../decisions/numbers.md#d30)). The trailing comma is
optional.

<a class="vx-rule" id="arrays.elements.expected-type" href="#arrays.elements.expected-type">arrays.elements.expected-type</a> When an element-list array has an expected type `[T; d1, d2, ..., dn]` with n
of at least 2, it must have exactly d1 elements, and each element is checked
against the expected type `[T; d2, ..., dn]`; a wrong element count or a
mismatched element is a type error. Expected types come from the typing
contexts in [Types and values](types-and-values.md#literal-typing), such as an
annotated `let`, a parameter, or a return type. Without such an expected type,
an element list whose elements are arrays has a nested array type:
`[[1.0, 2.0], [3.0, 4.0]]` has type `[[f32; 2]; 2]`. A value of a nested array
type never converts to a multidimensional array type, or the reverse. See
[decision 21](../decisions/arrays.md#d21).

```vortex
// statements: valid
let matrix: [f32; 2, 2] = [[1.0, 2.0], [3.0, 4.0]];
let grid = [[1.0, 2.0], [3.0, 4.0]]; // type [[f32; 2]; 2]
```

<a class="vx-rule" id="arrays.elements.empty-rejected" href="#arrays.elements.empty-rejected">arrays.elements.empty-rejected</a> An empty array expression `[]` is not accepted in v0.1 because it supplies no
element from which local type inference can determine an element type.

## 7.4 Repeat-array construction

```ebnf
repeat_array_body ::=
    expression, ";",
    expression, { ",", expression } ;
```

<a class="vx-rule" id="arrays.repeat.structure" href="#arrays.repeat.structure">arrays.repeat.structure</a> The expression before `;` supplies the repeated value. The expressions after
`;` supply one or more dimensions:

```vortex
// fragment
[0.0; 16]
[0.0; 4, 4]
[false; 2 + 2]
```

<a class="vx-rule" id="arrays.repeat.evaluation" href="#arrays.repeat.evaluation">arrays.repeat.evaluation</a> The repeated value expression is evaluated once, and its resulting value fills
the array. Each dimension follows the same constant-evaluation rules as an
array type. A repeat array has the type its dimensions spell, whatever its
expected type: `[0.0; 2, 2]` has type `[f32; 2, 2]`, and `[[0.0; 2]; 2]` has
type `[[f32; 2]; 2]` ([decision 21](../decisions/arrays.md#d21)).

<a class="vx-rule" id="arrays.repeat.no-trailing-comma" href="#arrays.repeat.no-trailing-comma">arrays.repeat.no-trailing-comma</a> A trailing comma after repeat dimensions is not accepted.

## 7.5 Rank and shape

<a class="vx-rule" id="arrays.shape.definitions" href="#arrays.shape.definitions">arrays.shape.definitions</a> The **rank** of an array is its number of dimensions. Its **shape** is the
ordered list of evaluated extents.

| Type | Rank | Shape |
| --- | ---: | --- |
| `[f32; 4]` | 1 | `[4]` |
| `[f32; 2, 3]` | 2 | `[2, 3]` |
| `[f32; 2 + 2]` | 1 | `[4]` once the dimension is evaluated |

<a class="vx-rule" id="arrays.shape.type-equality" href="#arrays.shape.type-equality">arrays.shape.type-equality</a> Array type equality requires equal element types, ranks, and evaluated shapes.

## 7.6 Indexing

```ebnf
index_suffix ::=
    "[", expression, { ",", expression }, "]" ;
```

<a class="vx-rule" id="arrays.index.types" href="#arrays.index.types">arrays.index.types</a> Vortex array indexing is zero-based. Each index must have an integer type:
`i32`, `u32` or `usize`. The indices of one access may have different integer
types. An index of any other type is a type error. An index `i` is within the
bounds of a dimension with extent `n` when `0 <= i < n`, compared as
mathematical integers, so a negative index is out of bounds.

```vortex
// fragment
let first = values[0];
let cell = matrix[row, column];
```

<a class="vx-rule" id="arrays.index.arity" href="#arrays.index.arity">arrays.index.arity</a> The number of indices in an index suffix must equal the rank of the indexed
array; any other count is a type error. A nested array takes one index suffix
per level: if `grid` has type `[[i32; 2]; 3]`, then `grid[1][0]` selects an
element and `grid[1, 0]` is a type error. Partial indexing, linear indexing,
and slices are not part of v0.1. See [decision 47](../decisions/arrays.md#d47).

<a class="vx-rule" id="arrays.index.bounds-check" href="#arrays.index.bounds-check">arrays.index.bounds-check</a> When an index is an integer constant expression
([Expressions, 5.12](expressions.md#512-constant-expressions)), the compiler
must check it against its extent during compilation, and an index outside its
extent is a constant-evaluation error. Every other index is checked when the
program runs, and an index outside its extent is a runtime error. An
implementation must not reject an index that is not an integer constant
expression, even when it can prove the index is out of bounds
([record 39](../decisions/diagnostics.md#d39)). See
[decision 12](../decisions/arrays.md#d12).

<a class="vx-rule" id="arrays.index.eval-order" href="#arrays.index.eval-order">arrays.index.eval-order</a> An index expression evaluates its base and then its indices from left to
right, and then checks each index against its extent, from left to right
([5.10 Evaluation order](expressions.md#510-evaluation-order)).

## 7.7 Assignment

<a class="vx-rule" id="arrays.assign.value-semantics" href="#arrays.assign.value-semantics">arrays.assign.value-semantics</a> Arrays are values. Initializing a variable, assigning, passing an argument to a
parameter whose type is not a reference type, and returning a value each copy
every element; there are no moves ([Structs](structs.md) 8.6). After
`let copy = values;`, assigning to `values[0]` does not change `copy[0]`.
Decision record: [Value semantics](../decisions/references.md#d25).

<a class="vx-rule" id="arrays.assign.mutable-place" href="#arrays.assign.mutable-place">arrays.assign.mutable-place</a> An element may be assigned when its place is mutable
([References and mutability](references.md) 9.3): the root name is a variable
declared with `let mut`, or a name of type `&mut T` such as a `&mut [f32; 4]`
parameter ([record 23](../decisions/references.md#d23)).

```vortex
// statements: valid
let mut values = [1.0, 2.0, 3.0];
values[0] = 10.0;
```

<a class="vx-rule" id="arrays.assign.eval-order" href="#arrays.assign.eval-order">arrays.assign.eval-order</a> In an element assignment, the target's indices are evaluated before the
assigned value and checked against their extents after it, just before the
store ([Statements 6.4](statements.md#64-assignment)). See
[decision 38](../decisions/statements.md#d38).

<a class="vx-rule" id="arrays.assign.fixed-shape" href="#arrays.assign.fixed-shape">arrays.assign.fixed-shape</a> Vortex v0.1 has fixed shapes. Assignment never resizes an array. Shape mismatch is a
type error rather than a request to truncate, pad, or reallocate.

## 7.8 Memory and layout

<a class="vx-rule" id="arrays.layout.row-major" href="#arrays.layout.row-major">arrays.layout.row-major</a> An array value is stored contiguously: its elements occupy consecutive
positions with no gap between them. A multidimensional array is stored in
row-major order: the last index varies fastest. For `[T; rows, columns]`, the
element at `[r, c]` is at position `r * columns + c`, counted in elements from
the start of the array, and each higher rank extends the same rule. A nested
array `[[T; d2]; d1]` is stored the same way, one inner array after another.
<a class="vx-rule" id="arrays.layout.element-size" href="#arrays.layout.element-size">arrays.layout.element-size</a> The size and alignment of each element type are implementation-defined and
must be documented
([Conformance](conformance.md#110-implementation-defined-behavior-and-limits)).
<a class="vx-rule" id="arrays.layout.no-address-observation" href="#arrays.layout.no-address-observation">arrays.layout.no-address-observation</a> A v0.1 program cannot observe addresses, so layout does not change results; it
fixes which loop orders read neighbouring elements. See
[decision 43](../decisions/arrays.md#d43).

## 7.9 Excluded array behavior

<a class="vx-rule" id="arrays.excluded.features" href="#arrays.excluded.features">arrays.excluded.features</a> Vortex v0.1 has no runtime-sized array types, vectors, slices, array views, open-ended
index ranges, shape broadcasting, implicit reshaping, or built-in matrix
operators. These require separate future specifications.
