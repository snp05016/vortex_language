# Vortex v0.1 language tour

This guide is the practical, example-driven reference for writing Vortex v0.1
programs. Read it in order the first time. Later, use the tables below to jump
straight to a rule or an example.

The tour describes the language you can write. Two other documents describe
the same language more formally:

- The [formal grammar](../specification/grammar.md) is the exact set of rules
  the parser follows. (The **parser** is the part of the compiler that checks
  how the words and symbols of a program are arranged.)
- The [compiler documentation](../compiler/parser-design.md) explains how the
  compiler's front end (the stages that read and check your program) follows
  that grammar.

The tour explains the rules; it does not define them. The
[specification](../specification/index.md) chapters, including the formal
grammar, are the rules, and
[Document authority](../specification/conformance.md#11-document-authority)
says which page decides when two disagree. If the tour and the specification
disagree, the specification is right
([why](../decisions/documentation.md#d49)); please report the mismatch so it
can be fixed.

## How each chapter is organized

Each chapter follows the same simple pattern:

1. **Learning goals** state what you should understand after reading.
2. **Syntax** shows the accepted source form.
3. **Allowed** lists what v0.1 accepts.
4. **Not allowed** marks syntax or behavior that is outside v0.1.
5. **Valid examples** show programs the compiler should accept.
6. **Invalid examples** show programs the compiler should reject and why.
7. **Compiler handling** says which part of the compiler enforces the rule.
   This section is collapsed by default; open it only if you are curious.
8. **Practice and self-check** gives a short exercise with an answer.

An invalid example is wrong on purpose. Do not copy one into a program unless
you are testing the compiler's error messages (its **diagnostics**).

Every example starts with a comment that says what it is and what the compiler
does with it, such as `// statements: valid` or `// items: type error`.
`statements` means lines that belong inside a function body, `items` means
top-level declarations such as functions and structs, and `program` means a
whole file; `// fragment` marks a piece of syntax shown on its own, which is
never compiled.
[Specification examples](../specification/conformance.md#18-specification-examples)
lists every label ([why](../decisions/documentation.md#d28)).

## Guided reading order

The core language comes first. The last two chapters describe features that
are planned for later versions and are not part of v0.1.

| Part | Chapter | Use it to answer |
| --- | --- | --- |
| 1 | [Syntax philosophy](01-syntax-philosophy.md) | What kind of language is Vortex? |
| 2 | [Basic source rules](02-basic-source-rules.md) | How do names, comments, semicolons, and escapes work? |
| 3 | [Hello world and main](03-hello-world.md) | What must an executable program contain? |
| 4 | [Variables and types](04-variables-and-types.md) | How are values typed, stored, grouped, and referenced? |
| 5 | [Expressions](08-expressions.md) | Which pieces of syntax produce values? |
| 6 | [Statements](09-statements.md) | Which pieces of syntax perform actions and control flow? |
| 7 | [Declarations](10-declarations.md) | How are functions, structs, parameters, fields, and variables introduced? |
| 8 | [Runtime and numerical rules](06-runtime-and-numerical-rules.md) | Which numerical failures are checked, and when? |
| 9 | [Planned types](05-types-planned-for-later.md) | Which useful types are deliberately outside v0.1? |
| 10 | [Planned kernels and parallelism](07-kernels-and-parallel-execution.md) | Which accelerator features are planned but unavailable? |

The file names still carry their old numbers (for example, part 5 lives in
`08-expressions.md`) so that existing links keep working.

## Quick feature index

| Feature | Primary chapter |
| --- | --- |
| Arrays, dimensions, repeat arrays, indexing | [Variables and types](04-variables-and-types.md#fixed-size-arrays) and [Expressions](08-expressions.md#array-expressions) |
| Assignment and mutability | [Statements](09-statements.md#assignment-statements) |
| Binary and unary operators | [Expressions](08-expressions.md#unary-expressions) |
| Casts | [Expressions](08-expressions.md#cast-expressions) |
| Functions and `main` | [Declarations](10-declarations.md#function-declarations) |
| References | [Variables and types](04-variables-and-types.md#references) |
| Scope and names | [Declarations](10-declarations.md#names-and-scopes) |
| Struct definitions and values | [Declarations](10-declarations.md#struct-declarations) and [Expressions](08-expressions.md#struct-expressions-and-field-access) |
| `if`, loops, `break`, `continue`, and `return` | [Statements](09-statements.md) |
| Runtime errors and floating-point behavior | [Runtime and numerical rules](06-runtime-and-numerical-rules.md) |

## Compiler-stage legend

You do not need this table to write Vortex. It explains the stage names used
in each chapter's "Compiler handling" section. The
[compiler guide](../compiler/guide/index.md) explains each stage in depth.

| Stage | Responsibility |
| --- | --- |
| Lexer | Turns source characters into tokens and reports malformed tokens. |
| Parser | Checks grammatical structure and builds the AST (abstract syntax tree: a tree that records the structure of the program). |
| Name resolution | Connects each used name to a declaration and checks scope. |
| Type checking | Checks operand, argument, return, field, index, and assignment types, and evaluates array dimensions while reading array types ([decision](../decisions/arrays.md#d52)). |
| Constant evaluation | Checks operations whose deciding values are written with integer literals only, such as `10 / 0`, and rejects those that would fail ([decision](../decisions/diagnostics.md#d39)). |
| Runtime/code generation | Emits executable code, including the runtime checks for every other operation. |

## v0.1 boundary

The label **v0.1** means the small language described by the current grammar.
Features marked **planned for later** are design notes, not accepted syntax.
Keeping this boundary explicit prevents examples from promising compiler
behavior that has not been designed yet.
