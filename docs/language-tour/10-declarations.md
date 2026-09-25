# Declarations

--8<-- "includes/remember/language-tour__10-declarations.md"

## Learning goals

After this chapter, you should be able to distinguish top-level declarations
from child declarations and local declaration statements, describe what each
one stores, and explain how the compiler enters names into scopes.

A declaration introduces a name or definition that the rest of a program can
use. Vortex has declarations for functions, structs, local variables,
parameters, and struct fields.

Declarations are checked by the compiler. The compiler records each name, its
[scope](../specification/glossary.md) (the region of code where the name is visible), and its type before it generates code.

## Declaration quick reference

| Form | Where it appears | Independent top-level declaration? |
| --- | --- | --- |
| Function declaration | Program | Yes |
| Struct declaration | Program | Yes |
| Parameter declaration | Inside a function signature | No; child of its function |
| Field declaration | Inside a struct body | No; child of its struct |
| Variable declaration | Inside a block | No; it is a statement |

Vortex v0.1 has no standalone type-alias declaration. A struct declaration introduces
a named user-defined type. Modules, generics, traits, classes, global variables,
and compile-time constant declarations are outside v0.1.

## Function declarations

A function declaration gives a function a name, parameters, an optional return
type, and a body:

```vortex
// items: valid
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}
```

The declaration contains these parts:

- `fn` marks the declaration as a function.
- `add` is the function name.
- `left` and `right` are parameter names.
- `i32` is the declared type of each parameter.
- `-> i32` declares the return type.
- The block after the signature is the function body.

A function with no return value can omit the return type. Vortex treats the
missing type as `void`:

```vortex
// items: valid
fn show_message(message: String) {
    print(message);
}
```

