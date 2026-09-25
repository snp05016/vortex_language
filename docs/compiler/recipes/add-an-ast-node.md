# How to add an AST node

<p class="page-intro">Use this when a Vortex construct needs a tree shape your compiler does not build yet: a new kind of expression, statement, declaration or type. It walks through one node from what it stores to every later pass that has to understand it, so you add it once and correctly rather than patching each stage after a test fails.</p>

## Before you start

- [The AST learning guide](../ast-guide.md): you can say, for the category your
  node belongs to, why a heterogeneous child is a `std::unique_ptr` and not a
  value member, and why the base categories `Expr`, `Stmt`, `Decl` and `Type`
  exist at all.
- [Stage 3: The parser and the syntax tree](../guide/stage-3-parser-and-tree.md):
  your parser already builds and owns at least one node in the same category,
  and your tree printer already prints it. You are about to copy that pattern,
  not invent one.
- [Compiler architecture](../architecture.md): you can name which pass owns
  which check, so a stage-5 rule does not end up living in the parser.
- If the node introduces or uses a name, or produces a value: the matching
  pass already handles at least one node of the same kind. [Stage 4: Names and
  scopes](../guide/stage-4-names-and-scopes.md) for anything declared or
  looked up; [Stage 5: Types and language rules](../guide/stage-5-types-and-rules.md)
  for anything that needs a type or a semantic check.

## Steps

