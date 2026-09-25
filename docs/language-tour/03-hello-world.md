# Hello world and program entry

--8<-- "includes/remember/language-tour__03-hello-world.md"

## Learning goals

After this chapter, you should know the required executable entry point and the
difference between parsing a function and validating `main`.

## Smallest useful program

```vortex
// program: valid
fn main() {
    print("Hello, world!");
}
```

Every executable Vortex program begins in `main`, its **entry point** (the
function where execution starts). In v0.1, `main` takes no arguments and
returns `void`. The `-> void` may be left out, as shown above.

## Syntax

```text
fn main() [-> void] {
    statements
}
```

The brackets above explain that `-> void` is optional; they are not typed in a
Vortex program.

## Allowed

Both entry-point declarations are equivalent:

```vortex
// program: valid
fn main() {
    print("ready");
}
```

```vortex
// program: valid
fn main() -> void {
    print("ready");
}
```

A file may also contain helper functions and structs. Apart from the
entry-point requirement, nothing about `main`'s declaration is special.

??? check "Besides serving as the entry point, is `main` an ordinary function: can another function call it?"

    Yes. Other than the entry-point requirement, `main` behaves like any
    other function: other functions may call it, and it may call itself.

## Not allowed

```vortex
// program: semantic error
fn main(arguments: String) { }   // semantic error: v0.1 main takes no parameters
```

```vortex
// program: semantic error
fn main() -> i32 {   // semantic error: v0.1 main returns void
    return 0;
}
```

```vortex
// program: name error
fn main() { }
fn main() { }   // name error: a second function named main
```

A file containing only helper declarations may be syntactically parseable, but
without `main` it is not a valid executable program; the compiler reports a
semantic error.

??? check "A file has no function named `main` at all, but every other declaration is valid. What does the compiler report, and where?"

    A semantic error, reported at the start of the file (line 1, column 1).
    The rest of the file being valid does not change this: an executable
    program must define `main`.

## Compiler handling

<details markdown="1">
<summary>Which compiler stage enforces each rule (optional reading)</summary>

The parser (the [compiler stage](../compiler/guide/index.md) that checks how tokens fit together and builds the program's structure) builds ordinary function-declaration nodes for `main` and other
functions. Name resolution, the stage that links each name to its declaration,
then checks that exactly one function is named `main`, has no parameters, and
returns `void`. A missing `main` or a wrong signature is a semantic error, and a
second `main` is reported once, as a duplicate name
([entry point rules](../specification/declarations.md#33-entry-point),
[decision](../decisions/program.md#d6)). The runtime then begins execution at
that validated function.

</details>

??? check "Does the parser or name resolution reject `fn main(count: i32) {}`?"

    Name resolution. The parser accepts any function named `main` regardless
    of its signature; name resolution checks the parameter list and return
    type only after it has collected every top-level name.

## Practice and self-check

**Question:** Is `fn main() -> void {}` different from `fn main() {}`?

**Answer:** No. An omitted return type means `void`.

## Key ideas

!!! recap "Questions you can now answer"

    - **What must every executable Vortex program define?** Exactly one
      function named `main`, with no parameters, that returns `void`.
    - **Can `-> void` be left off a `main` declaration?** Yes. An omitted
      return type on `main` means `void`.
    - **What happens when a file has no function named `main`?** The
      compiler reports a semantic error at the start of the file (line 1,
      column 1).
    - **What diagnostic does a `main` with parameters or a non-`void` return
      type produce?** One semantic error, at that declaration.
    - **What diagnostic does a second `main` produce, and how many times?**
      One name error, reported once, at the second declaration; it does not
      also produce an entry-point error.
    - **Which compiler stage checks `main`'s parameter list and return
      type?** Name resolution, not the parser.
    - **Is `main` anything more than the entry point?** No more is required,
      but it is still an ordinary function: other functions may call it, and
      it may call itself.

## Where this comes back

--8<-- "includes/next/language-tour__03-hello-world.md"
