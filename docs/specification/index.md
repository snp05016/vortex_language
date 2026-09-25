# Vortex v0.1 specification

<div class="spec-meta">
  <div><strong>Status</strong>Working specification</div>
  <div><strong>Scope</strong>Vortex v0.1</div>
  <div><strong>Authority</strong>Language behavior, not implementation status</div>
</div>

This section defines the accepted source language and the obligations of a
conforming Vortex v0.1 implementation. It is organized like a language
reference: each chapter states syntax, static rules, required behavior,
examples, and relevant diagnostics.

The specification is intentionally separate from the compiler code. A rule can
be **specified** before it is **implemented**. Conversely, experimental compiler
behavior is not part of Vortex merely because the current executable accepts
it.

## Chapter map

| Chapter | Question answered |
| --- | --- |
| [1. Conformance and terminology](conformance.md) | Which document is authoritative, and what must an implementation do? |
| [2. Lexical structure](lexical-structure.md) | How do source characters become tokens? |
| [3. Programs and declarations](declarations.md) | Which definitions can appear, and where? |
| [4. Types and values](types-and-values.md) | Which value categories exist in v0.1? |
| [5. Expressions](expressions.md) | How are values formed and operators grouped? |
| [6. Statements and control flow](statements.md) | How does execution proceed through a block? |
| [7. Arrays and shapes](arrays.md) | How are fixed-size multidimensional values represented? |
| [8. Structs](structs.md) | How are named aggregate values declared and used? |
| [9. References and mutability](references.md) | Which storage can change, and how is it borrowed? |
| [10. Diagnostics](diagnostics.md) | Which invalid programs must be rejected? |
| [11. Formal grammar](grammar.md) | What is the exact EBNF accepted by the parser? |
| [12. Glossary](glossary.md) | What do recurring specification terms mean? |

## Rule identifiers

Each normative rule starts with a short identifier, such as
`arrays.dims.const`: the chapter, the topic, then the rule. Identifiers are
stable. A rule keeps its identifier when its wording is edited, and an
identifier is never reused for a different rule, so a test, a decision record
or a guide page can point at one exact rule with a link of the form
`arrays.md#<identifier>`.

## Normative vocabulary

The words **must**, **must not**, **required**, and **shall** state requirements.
The words **may** and **implementation-defined** permit an implementation
choice. The word **should** states guidance rather than a conformance
requirement.

Every `vortex` code block is an example. Its first line is a label, such as
`// statements: type error`, that names what the block contains and what a
conforming implementation must do with it;
[Specification examples](conformance.md#18-specification-examples) defines the
labels. A block labelled with an error is intentionally not a valid program.
The reasons are in [record 28](../decisions/documentation.md#d28).

## v0.1 language boundary

### Included

- statically typed values;
- local type inference from an initializer;
- explicit local mutability;
- primitive scalar types;
- fixed-size arrays with one or more dimensions;
- structs and field access;
- shared and mutable reference syntax;
- functions, calls, local bindings, assignment, and structured control flow;
- source locations and diagnostics sufficient to identify rejected source.

### Excluded

- modules and imports;
- enums, classes, traits, interfaces, and inheritance;
- tuples, unions, generics, type aliases, and function types;
- runtime-sized array types, slices, and vectors;
- exceptions, pattern matching, lambdas, and closures;
- user-visible raw pointers or unchecked memory operations;
- kernel-launch, GPU-grid, or scheduling syntax;
- an optimizer contract or performance guarantee.

Excluded features may appear in design documents as future work. They are not
accepted v0.1 syntax unless this specification is revised.

## Compiler phase boundaries

| Phase | Required responsibility |
| --- | --- |
| Lexing | Recognize tokens and reject malformed token text |
| Parsing | Validate grammatical structure and build a source-located AST |
| Name resolution | Connect names to declarations and enforce scope |
| Type checking | Determine types, evaluate array dimensions while resolving array types, and validate operations, calls, and assignments |
| Semantic checking | Enforce contextual rules such as mutability and loop-only statements |
| Constant evaluation | Check every checked operation whose deciding operands are integer constant expressions, and report those that fail |
| Lowering and runtime | Execute valid programs and perform every other required check at run time |

Array dimensions are evaluated during type checking because type equality
needs their values; a failing dimension is still a constant-evaluation error
([decision 52](../decisions/arrays.md#d52)).

A parser may accept a syntactically correct program that later phases reject.
For example, `[f32; rows + 1]` is a valid array-type shape, but the type
checker must reject it with a constant-evaluation error, because `rows` is a
name and a dimension must be an integer constant expression
([decision 11](../decisions/arrays.md#d11)).

## Reading order

First-time readers should read Chapters 1 through 6, then the array, struct,
and reference chapters. Compiler implementers should keep the
[formal grammar](grammar.md), [parser design](../compiler/parser-design.md),
and [diagnostic taxonomy](diagnostics.md) open together.
