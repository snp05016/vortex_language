# How to add a diagnostic

<p class="page-intro">Use this when the specification states a rule that your compiler does not yet reject, or accepts but reports through the wrong category or from the wrong pass. It walks through what a diagnostic must carry, where it belongs, and the test that proves it fires.</p>

## Before you start

This assumes the parts of the compiler below already exist. Add a diagnostic
for a rule your compiler does not check yet; do not use it to invent a rule
the specification does not state.

- The diagnostic format and source manager from
  [Stage 1: source text and error messages](../guide/stage-1-source-and-diagnostics.md)
  are working: every diagnostic already goes through one format, with a
  category, a message, a primary span, and the source line and marker printed
  from that span.
- The pass that will detect the new rule already produces the input it needs:
  tokens with spans for a lexical rule
  ([Stage 2](../guide/stage-2-lexer.md)), a syntax tree with spans for a
  syntax rule ([Stage 3](../guide/stage-3-parser-and-tree.md)), or resolved,
  typed declarations for a name, type, semantic or constant-evaluation rule
  ([Stage 5](../guide/stage-5-types-and-rules.md)).
- You can point at the exact sentence or list item in the specification that
  states the rule. If you cannot, the rule is not settled yet and this recipe
  is the wrong tool.

## Steps

1. **Find the rule and read it in context.** Locate the sentence or list item
   in the relevant [specification](../../specification/index.md) chapter, and
   read the [decision record](../../decisions/index.md) it cites, if any: the
   record usually explains why the rule exists and what alternative was
   rejected, which keeps the message from contradicting the reasoning. If the
   specification page carries `vx-rule` identifiers, note the rule's ID; the
   diagnostic you add should be traceable back to it, even if only in a code
   comment or test name.

