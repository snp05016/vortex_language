# 8. Structs

A struct is a named value type containing an ordered set of named fields. It is
Vortex v0.1's user-defined aggregate type.

## 8.1 Declaration

```vortex
// items: valid
struct Point {
    x: f32,
    y: f32,
}
```

<a class="vx-rule" id="structs.fields.declaration" href="#structs.fields.declaration">structs.fields.declaration</a> Field declarations use `name: type`. A struct declares one or more fields.
Fields are comma-separated, and the final comma is optional. Each field name
must be unique within its struct.

<a class="vx-rule" id="structs.decl.name-and-scope" href="#structs.decl.name-and-scope">structs.decl.name-and-scope</a> A struct declaration introduces the struct name into the program scope, the
one namespace shared by every function and struct
([Scopes](declarations.md#36-scopes)). The name must differ from every other
function and struct name and from `print`; a clash is a name error
([decision record](../decisions/names.md#d5)). A struct declaration does not
create a runtime value.

<a class="vx-rule" id="structs.fields.no-self-containment" href="#structs.fields.no-self-containment">structs.fields.no-self-containment</a> A struct must not contain itself by value, directly or through any chain of
field types and array element types. Such a cycle is a type error, because the
struct would need infinite storage ([why](../decisions/operators.md#d45)):

```vortex
// items: type error
struct Tree {
    value: i32,
    children: [Tree; 2], // type error: Tree contains itself
}
```

## 8.2 Empty structs

<a class="vx-rule" id="structs.fields.non-empty" href="#structs.fields.non-empty">structs.fields.non-empty</a> A struct must declare at least one field. The grammar has no empty field list,
so this declaration is a syntax error ([why](../decisions/operators.md#d26)):

```vortex
// items: syntax error
struct Marker {} // a struct needs at least one field
```

## 8.3 Struct construction

```ebnf
struct_expression ::=
    identifier, "{",
    field_initializer,
    { ",", field_initializer },
    [ "," ],
    "}" ;

field_initializer ::=
    identifier, ":", expression ;
```

<a class="vx-rule" id="structs.construct.expression-form" href="#structs.construct.expression-form">structs.construct.expression-form</a> A struct expression names the target struct and provides one initializer for
every declared field:

```vortex
// fragment
let origin = Point {
    x: 0.0,
    y: 0.0,
};
```

<a class="vx-rule" id="structs.construct.preserved-fields" href="#structs.construct.preserved-fields">structs.construct.preserved-fields</a> The parser preserves the struct name and every initializer's field name, value,
order, and location. Checking then applies these rules:

- <a class="vx-rule" id="structs.construct.name-must-resolve" href="#structs.construct.name-must-resolve">structs.construct.name-must-resolve</a> The name must resolve; an unresolved name is a name error.
- <a class="vx-rule" id="structs.construct.name-must-be-struct" href="#structs.construct.name-must-be-struct">structs.construct.name-must-be-struct</a> The name must resolve to a struct. A function, local variable, parameter or
  loop variable used as the name of a struct expression is a type error.
- <a class="vx-rule" id="structs.construct.field-coverage" href="#structs.construct.field-coverage">structs.construct.field-coverage</a> Every field of the struct must be initialized exactly once, in any order. A
  missing field, an unknown field, or a field initialized twice is a type
  error.
- <a class="vx-rule" id="structs.construct.field-type-match" href="#structs.construct.field-type-match">structs.construct.field-type-match</a> Every initializer value must be compatible with its field's declared type;
  otherwise it is a type error.

<a class="vx-rule" id="structs.construct.order-not-function" href="#structs.construct.order-not-function">structs.construct.order-not-function</a> Initializer values are evaluated in the order they are written, not in
declaration order. Calling a struct as if it were a function, as in
`Point(0.0, 0.0)`, is a type error: a struct is not a function.
([Why](../decisions/operators.md#d7))

<a class="vx-rule" id="structs.construct.rejected-forms" href="#structs.construct.rejected-forms">structs.construct.rejected-forms</a> Empty, positional, shorthand, anonymous, and update-style struct expressions
are not accepted:

```vortex
// fragment
Point {}                  // syntax error: at least one field initializer is required
Point(0.0, 0.0)           // type error: a struct is not a function
Point { x, y }            // syntax error: shorthand fields are not accepted
```

## 8.4 Field access

<a class="vx-rule" id="structs.access.dot-operator" href="#structs.access.dot-operator">structs.access.dot-operator</a> Use `.` to select a field from a struct value:

```vortex
// fragment
let horizontal = point.x;
let area = size.width * size.height;
```

<a class="vx-rule" id="structs.access.postfix-chains" href="#structs.access.postfix-chains">structs.access.postfix-chains</a> Field access can participate in postfix chains:

```vortex
// fragment
points[index].position.x
```

<a class="vx-rule" id="structs.access.unknown-field" href="#structs.access.unknown-field">structs.access.unknown-field</a> Type checking resolves the object type and rejects a field that does not exist.

## 8.5 Field assignment

<a class="vx-rule" id="structs.assign.mutable-place" href="#structs.assign.mutable-place">structs.assign.mutable-place</a> A field is assignable when its place is mutable
([References and mutability](references.md) 9.3): the root of the access path
is a variable declared with `let mut`, or a name of type `&mut T`.

```vortex
// fragment
let mut point = Point { x: 0.0, y: 0.0 };
point.x = 4.0;
```

<a class="vx-rule" id="structs.assign.immutable-error" href="#structs.assign.immutable-error">structs.assign.immutable-error</a> A field of an immutable binding, including a parameter, cannot be modified;
assigning to it is a semantic error
([record 23](../decisions/references.md#d23)). The field declaration does not
independently carry mutability in v0.1.

## 8.6 Value semantics

<a class="vx-rule" id="structs.semantics.value-type" href="#structs.semantics.value-type">structs.semantics.value-type</a> Structs are value types. A struct expression creates a value rather than an
object with independent identity.

<a class="vx-rule" id="structs.semantics.copy-semantics" href="#structs.semantics.copy-semantics">structs.semantics.copy-semantics</a> Initializing a variable, assigning, passing an argument to a parameter whose
type is not a reference type, and returning a value each copy the whole
struct. Vortex v0.1 has no moves: after `let copy = point;`, both `point` and
`copy` remain usable, and changing a field of one does not change the other.
An implementation may pass or return a struct by any machine method, including
by address, provided the program cannot observe the difference;
[References and mutability](references.md) 9.8 rules out the calls that could.
Decision record: [Value semantics](../decisions/references.md#d25).

References allow a function to access an existing struct without treating the
struct as a raw pointer.

## 8.7 Type identity

<a class="vx-rule" id="structs.identity.nominal" href="#structs.identity.nominal">structs.identity.nominal</a> Struct types are nominal: two separately declared structs are distinct types
even when they have the same field names and field types.

```vortex
// items: valid
struct Position { x: f32, y: f32 }
struct Velocity { x: f32, y: f32 }
```

<a class="vx-rule" id="structs.identity.no-implicit-conversion" href="#structs.identity.no-implicit-conversion">structs.identity.no-implicit-conversion</a> `Position` and `Velocity` are not interchangeable without an explicitly
specified conversion, and v0.1 defines no implicit structural conversion.

## 8.8 Layout

<a class="vx-rule" id="structs.layout.field-order" href="#structs.layout.field-order">structs.layout.field-order</a> A struct value stores its fields in declaration order; an implementation must
not reorder them. Padding between fields and after the last field, and the
alignment of the struct, are implementation-defined and must be documented
([Conformance](conformance.md#110-implementation-defined-behavior-and-limits)).
ABI compatibility with other languages is not specified in v0.1. See
[decision 43](../decisions/arrays.md#d43).

## 8.9 Excluded struct features

<a class="vx-rule" id="structs.excluded.features" href="#structs.excluded.features">structs.excluded.features</a> Vortex v0.1 has no field defaults, private fields, methods, constructors,
destructors, inheritance, anonymous structs, generics, extension declarations,
or update syntax.
