# Hello world and program entry

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

Every executable Vortex program begins in `main`. In v0.1, `main` takes no
arguments and returns `void`. The `-> void` may be left out, as shown above.

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

A file may also contain helper functions and structs.

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

## Practice and self-check

**Question:** Is `fn main() -> void {}` different from `fn main() {}`?

**Answer:** No. An omitted return type means `void`.
