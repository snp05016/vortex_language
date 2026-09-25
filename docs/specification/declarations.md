# 3. Programs and declarations

<a class="vx-rule" id="decl.program.top-level-forms" href="#decl.program.top-level-forms">decl.program.top-level-forms</a> A Vortex source file contains an ordered sequence of top-level declarations.
Vortex v0.1 has two top-level declaration forms: functions and structs.

## 3.1 Program structure

```ebnf
program ::=
    { top_level_declaration } ;

top_level_declaration ::=
      function
    | struct_definition ;
```

<a class="vx-rule" id="decl.program.order-and-visibility" href="#decl.program.order-and-visibility">decl.program.order-and-visibility</a> Functions and structs may appear in either order. Every top-level function and
struct name is visible throughout the source file, including above its own
declaration: a function may call a function declared later, a function may
call itself, two functions may call each other, and any written type may name
a struct declared later ([decision record](../decisions/names.md#d3)). An
empty source file is grammatically valid, although it is not a valid
executable program.

<a class="vx-rule" id="decl.program.no-nested-at-top-level" href="#decl.program.no-nested-at-top-level">decl.program.no-nested-at-top-level</a> Executable statements, local variables, and nested function or struct
declarations are not permitted at top level.

## 3.2 Functions

```ebnf
function ::=
    "fn", identifier,
    "(", [ parameter_list ], ")",
    [ "->", type ],
    block ;

parameter_list ::=
    parameter, { ",", parameter } ;

parameter ::=
    identifier, ":", type ;
```

<a class="vx-rule" id="decl.functions.preserved-fields" href="#decl.functions.preserved-fields">decl.functions.preserved-fields</a> A function declaration preserves:

- the function name and source location;
- parameters in source order, including each parameter name, type, and
  location;
- the return type;
- the body block.

<a class="vx-rule" id="decl.functions.implicit-void-return" href="#decl.functions.implicit-void-return">decl.functions.implicit-void-return</a> When the return annotation is omitted, the function return type is `void`.

```vortex
// items: valid
fn announce() {
    print("ready");
}

fn add(left: i32, right: i32) -> i32 {
    return left + right;
}
```

<a class="vx-rule" id="decl.functions.parameter-rules" href="#decl.functions.parameter-rules">decl.functions.parameter-rules</a> Parameter names must be unique within a function. Parameters always have an
explicit type. A parameter's type must not be `void`
([why](../decisions/operators.md#d44)).

<a class="vx-rule" id="decl.functions.immutable-parameters" href="#decl.functions.immutable-parameters">decl.functions.immutable-parameters</a> Parameters are immutable bindings: assigning to a parameter, or to any place
rooted at one, is a semantic error unless the parameter's type is `&mut T`, in
which case the assignment writes the referent
([References and mutability](references.md) 9.3). A function that needs a
changeable copy declares one, as in `let mut result = value;`. Default values,
`mut` parameters, variadic parameters, a trailing parameter comma, function
prototypes without bodies, and nested functions are not part of v0.1. The
built-in `print` ([3.9](#39-built-in-functions)) is the only function that
takes a variable number of arguments. Decision record:
[Parameters and mutable places](../decisions/references.md#d23).

<a class="vx-rule" id="decl.functions.return-type-not-reference" href="#decl.functions.return-type-not-reference">decl.functions.return-type-not-reference</a> A parameter may have a reference type. A function's return type must not be a
reference type; writing one is a type error
([References and mutability](references.md) 9.7).

## 3.3 Entry point

<a class="vx-rule" id="decl.entry.exactly-one-main" href="#decl.entry.exactly-one-main">decl.entry.exactly-one-main</a> An executable program must define exactly one function named `main`. Its v0.1
signature is:

```vortex
// program: valid
fn main() {
    // program body
}
```

`main` has no parameters and returns `void`, either implicitly or through an
explicit `-> void` annotation. The parser accepts any function named `main`.
Name resolution checks the entry point once it has collected the top-level
names:

- <a class="vx-rule" id="decl.entry.missing-main" href="#decl.entry.missing-main">decl.entry.missing-main</a> A program with no function named `main` is a semantic error, reported at the
  start of the file (line 1, column 1).
- <a class="vx-rule" id="decl.entry.invalid-signature" href="#decl.entry.invalid-signature">decl.entry.invalid-signature</a> A `main` with one or more parameters, or with a written return type other
  than `void`, is one semantic error at that declaration.
- <a class="vx-rule" id="decl.entry.duplicate-main" href="#decl.entry.duplicate-main">decl.entry.duplicate-main</a> A second function named `main` is a duplicate declaration. It is reported
  once, as a name error at the second declaration, and does not also produce
  an entry-point error.

<a class="vx-rule" id="decl.entry.ordinary-function" href="#decl.entry.ordinary-function">decl.entry.ordinary-function</a> In every other respect `main` is an ordinary function: other functions may
call it, and it may call itself.

```vortex
// program: semantic error
fn main(count: i32) { }   // semantic error: main takes no parameters
```

See the [decision record](../decisions/program.md#d6).

## 3.4 Struct declarations

```ebnf
struct_definition ::=
    "struct", identifier, "{",
    struct_field, { ",", struct_field }, [ "," ],
    "}" ;

struct_field ::=
    identifier, ":", type ;
```

<a class="vx-rule" id="decl.structs.preserved-fields" href="#decl.structs.preserved-fields">decl.structs.preserved-fields</a> A struct declaration preserves its name, source location, and ordered field
declarations. Each field preserves its own name, type, and location.

```vortex
// items: valid
struct Point {
    x: f32,
    y: f32,
}
```

<a class="vx-rule" id="decl.structs.trailing-comma" href="#decl.structs.trailing-comma">decl.structs.trailing-comma</a> A trailing field comma is accepted:

```vortex
// items: valid
struct Pair { left: i32, right: i32, }
```

<a class="vx-rule" id="decl.structs.at-least-one-field" href="#decl.structs.at-least-one-field">decl.structs.at-least-one-field</a> A struct must declare at least one field, so an empty field list is a syntax
error ([why](../decisions/operators.md#d26)):

```vortex
// items: syntax error
struct Marker {} // a struct needs at least one field
```

<a class="vx-rule" id="decl.structs.field-constraints" href="#decl.structs.field-constraints">decl.structs.field-constraints</a> Field names must be unique. Field defaults, methods, visibility modifiers,
inheritance, and generic fields are outside v0.1.

## 3.5 Local variable declarations

<a class="vx-rule" id="decl.locals.statement-form" href="#decl.locals.statement-form">decl.locals.statement-form</a> Local variables are declaration statements rather than top-level
declarations:

```ebnf
variable_declaration ::=
    "let", [ "mut" ], identifier,
    [ ":", type ],
    "=", expression, ";" ;
```

<a class="vx-rule" id="decl.locals.initializer-required" href="#decl.locals.initializer-required">decl.locals.initializer-required</a> Every local declaration has an initializer. The type annotation is optional;
when omitted, the type is inferred from the initializer.

```vortex
// statements: valid
let width = 128;
let mut total: f32 = 0.0;
```

<a class="vx-rule" id="decl.locals.mutability-and-references" href="#decl.locals.mutability-and-references">decl.locals.mutability-and-references</a> The declaration preserves the name, `mut` flag, optional written type,
initializer, and complete source location. A declaration without `mut` creates
immutable storage. A local whose type is a reference type must be declared
without `mut`, and its initializer must be a reference expression;
`let mut shared = &value;` is a type error
([References and mutability](references.md) 9.7). Decision record:
[Where references may appear, and how long a borrow lasts](../decisions/references.md#d41).

## 3.6 Scopes

Vortex v0.1 has these scope categories:

- <a class="vx-rule" id="decl.scopes.builtin-scope" href="#decl.scopes.builtin-scope">decl.scopes.builtin-scope</a> a built-in scope surrounds the program scope and contains `print`
  ([3.9](#39-built-in-functions));
- <a class="vx-rule" id="decl.scopes.program-scope" href="#decl.scopes.program-scope">decl.scopes.program-scope</a> the program scope contains every function and struct name in one namespace;
- <a class="vx-rule" id="decl.scopes.function-scope" href="#decl.scopes.function-scope">decl.scopes.function-scope</a> a function scope contains its parameter names and the local variables
  declared directly in the function's body block; that block does not open a
  second scope;
- <a class="vx-rule" id="decl.scopes.nested-block-scope" href="#decl.scopes.nested-block-scope">decl.scopes.nested-block-scope</a> every other block creates a nested local scope; the scope of a `for` loop's
  body block also contains the loop variable;
- <a class="vx-rule" id="decl.scopes.struct-field-namespace" href="#decl.scopes.struct-field-namespace">decl.scopes.struct-field-namespace</a> a struct has a field namespace selected through a value of that type.

<a class="vx-rule" id="decl.scopes.visibility-extents" href="#decl.scopes.visibility-extents">decl.scopes.visibility-extents</a> A top-level function or struct name is visible throughout the source file
([3.1](#31-program-structure)). A local variable becomes visible after its
complete declaration, including its initializer, and remains visible until the
end of its block. A parameter is visible in the whole function body. A loop
variable is visible only in the loop body. It is immutable, so assigning to it
is a semantic error ([Statements 6.8](statements.md#68-for-loops)).

<a class="vx-rule" id="decl.scopes.no-shadowing" href="#decl.scopes.no-shadowing">decl.scopes.no-shadowing</a> Names declared in the same scope must be distinct. A declaration also must not
have the same name as any other declaration that is visible where it appears:
Vortex has no shadowing, so an inner declaration never hides an outer one.
Because `print` and every top-level function and struct are visible
everywhere, no parameter, local variable or loop variable may take one of
their names. Declarations that are never visible at the same point may share a
name, such as locals in two sibling blocks or the variables of two consecutive
`for` loops. A violation of any rule in this paragraph is a name error
([decision record](../decisions/names.md#d2)).

<a class="vx-rule" id="decl.scopes.top-level-uniqueness" href="#decl.scopes.top-level-uniqueness">decl.scopes.top-level-uniqueness</a> Two top-level declarations must not have the same name, whatever their kinds,
so `struct Point` and `fn Point` cannot appear in the same program, and no
top-level declaration may be named `print`. A clash is a name error
([decision record](../decisions/names.md#d5)). Field names are not in the
program scope: each struct's fields form their own namespace, reached through
a value of that struct type, so a field may have the same name as a function,
a struct, a parameter or a local variable.

## 3.7 Name resolution

<a class="vx-rule" id="decl.resolution.deferred-binding" href="#decl.resolution.deferred-binding">decl.resolution.deferred-binding</a> The parser preserves identifier spelling without resolving it. Name resolution
must later connect each name expression and named type to one visible
declaration.

The following source parses but fails name resolution:

```vortex
// program: name error
fn main() {
    print(missing_value);
}
```

<a class="vx-rule" id="decl.resolution.diagnostics" href="#decl.resolution.diagnostics">decl.resolution.diagnostics</a> Name resolution must diagnose unknown names, duplicate declarations,
declarations that reuse a visible name, invalid scope use, and unresolved
named types with the relevant source location.

## 3.8 Excluded declaration forms

<a class="vx-rule" id="decl.excluded.v01-scope" href="#decl.excluded.v01-scope">decl.excluded.v01-scope</a> Vortex v0.1 has no modules, imports, namespace declarations, global variables,
type aliases, constants, enums, classes, traits, interfaces, generics, methods,
overload sets, or separate function prototypes. Because v0.1 has no constants,
an array dimension may contain only integer literals and arithmetic
([Arrays and shapes, 7.2](arrays.md#72-dimension-rules)). Constant
declarations are planned as the first addition after v0.1
([decision 11](../decisions/arrays.md#d11)).

See [Structs](structs.md) for struct value construction and
[Statements](statements.md) for local declaration execution.

## 3.9 Built-in functions

<a class="vx-rule" id="decl.builtins.print-declaration" href="#decl.builtins.print-declaration">decl.builtins.print-declaration</a> Vortex v0.1 has one built-in function, `print`. It is declared in a built-in
scope that surrounds the program scope, so every program can call it without
declaring it. Declaring anything named `print` is a name error (see
[3.6 Scopes](#36-scopes)).

<a class="vx-rule" id="decl.builtins.print-arguments" href="#decl.builtins.print-arguments">decl.builtins.print-arguments</a> `print` takes one or more arguments and returns `void`. Each argument must have
type `bool`, `char`, `i32`, `u32`, `usize`, `f32`, `f64` or `String`. A call
with no arguments, or with an argument of any other type, is a type error; this
includes a `void` call, an array, a struct and a reference such as `&total`. A
name of reference type used as an argument reads its referent, so `print(r)`
prints the value `r` refers to ([References and mutability](references.md)).
`print` gives its arguments no expected type, so a literal argument has its
default type.

<a class="vx-rule" id="decl.builtins.print-output" href="#decl.builtins.print-output">decl.builtins.print-output</a> The arguments are evaluated from left to right. `print` then writes the text of
each argument to standard output, with one space (U+0020) between consecutive
arguments and one line feed (U+000A) after the last.

| Argument type | Text written |
| --- | --- |
| `i32`, `u32`, `usize` | Decimal digits, preceded by `-` when the value is negative |
| `bool` | `true` or `false` |
| `char` | The character's UTF-8 encoding |
| `String` | The string's UTF-8 bytes, unchanged and without quotes |
| `f32`, `f64` | As described below |

<a class="vx-rule" id="decl.builtins.print-float-format" href="#decl.builtins.print-float-format">decl.builtins.print-float-format</a> A finite floating-point value is written as the shortest decimal that converts
back to the same value of the argument's type; if several decimals of that
length qualify, the one closest to the exact value is used. Let e be that
decimal's exponent in scientific notation, so `16.0` has e = 1 and `0.001` has
e = -3. When e is from -4 to 15, the value is written in positional form, such
as `16.0`, `0.1` or `0.0001`. Otherwise it is written in exponent form, such as
`1.0e16` or `2.5e-7`: the significant digits with a point after the first, a
lowercase `e`, and the exponent in decimal with a `-` only when it is
negative. Both forms show at least one digit after the point. Zero is written
`0.0` and negative zero `-0.0`; any NaN is written `NaN`, and the infinities
`inf` and `-inf`.

```vortex
// statements: valid
print("rows:", 128);         // rows: 128
print(16.0, true, 1.0e20);   // 16.0 true 1.0e20
print('λ', "two words");     // λ two words
```

```vortex
// statements: type error
print();   // type error: print needs at least one argument
```

The [decision record](../decisions/program.md#d4) explains this design.
