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

Field declarations use `name: type`. A struct declares one or more fields.
Fields are comma-separated, and the final comma is optional. Each field name
must be unique within its struct.

A struct declaration introduces the struct name into the program scope, the
one namespace shared by every function and struct
([Scopes](declarations.md#36-scopes)). The name must differ from every other
function and struct name and from `print`; a clash is a name error
([decision record](../decisions/names.md#d5)). A struct declaration does not
create a runtime value.

A struct must not contain itself by value, directly or through any chain of
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

A struct must declare at least one field. The grammar has no empty field list,
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

A struct expression names the target struct and provides one initializer for
every declared field:

```vortex
// fragment
let origin = Point {
    x: 0.0,
    y: 0.0,
};
```

The parser preserves the struct name and every initializer's field name, value,
order, and location. Checking then applies these rules:

- The name must resolve; an unresolved name is a name error.
- The name must resolve to a struct. A function, local variable, parameter or
  loop variable used as the name of a struct expression is a type error.
- Every field of the struct must be initialized exactly once, in any order. A
  missing field, an unknown field, or a field initialized twice is a type
  error.
- Every initializer value must be compatible with its field's declared type;
  otherwise it is a type error.

Initializer values are evaluated in the order they are written, not in
declaration order. Calling a struct as if it were a function, as in
`Point(0.0, 0.0)`, is a type error: a struct is not a function.
([Why](../decisions/operators.md#d7))

Empty, positional, shorthand, anonymous, and update-style struct expressions
are not accepted:

```vortex
// fragment
Point {}                  // syntax error: at least one field initializer is required
Point(0.0, 0.0)           // type error: a struct is not a function
Point { x, y }            // syntax error: shorthand fields are not accepted
```

## 8.4 Field access

Use `.` to select a field from a struct value:

```vortex
// fragment
let horizontal = point.x;
let area = size.width * size.height;
```

Field access can participate in postfix chains:

```vortex
// fragment
points[index].position.x
```

Type checking resolves the object type and rejects a field that does not exist.

## 8.5 Field assignment

A field is assignable when its place is mutable
([References and mutability](references.md) 9.3): the root of the access path
is a variable declared with `let mut`, or a name of type `&mut T`.

```vortex
// fragment
let mut point = Point { x: 0.0, y: 0.0 };
point.x = 4.0;
```

A field of an immutable binding, including a parameter, cannot be modified;
assigning to it is a semantic error
([record 23](../decisions/references.md#d23)). The field declaration does not
independently carry mutability in v0.1.

## 8.6 Value semantics

Structs are value types. A struct expression creates a value rather than an
object with independent identity.

Initializing a variable, assigning, passing an argument to a parameter whose
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

Struct types are nominal: two separately declared structs are distinct types
even when they have the same field names and field types.

```vortex
// items: valid
struct Position { x: f32, y: f32 }
struct Velocity { x: f32, y: f32 }
```

`Position` and `Velocity` are not interchangeable without an explicitly
specified conversion, and v0.1 defines no implicit structural conversion.

## 8.8 Layout

A struct value stores its fields in declaration order; an implementation must
not reorder them. Padding between fields and after the last field, and the
alignment of the struct, are implementation-defined and must be documented
([Conformance](conformance.md#110-implementation-defined-behavior-and-limits)).
ABI compatibility with other languages is not specified in v0.1. See
[decision 43](../decisions/arrays.md#d43).

## 8.9 Excluded struct features

Vortex v0.1 has no field defaults, private fields, methods, constructors,
destructors, inheritance, anonymous structs, generics, extension declarations,
or update syntax.