2. **Choose the category.** [Diagnostics 10.2](../../specification/diagnostics.md#102-categories)
   lists exactly eight: lexical, syntax, name, type, semantic,
   constant-evaluation, runtime, and implementation-limit. Match the rule's
   shape to a category, not to the pass that happens to notice it first:

     - the source cannot form a valid token: lexical;
     - the token sequence does not match the grammar: syntax;
     - a name is unknown, duplicated, or out of scope: name;
     - operand, initializer, argument, or return types disagree, or a literal
       does not fit its type: type;
     - the syntax and local types are valid but a contextual rule fails, such
       as assigning to an immutable place or using `break` outside a loop:
       semantic;
     - a compile-time-required expression cannot be evaluated, such as an
       array dimension that is not a constant, or a checked operation whose
       *every* deciding operand is a constant expression: constant-evaluation;
     - a checked operation fails on values only known while the program runs:
       runtime;
     - specified behavior the compiler has not implemented yet, or a
       documented implementation limit exceeded: implementation-limit.

   [Stage 5's table of rules](../guide/stage-5-types-and-rules.md#a-negative-test-for-every-rule)
   works through this choice for every static rule in v0.1 and is worth
   checking against before deciding. Watch for the one rule that can fall into
   two categories depending on its operands, described next.

3. **Choose the stage that reports it, and check for the constant-versus-runtime split.**
   [Architecture: pass contracts](../architecture.md#pass-contracts) lists
   what each pass consumes and produces; the stage that reports a diagnostic
   is the one whose contract already holds the information the rule needs,
   not an earlier or later one. A missing field in a struct literal cannot be
   caught by the parser, because the parser does not know what fields the
   struct declares. An unknown identifier cannot be caught by the type
   checker alone, because by then name resolution should already have
   rejected it.

   [Diagnostics 10.3](../../specification/diagnostics.md#103-error-phase-versus-category)
   describes one rule that reports through two categories depending on where
   its deciding values come from: an out-of-bounds array index is a
   constant-evaluation error when every operand deciding the index is an
   integer constant expression, and a runtime error otherwise, even though
   both check the same bound
   ([decision 39](../../decisions/diagnostics.md#d39)). Before wiring a new
   diagnostic into one pass, ask whether its rule has the same shape: a check
   that is sometimes decidable at compile time and always required at run
   time when it is not. If so, it needs two reporting paths, not one, and a
   test for each.

4. **List the data the diagnostic must carry.** [Diagnostics 10.1](../../specification/diagnostics.md#101-required-diagnostic-data)
   sets the minimum: the category, a concise primary message, a primary
   source span, the unexpected or invalid construct, the expected form or
   violated rule when that helps, and room for an optional note pointing at a
   related span (a conflicting declaration, the place a borrow started, the
   type a value was expected to match). Decide, for this rule, which of these
   are useful; a struct-literal error naming a missing field needs the field
   name in the message, an immutable-assignment error benefits from a note at
   the `let` that omitted `mut`. Do not encode any of this wording into an
   AST node constructor or a token-handling routine
   ([Architecture: diagnostics](../architecture.md#diagnostics)); the message
   belongs with the pass that detects the rule, not with the data structures
   the rule happens to inspect.

5. **Write the message and pick the primary span.** State the language rule
   that failed, in terms the programmer used, not the pass that caught it:
   "cannot assign to immutable variable `value`", not "assignment to
   read-only lvalue". [Architecture: separation of concerns](../architecture.md#separation-of-concerns)
   and [Diagnostics 10.3](../../specification/diagnostics.md#103-error-phase-versus-category)
   both make this point. For the span, follow
   [Conformance 1.7](../../specification/conformance.md#17-source-locations):
   a compound construct's span covers the whole construct (`left + right`
   covers both operands and the operator), while a child keeps its own
   narrower span for use in a note. Point the primary span at the smallest
   piece of source that shows the mistake: the offending index, not the whole
   subscript expression; the mismatched initializer, not the whole
   declaration.

6. **Write a minimal rejected program and a nearby accepted one.**
   [Diagnostics 10.7](../../specification/diagnostics.md#107-verification-requirements)
   asks for both, for every diagnostic rule. The rejected program should show
   nothing except the one mistake; the accepted program should be as close to
   it as possible while breaking no rule, so the pair proves the compiler
   rejects the broken form and not a wider, valid one. `git log --oneline -- docs/decisions/`
   for the rule's decision record is a fast way to check whether the record
   already gives you both, or a form close enough to adapt.

7. **Write the golden test.** Save the rejected program as a test input, run
   it through your compiler exactly as a user would
   ([the command-line decision](../../decisions/program.md#d20)), and record
   what a passing run must match: whichever choice your compiler documented
   in [implementation choice I3](../../decisions/implementation.md#i3), either
   the whole message or, more commonly, the category and the position where
   the primary span starts, plus the exit status. A rule caught before
   execution exits 1; a rule caught only at run time exits 101, with the one
   line to standard error that
   [Diagnostics 10.6](../../specification/diagnostics.md#106-runtime-reporting)
   specifies, in the form `runtime error[<kind>]: <message> at <file>:<line>:<column>`,
   after every earlier `print` call's output. Compare the two files exactly,
   for example:

   ```sh
   diff expected/bool_from_int.out <(vortex tests/invalid/bool_from_int.vx 2>&1 1>/dev/null; echo "exit:$?")
   ```

   Then run the accepted program from step 6 the same way, and confirm it
   exits 0 with no diagnostic. A test that only checks "the compiler failed"
   passes even when the compiler fails for the wrong reason, such as a crash,
   so the category and the position are what make it a real test of this
   rule.

8. **Choose the specification example label.** If this diagnostic corresponds
   to a `vortex` code block in the specification, or you are adding one, its
   first line must be a label of the form `// <kind>: <result>`
   ([Conformance 1.8](../../specification/conformance.md#18-specification-examples)).
   Pick `statements` for a block that only needs `fn main() { ... }` around
   it, `items` for top-level declarations, or `program` when the rule is
   about the whole file, such as the ban on top-level statements or the
   requirement for `main`; a block that shows more than one category of
   mistake is not valid, so split it. The result half of the label must name
   the category you chose in step 2, exactly as
   [Diagnostics 10.4](../../specification/diagnostics.md#104-examples) writes
   it, for example `// statements: constant-evaluation error`. This label is
   itself part of the test suite: a checker reads it and expects that exact
   outcome, so a wrong label is a wrong test.

9. **Run the whole suite, not only the new test.** A new diagnostic can
   change how the compiler treats programs that used to reach a later pass,
   so a program that previously failed for a different reason may now stop
   earlier, at this new check, with a different category. Run every earlier
   stage's tests before calling the diagnostic finished, exactly as the
   [roadmap](../../roadmap.md#how-to-use-this-roadmap) asks for every
   milestone.

## Check that it worked

- The rejected program from step 6 exits with the status from step 7 (1 for a
  compile-time category, 101 for a runtime one), and the diagnostic's
  category and primary-span position match what you recorded, byte for byte
  against the saved expected file.
- The nearby accepted program from step 6 exits 0 and prints only what its
  own `print` calls produce.
- If the rule has the constant-versus-runtime split from step 3, both the
  constant form (rejected at compile time, exit 1) and the variable form
  (accepted, then rejected or successful at run time, exit 101 or 0) have
  their own test, and neither is missing.
- The full existing test suite still passes: no program that used to be
  accepted, or used to fail with a different category or position, changed
  behavior as a side effect.
- Any `vortex` example block you added or pointed at carries a label whose
  result matches the category from step 2, and, if the specification page
  uses `vx-rule` identifiers, the rule text it documents is unchanged.

## Related

- [Stage 1: source text and error messages](../guide/stage-1-source-and-diagnostics.md):
  the one diagnostic format every stage reuses.
- [Stage 2: the lexer](../guide/stage-2-lexer.md#errors-the-lexer-reports):
  the full list of lexical errors.
- [Stage 3: the parser and the syntax tree](../guide/stage-3-parser-and-tree.md#when-the-source-is-wrong):
  recovering after a syntax error without cascading diagnostics.
- [Stage 5: types and language rules](../guide/stage-5-types-and-rules.md#a-negative-test-for-every-rule):
  the full table of static rules and their categories.
- [Architecture: pass contracts](../architecture.md#pass-contracts) and
  [Architecture: diagnostics](../architecture.md#diagnostics).
- [Specification: Diagnostics](../../specification/diagnostics.md).
- [Specification: Conformance, 1.7 Source locations](../../specification/conformance.md#17-source-locations)
  and [1.8 Specification examples](../../specification/conformance.md#18-specification-examples).
</content>