Functions are declared at the top level. A program can call a function from
anywhere in the file, even above the function's declaration, and a function can
call itself ([decision record](../decisions/names.md#d3)).

??? check "A file declares `fn main() { print(triple(4)); }` first, then `fn triple(value: i32) -> i32 { return value * 3; }` below it. Does this compile?"

    Yes. Every top-level function and struct name is visible throughout the
    file, including above its own declaration, so `main` can call `triple`
    before the compiler has read `triple`'s body
    ([decision](../decisions/names.md#d3)).

Every executable program must contain exactly one `main` function. In v0.1,
`main` takes no parameters and returns `void`:

```vortex
// program: valid
fn main() {
    show_message("ready");
}

fn show_message(message: String) {
    print(message);
}
```

A function declaration does not run the function. The function runs only when
code calls it, or when the runtime starts the `main` function.

### Allowed and not allowed

- A function may have zero or more comma-separated parameters.
- Every parameter needs an explicit type.
- The return type may be omitted only to mean `void`.
- A function body is always a block.
- Nested function declarations, default parameter values, variadic parameters,
  overload declarations, and function prototypes without bodies are not part
  of v0.1.

```vortex
// items: syntax error
fn add(left, right: i32) -> i32 { // invalid: left has no type
    return left + right;
}

fn declared_only(value: i32) -> i32; // invalid: body is missing
```

The parser (the [compiler stage](../compiler/guide/index.md) that checks how tokens fit together and builds the program's structure) builds a function declaration with its name, ordered parameter
children, optional written return type, and body. Name resolution enters the
function in top-level scope and its parameters in function scope. Type and
control-flow checking validate calls and returns.

## Struct declarations

A struct declaration creates a user-defined type with named fields:

```vortex
// items: valid
struct Point {
    x: f32,
    y: f32,
}
```

Each field declaration contains a name and a type. The field names must be
unique within the struct.

A struct declaration defines a type. It does not create a value by itself. Use
a struct expression to create a value:

```vortex
// program: valid
struct Point {
    x: f32,
    y: f32,
}

fn main() {
    let origin = Point {
        x: 0.0,
        y: 0.0,
    };
}
```

A struct expression must provide the fields required by the struct. The
compiler rejects unknown fields, duplicate fields, missing fields, and values
with the wrong type.

Structs are simple value types in v0.1. Inheritance and class declarations are
not part of the language.

### Allowed and not allowed

- A struct contains one or more comma-separated field declarations; an empty
  struct such as `struct Marker {}` is a syntax error
  ([why](../decisions/operators.md#d26)).
- Each field has a unique name and an explicit type.
- A struct's name must differ from every function name, every other struct name
  and `print`, because all top-level names share one namespace (a set of names
  in which each name has one meaning)
  ([decision record](../decisions/names.md#d5)).
- A struct cannot contain itself, directly or through an array or another
  struct: `struct Node { next: Node }` is invalid, because such a value would
  need infinite storage ([why](../decisions/operators.md#d45)).
- A trailing comma is allowed.
- Field initializers, methods, visibility modifiers, inheritance, and generic
  fields are not part of v0.1 struct declarations.

??? check "`struct A { b: B }` and `struct B { a: A }` are declared together, each holding the other by value. Does this compile?"

    No. Two structs that hold each other by value close the same kind of
    cycle as a struct containing itself: both would need infinite storage, so
    the compiler rejects the pair with a type error
    ([decision](../decisions/operators.md#d45)).

```vortex
// items: name error
struct Invalid {
    value: i32,
    value: f32, // invalid: duplicate field name
}
```

```vortex
// items: syntax error
struct AlsoInvalid {
    count = 0, // invalid: a field declaration uses name: type
}
```

The parser stores ordered field children in the struct declaration. Name
resolution introduces the struct type, and type checking resolves every field
type and later checks struct construction.

## Variable declarations

Use `let` to declare a local variable:

```vortex
// program: valid
fn main() {
    let width = 128;
    let mut total: f32 = f32(width);

    total = total + 1.0;
}
```

A variable declaration can include an optional type annotation:

```vortex
// statements: valid
let count: i32 = 10;
let name: String = "vortex";
```

When the type is omitted, the compiler infers it from the initializer:

```vortex
// statements: valid
let count = 10;       // i32
let temperature = 21.5; // f32
```

A local variable must have an initializer in v0.1. The compiler cannot infer a
type from a declaration without a starting value:

```vortex
// statements: syntax error
let missing_value; // syntax error: no initializer
```

Variables are immutable by default. Add `mut` when the variable must change:

```vortex
// statements: valid
let mut counter = 0;
counter += 1;
```

A declaration with no `mut` cannot be the target of an assignment:

```vortex
// statements: semantic error
let limit = 10;
limit = 20; // semantic error: limit was not declared mut
```

??? check "`let mut counter = 0;` declares `counter` as `i32`. Does `mut` let a later statement write `counter = 2.5;`?"

    No. `mut` only allows a new value of the variable's existing type; it does
    not let the variable change type. `counter` stays `i32` for its whole
    lifetime, so assigning `2.5` to it is a type error, the same rule that
    rules out changing a variable's type after its declaration.

A local variable declaration is also a statement because it appears inside a
block and affects program execution by creating a value.

Local declarations cannot appear directly at the top level. Vortex v0.1 also does not
support uninitialized locals, global variables, destructuring declarations, or
changing a variable's type after its declaration.

## Parameter declarations

A parameter declaration names one input to a function and gives it a type:

```vortex
// items: valid
fn scale(value: f32, factor: f32) -> f32 {
    return value * factor;
}
```

`value` and `factor` are parameters. Their types are written in the function
signature and are required in v0.1.

A parameter is available inside the function body. A call supplies an argument
for each parameter:

```vortex
// fragment
let result = scale(2.0, 3.0);
```

The compiler checks the number and types of arguments against the parameter
list. A parameter that is not a reference receives a copy of its argument, even
when the argument is a whole array or struct. Only a `&mut` reference parameter
lets a function change the caller's value
([decision](../decisions/references.md#d25)). Parameters are local to their
function and cannot be used outside it.

Each parameter keeps its own name, type and source position, so an error
message can point at the exact parameter.
[Parser design](../compiler/parser-design.md#72-parameter-declaration) explains
how the compiler stores parameters.

Parameters are immutable: a function cannot assign to a parameter. Default
values and `mut` parameter syntax are not part of v0.1:

```vortex
// items: syntax error
fn invalid(value: i32 = 10) { }
// invalid: default parameters are unsupported
```

To change a value inside a function, copy it into a local first, as in
`let mut result = value;`, or use a `&mut` reference parameter when the
caller's value should change ([decision](../decisions/references.md#d23)).

Parameters can use references when a function must read or change an existing
value without copying it:

```vortex
// items: valid
fn scale_values(values: &mut [f32; 4], factor: f32) {
    for index in 0..4 {
        values[index] *= factor;
    }
}
```

A reference can be a parameter type but not a return type or a field type, so
a function never keeps a reference after it returns
([decision](../decisions/references.md#d41)).

The rules for shared and mutable references are described in
[Variables and types](04-variables-and-types.md).

## Field declarations

A field declaration gives a name and type to one value stored in a struct:

```vortex
// items: valid
struct Size {
    width: usize,
    height: usize,
}
```

`width` and `height` are fields of `Size`. A field belongs to its struct type;
it is not a local variable and cannot be used as a standalone name.

Read a field with the field-access operator:

```vortex
// fragment
let area = size.width * size.height;
```

Field access uses the type of the value on the left side of the dot. The
compiler rejects a field name that does not exist for that type.

Like a parameter, a field declaration exists only as a child record of its
owning declaration. A struct declaration contains its fields in order; a field
is not placed in a local scope or in the program scope, so a field may share
its name with a function, a struct or a variable. A field declaration stores a
source location, field name, and type, but no runtime value. A struct expression
supplies the value later.

## Names and scopes

A declaration makes a name visible in a scope. Vortex uses these scopes:

- top-level functions and structs are visible in the whole file, even above
  their declarations;
- parameters are visible inside their function body;
- local variables are visible from the end of their declaration to the end of
  their block;
- fields are visible through values of their struct type.

A nested block creates a nested scope:

```vortex
// program: name error
fn main() {
    let outside = 1;

    {
        let inside = 2;
        print(outside);
        print(inside);
    }

    print(outside);
    print(inside); // name error: outside the scope of inside
}
```

??? check "`struct Point { x: f32 }` is declared at the top level. Can a function have a parameter named `Point`?"

    No. Every top-level function and struct name is visible throughout the
    file, so no parameter, local or loop variable may reuse it: doing so is a
    name error, the same rule that blocks shadowing inside a nested block
    ([decision](../decisions/names.md#d2)).

Names are case-sensitive. A name cannot start with a number, and names can
contain ASCII letters, numbers, and `_`:

```vortex
// statements: valid
let item_count = 4;
let ItemCount = 8;
```

These are different names. The compiler rejects duplicate declarations in the
same scope. Vortex also has no shadowing: a declaration cannot reuse a name
that is already visible, even inside an inner block, and a local, parameter or
loop variable cannot take the name of a top-level function or struct, or
`print` ([decision record](../decisions/names.md#d2)).

```vortex
// statements: name error
let total = 5;
{
    let total = 6; // name error: total is already visible
}
```

Blocks that are never open at the same time can reuse a name, so two loops one
after the other may both use `index`.

## Declaration grammar

The v0.1 grammar defines top-level functions and structs as follows:

```ebnf
program ::=
    { function | struct_definition } ;

function ::=
    "fn", identifier,
    "(", [ parameter_list ], ")",
    [ "->", type ],
    block ;

struct_definition ::=
    "struct", identifier, "{",
    struct_field, { ",", struct_field }, [ "," ],
    "}" ;

struct_field ::=
    identifier, ":", type ;
```

Local variable declarations are block statements:

```ebnf
variable_declaration ::=
    "let", [ "mut" ], identifier,
    [ ":", type ],
    "=", expression, ";" ;
```

See the complete grammar in [Vortex v0.1 grammar](../specification/grammar.md).

## Declarations planned for later

The first version keeps declarations small. These features are outside v0.1:

- modules and packages;
- generic declarations;
- traits or interfaces;
- compile-time constants (`const`), planned as the first addition after v0.1;
  until then an array dimension uses integer literals only
  ([decision](../decisions/arrays.md#d11));
- methods declared inside structs;
- class and inheritance declarations.

These features can be added later when their effect on name resolution, types,
and numerical code is defined.

## Compiler handling summary

<details markdown="1">
<summary>Which compiler stage enforces each rule (optional reading)</summary>

1. The parser builds top-level function and struct declarations.
2. Parameter and field records are constructed only inside their owners.
3. Name resolution creates top-level, function, block, and struct-field scopes.
4. Type checking resolves every written type and checks initializers, calls,
   struct construction, assignments, and returns.
5. Code generation lays out structs, creates function symbols, and allocates
   storage for parameters and local variables.

</details>

## Practice and self-check

For each item, name its owner and scope:

```vortex
// items: valid
struct Point {
    x: f32,
}

fn move_x(point: Point, amount: f32) -> Point {
    let result = Point { x: point.x + amount };
    return result;
}
```

Answers:

- `Point` is a top-level struct declaration and introduces a named type.
- `x` is a field child owned by `Point`; it is selected through a `Point` value.
- `point` and `amount` are parameter children owned by `move_x` and visible in
  its function body.
- `result` is a local variable-declaration statement visible from the end of
  its declaration to the end of the function block.

## Key ideas

!!! recap "Questions you can now answer"

    - **Which two declaration forms are independent top-level declarations,
      and which three are children or statements?** Function and struct
      declarations stand on their own at the top level. Parameter
      declarations are children of a function, field declarations are
      children of a struct, and variable declarations are statements inside a
      block.
    - **Does declaring a function run it?** No. A function body runs only
      when code calls it, or when the runtime starts `main`.
    - **Can a function call another function written later in the same
      file?** Yes. Every top-level function and struct name is visible
      throughout the file, including above its own declaration.
    - **Can a struct contain itself, directly or through another struct that
      holds it back?** No. Either shape needs infinite storage, so the
      compiler rejects it with a type error.
    - **Does a local variable declaration ever skip its initializer?** No. A
      local must have an initializer, since v0.1 has no way to infer a type
      from a bare declaration.
    - **Where is a struct field's name visible?** Only through a value of its
      struct type. A field is never placed in a local or program scope, so it
      may share its name with a function, a struct or a variable.
    - **Does Vortex allow a declaration to reuse a name already visible where
      it appears?** No. That would be shadowing, and Vortex has none: each
      name has one meaning at every point in the scopes where it is visible.

## Where this comes back

--8<-- "includes/next/language-tour__10-declarations.md"