1. **Find the rule in the grammar and settle its category.**
   Look up the construct in the [formal grammar](../../specification/grammar.md)
   before writing anything. The grammar already says whether it is an
   expression, a statement, a declaration or a type, and where it sits among
   the productions that contain it. If it is an operator, read off its
   precedence level and associativity from the
   [expressions chapter](../../specification/expressions.md#52-precedence-and-associativity):
   you will need both when you wire it into the parser. Deciding the category
   first keeps the new node inside one of the four bases the rest of the
   front end already understands, per
   [architecture: frontend invariants](../architecture.md#frontend-invariants).

2. **Decide what the node stores, and who owns each part.**
   List every piece of information the construct carries. Plain data that has
   only one possible shape (an operator enum, a name, a boolean such as
   whether `mut` was written) lives directly in the node. Anything that could
   be one of several concrete node types, such as an operand or an argument,
   needs a `std::unique_ptr` to the base category, never a by-value member of
   that base: a by-value `Expr` field slices off whatever derived data the
   real operand has ([AST guide, section 5](../ast-guide.md#5-why-child-expressions-cannot-be-stored-as-expr-values)).
   A list of heterogeneous children is
   `std::vector<std::unique_ptr<Expr>>`, not `std::vector<Expr>`, for the same
   reason. If a child is optional (a `return` with no expression, an `else`
   with no branch), document what a null pointer means for that field before
   you write the constructor
   ([parser design, ownership rules](../parser-design.md#15-ownership-and-constructors)).
   Give the constructor everything it needs to build a complete, valid node in
   one call, so a half-built node can never exist.

3. **Give it a source location.**
   Every node needs enough of a source span for a later diagnostic to point at
   it ([architecture: frontend invariants](../architecture.md#frontend-invariants)).
   Use the first token of the construct as the starting point, following the
   table in [parser design, section 13](../parser-design.md#13-source-locations):
   `fn` for a function, the opening `[` for an array, the left operand or the
   operator (pick one and keep it consistent) for a binary expression. For a
   node built from other nodes, its span should cover the whole construct, not
   only the token that names it: the span of a binary expression covers both
   operands, not only the operator
   ([AST guide, locations belong to complete constructs](../ast-guide.md#7-locations-belong-to-complete-constructs)).

4. **Add the parser rule that builds it, and only that.**
   Write or extend the one parser function responsible for this grammar rule,
   following the grammar-to-function map in
   [parser design, section 5](../parser-design.md#5-source-grammar-to-parser-function-map).
   If the construct can be mistaken for another one from its first tokens
   alone, work out how the parser tells them apart before you write the
   branch: [stage 3 lists the recurring cases](../guide/stage-3-parser-and-tree.md#places-where-the-grammar-needs-care)
   in Vortex (assignment versus a call, the three meanings of a leading `[`, a
   struct expression versus a block, a cast versus a call). Slot an operator
   into the existing precedence climb or table at the level you read off in
   step 1; do not add a second, competing way of parsing expressions
   ([parser design, section 5](../parser-design.md#5-source-grammar-to-parser-function-map)).
   The parser checks only that the shape is legal. It must not reject the
   construct for a reason that depends on a name's declaration or a value's
   type, and it must not fold, simplify or evaluate anything it has parsed
   ([stage 3: the parser checks form, not meaning](../guide/stage-3-parser-and-tree.md#the-parser-checks-form-not-meaning)).
   Decide, too, whether an error partway through this construct needs its own
   synchronization point or can rely on the existing ones (`;`, `}`, `fn`,
   `struct`); do not skip past a closing delimiter without tracking how many
   are open ([stage 3, when the source is wrong](../guide/stage-3-parser-and-tree.md#when-the-source-is-wrong)).

5. **Extend the tree printer.**
   The printer is how you and every test will look at the node, so it needs
   to show the node's kind, everything it stores, its children in order, and
   its position, in the same indented S-expression form as every other node:
   the kind first, then its stored data and `@line:column`, each child
   indented two more spaces from its parent
   ([implementation choice I5](../../decisions/implementation.md#i5)). Confirm
   the output is stable: parsing the same source twice must print exactly the
   same text, with nothing that varies between runs, such as a pointer address
   or the order of an unordered container
   ([stage 3, printing the tree](../guide/stage-3-parser-and-tree.md#printing-the-tree)).
   A stable printout is what lets you save it next to a test program and
   compare it on every later run.

6. **Handle it in name resolution, if it introduces or uses a name.**
   If the node declares a name, add it to the current scope the way the
   existing declarations are added, and reject a name that is already visible
   there, since Vortex has no shadowing
   ([stage 4: naming decisions](../guide/stage-4-names-and-scopes.md#decisions-vortex-has-not-made-yet)).
   If it is a top-level declaration, make sure it is recorded before any use is
   resolved, so a forward reference to it still works
   ([stage 4: order of top-level declarations](../guide/stage-4-names-and-scopes.md#order-of-top-level-declarations)).
   If it uses a name, resolve it outward through the enclosing scopes and link
   it to the declaration you find, or report an unknown name if you reach the
   built-in scope without one
   ([stage 4: scopes nest](../guide/stage-4-names-and-scopes.md#scopes-nest)).
   Record what kind of declaration it is (function, struct, parameter, local,
   loop variable), because stage 5 needs that to reject a struct used as a
   callee or a function used as a value
   ([stage 4: names that wait for types](../guide/stage-4-names-and-scopes.md#names-that-wait-for-types)).
   Leave alone anything that depends on a type to resolve, such as a field
   name after a `.`: this stage resolves only the name in front of the dot.

7. **Handle it in type checking and semantic checks, if it produces or
   consumes a value.**
   Work out the node's type from the types of its children, the same way
   every other node's type travels from the leaves upward, and reject
   combinations no rule allows rather than converting one type into another:
   Vortex has no implicit conversions
   ([stage 5: how types travel up an expression](../guide/stage-5-types-and-rules.md#how-types-travel-up-an-expression)).
   If the node is a literal, work out its type from its peer, then the
   expected type of its position, then its default kind, in that order
   ([stage 5: literals and the types around them](../guide/stage-5-types-and-rules.md#literals-and-the-types-around-them)).
   If the construct also carries a rule that is not about types, such as
   mutability, whether it is only valid inside a loop, or whether it changes
   how a function body can terminate, add that as a semantic check rather than
   folding it into the type rule: the two are different diagnostic categories
   even when the same node triggers both
   ([stage 5: the checks, one kind at a time](../guide/stage-5-types-and-rules.md#the-checks-one-kind-at-a-time)).
   If the node can appear where the language needs a compile-time value, such
   as an array dimension, evaluate it as part of resolving that type and
   report a failure as a constant-evaluation error, not a type error
   ([stage 5: array dimensions and constant evaluation](../guide/stage-5-types-and-rules.md#array-dimensions-and-constant-evaluation)).
   A program the gate rejects must never reach lowering
   ([stage 5: the gate](../guide/stage-5-types-and-rules.md#the-gate)).

8. **Add a test at every stage that now touches the node.**
   [Architecture: testing strategy](../architecture.md#testing-strategy) gives
   the shape of each one. A parser test asserts the tree's shape, its
   ownership, the order of its children and their source locations, not only
   that parsing returned. A name or type test pairs one program the new rule
   accepts with the nearest program it must reject, in the style of the table
   in [stage 5](../guide/stage-5-types-and-rules.md#a-negative-test-for-every-rule).
   If the node can fail constant evaluation, add a case with constant operands
   next to the same construct with a variable, since only the constant case is
   rejected at this stage
   ([stage 5: array dimensions and constant evaluation](../guide/stage-5-types-and-rules.md#array-dimensions-and-constant-evaluation)).
   Label each Vortex example you write with its exact expected category, using
   the labels the [documentation decision](../../decisions/documentation.md#d28)
   defines (`valid`, `syntax error`, `name error`, `type error`, `semantic
   error`, `constant-evaluation error`), so the example doubles as a test case
   later.

## Check that it worked

Parse a source file that exercises the new node and print its tree with
`vortex <source> --ast` ([decision 20](../../decisions/program.md#d20)). Check
by eye that the printed node shows its kind, every field you listed in step 2,
its children in the order they were written, and a source position that
points at the right token. Run the same file twice and confirm the two
printouts are identical, character for character: that is what "stable" means
for this printer.

Then run your test runner and confirm every new test passes alongside the
existing ones from earlier stages: a new node must never break a tree shape,
a name resolution or a type test that passed before you started. If the node
reaches name resolution or type checking, confirm the paired accepted and
rejected programs from step 8 both behave as labelled, and that the rejected
one is reported at the correct pass: a program that is wrong in shape must
fail during parsing, and a program that is wrong in meaning must still parse
and fail one stage later.

## Related

- [The AST learning guide](../ast-guide.md)
- [Compiler architecture](../architecture.md)
- [Parser design and implementation assignment](../parser-design.md)
- [Stage 3: The parser and the syntax tree](../guide/stage-3-parser-and-tree.md)
- [Stage 4: Names and scopes](../guide/stage-4-names-and-scopes.md)
- [Stage 5: Types and language rules](../guide/stage-5-types-and-rules.md)
- [Formal grammar](../../specification/grammar.md)
- [Add a token](add-a-token.md)
- [Add a diagnostic](add-a-diagnostic.md)
