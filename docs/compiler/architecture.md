# Compiler architecture

The Vortex compiler should be organized as a sequence of small, inspectable
passes. Each pass consumes one representation, checks one class of rules, and
produces either a more informed representation or a diagnostic.

## Pipeline

```text
Vortex source
    -> source manager
    -> lexer
    -> token stream
    -> parser
    -> abstract syntax tree
    -> name resolution
    -> type and semantic checking (array dimensions are evaluated here)
    -> constant evaluation (checked operations on constant operands)
    -> intermediate representation
    -> CPU lowering
    -> executable program
```

GPU lowering and scheduling are later work. They must reuse the same validated
language semantics rather than creating a second interpretation of Vortex.

## Pass contracts

| Pass | Input | Output | Must not do |
| --- | --- | --- | --- |
| Source manager | File path or source buffer | Stable source storage and locations | Interpret language syntax |
| Lexer | Source characters | Tokens with spelling and spans | Resolve names or types |
| Parser | Token stream | Owned, source-located AST | Perform final type checking |
| Name resolver | AST plus scopes | Declaration links or symbol information | Generate machine code |
| Type checker | Resolved AST | Types, evaluated array dimensions, and type diagnostics | Change source meaning for optimization |
| Semantic checker | Typed AST and context | Validated control flow, mutation, and references | Hide unsupported constructs |
| Constant evaluator | Typed checked operations whose deciding operands are integer constant expressions | Compile-time results of those operations, and a constant-evaluation error for each one that fails | Evaluate names, calls, or other runtime behavior |
| Lowering | Validated program | Intermediate and target representation | Accept programs rejected by earlier phases |

Array dimensions are evaluated while the type checker resolves array types,
because type equality needs their values; their failures are still
constant-evaluation errors ([decision](../decisions/arrays.md#d52)).

## Frontend invariants

The frontend must preserve:

- the order of top-level declarations, block statements, arguments, fields,
  array elements, indices, and dimensions;
- the complete operator selected by the source;
- distinct nodes for distinct type and expression categories;
- enough source span information for precise diagnostics;
- one clear owner for every polymorphic AST child.

Parser functions should return `std::unique_ptr<Expr>`,
`std::unique_ptr<Stmt>`, `std::unique_ptr<Decl>`, or
`std::unique_ptr<Type>` when the concrete derived node varies. Storing a
derived expression in a plain `Expr` value slices away its derived data.

## Separation of concerns

The parser checks form. It does not reject a syntactically valid name merely
because the name has not been declared, and it does not decide whether a
binary operator supports the operand types.

```vortex
// fragment
let value: MissingType = unknown_name + true;
```

This declaration has valid grammatical structure. Name resolution and type
checking must diagnose the unknown names and invalid operation.

## Diagnostics

All passes should report through one diagnostic model containing:

- a stable diagnostic category;
- a clear primary message;
- the primary source span;
- optional related spans or notes;
- enough context to distinguish the eight
  [diagnostic categories](../specification/diagnostics.md#102-categories):
  lexical, syntax, name, type, semantic, constant-evaluation, runtime, and
  implementation-limit errors.

Do not encode user-facing diagnostic wording into AST node constructors or
token movement helpers.

## Testing strategy

Each pass needs both local and pipeline tests:

1. Lexer tests assert kinds, spellings, and spans.
2. Parser tests assert tree shape, ownership, ordering, and source locations.
3. Name and type tests pair one accepted program with the nearest rejected
   form.
4. Constant-evaluation tests separate syntactically valid dimensions from
   dimensions that fail compile-time requirements, and pair each checked
   operation with constant operands, such as `10 / 0`, with the same operation
   on variables, which must compile and fail at run time
   ([decision](../decisions/diagnostics.md#d39)).
5. Lowering tests compare observable output for valid programs.
6. End-to-end tests verify diagnostics and executable behavior.

A build that succeeds is not evidence that every AST node is usable. Headers
must be compiled directly or included by dedicated tests, and tree tests must
inspect derived node data rather than checking only that parsing returned.

## Current implementation boundary

The repository currently has a working lexer test path and an AST and parser
under active development. The detailed frontend documents describe the target
shape. Consult the [roadmap](../roadmap.md) and executable tests before treating
a specification chapter as an implementation claim.

## Related documents

- [Building the Vortex compiler](guide/index.md), a stage-by-stage reading guide
- [AST learning guide](ast-guide.md)
- [Parser design](parser-design.md)
- [Formal grammar](../specification/grammar.md)
- [Implementation roadmap](../roadmap.md)
