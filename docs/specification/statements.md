# 6. Statements and control flow

A statement performs an action or changes control flow. Simple statements end
with `;`. Block-shaped statements do not take a trailing semicolon.

## 6.1 Statement forms

```ebnf
statement ::=
      variable_declaration
    | assignment_statement
    | return_statement
    | expression_statement
    | if_statement
    | while_statement
    | for_statement
    | break_statement
    | continue_statement
    | block ;
```

## 6.2 Blocks

<a class="vx-rule" id="stmt.blocks.scope" href="#stmt.blocks.scope">stmt.blocks.scope</a> A block contains zero or more statements and creates a nested local scope.

```ebnf
block ::=
    "{", { statement }, "}" ;
```

<a class="vx-rule" id="stmt.blocks.no-value" href="#stmt.blocks.no-value">stmt.blocks.no-value</a> Blocks do not produce values in v0.1.

## 6.3 Variable declarations

```vortex
// statements: valid
let width = 128;
let mut total: f32 = 0.0;
```

<a class="vx-rule" id="stmt.let.initializer" href="#stmt.let.initializer">stmt.let.initializer</a> Every local variable has an initializer. `mut` permits later assignment to its
storage. A variable whose type is a reference type must be declared without
`mut` ([References and mutability](references.md) 9.7). A written type is the
initializer's expected type for
[literal typing](types-and-values.md#literal-typing), and the initializer must
have that type. Without a written type, the variable takes the initializer's
type ([decision record](../decisions/numbers.md#d31)).

<a class="vx-rule" id="stmt.let.name-visibility" href="#stmt.let.name-visibility">stmt.let.name-visibility</a> The new name becomes visible only after the whole declaration, so its own
initializer cannot refer to it, and it must not reuse a name that is already
visible ([Scopes](declarations.md#36-scopes);
[decision 2](../decisions/names.md#d2)).

## 6.4 Assignment

Assignment changes a mutable place:

```vortex
// fragment
total = value;
matrix[row, column] *= scale;
point.x = 0.0;
```

<a class="vx-rule" id="stmt.assign.mutable-place" href="#stmt.assign.mutable-place">stmt.assign.mutable-place</a> The target grammar permits an identifier followed by any sequence of index or
field suffixes; such a target is a **place**, and its identifier is the
place's **root**. Arbitrary expressions and literals are not assignment
targets. The target must be a mutable place
([References and mutability](references.md) 9.3): its root is a variable
declared with `let mut`, or a name of type `&mut T`, in which case the
assignment writes the referent (the storage the reference refers to).
Assigning to any other place is a semantic error: for example, a place rooted
at a parameter or at a variable declared without `mut` whose type is not a
`&mut` reference, at a `for` loop variable, or at a name of type `&T`.

<a class="vx-rule" id="stmt.assign.order" href="#stmt.assign.order">stmt.assign.order</a> Supported operators are `=`, `+=`, `-=`, `*=`, `/=`, and `%=`. Compound
assignment requires both a mutable place as its target and a valid underlying
binary operation. The target is evaluated once. An assignment executes in this
order:

1. the target's base, then its index expressions from left to right;
2. the value on the right of the operator;
3. the bounds check of each index in the target, from left to right;
4. for compound assignment only, the load of the target's current value, the
   binary operation, and that operation's runtime check;
5. the store.

<a class="vx-rule" id="stmt.assign.runtime-error-stops" href="#stmt.assign.runtime-error-stops">stmt.assign.runtime-error-stops</a> A runtime error at any step stops the program before the later steps run.
[5.10 Evaluation order](expressions.md#510-evaluation-order) gives the order
inside expressions. See [decision 38](../decisions/statements.md#d38).

<a class="vx-rule" id="stmt.assign.not-expression" href="#stmt.assign.not-expression">stmt.assign.not-expression</a> Assignment is not an expression and cannot appear inside another expression.

Decision record: [Parameters and mutable places](../decisions/references.md#d23).

## 6.5 Expression statements

<a class="vx-rule" id="stmt.expr.any-expression" href="#stmt.expr.any-expression">stmt.expr.any-expression</a> Any grammatical expression may be followed by `;` and used as a statement.
Calls made for their effects are the common case:

```vortex
// program: valid
fn calculate() {
    print(6 * 7);
}

fn main() {
    print("ready");
    calculate();
}
```

<a class="vx-rule" id="stmt.expr.void-call" href="#stmt.expr.void-call">stmt.expr.void-call</a> An expression statement is the only place a call to a `void` function may
appear ([why](../decisions/operators.md#d44)). An implementation may warn when
a pure value is discarded, but the statement remains valid: a warning never
changes whether a program is accepted
([Diagnostics 10.1](diagnostics.md#101-required-diagnostic-data);
[decision 48](../decisions/diagnostics.md#d48)).

## 6.6 Conditional statements

```ebnf
if_statement ::=
    "if", expression, block,
    [ "else", ( if_statement | block ) ] ;
```

<a class="vx-rule" id="stmt.if.form" href="#stmt.if.form">stmt.if.form</a> Every condition must have type `bool`. Branch bodies use braces. `else if` is
represented as an `else` branch containing another `if` statement.

```vortex
// statements: valid
let score = 45;
if score >= 50 {
    print("passed");
} else if score >= 40 {
    print("close");
} else {
    print("try again");
}
```

<a class="vx-rule" id="stmt.if.no-value" href="#stmt.if.no-value">stmt.if.no-value</a> An `if` statement does not produce a value.

## 6.7 While loops

```ebnf
while_statement ::=
    "while", expression, block ;
```

<a class="vx-rule" id="stmt.while.condition" href="#stmt.while.condition">stmt.while.condition</a> The condition must have type `bool`. It is evaluated before every iteration,
so the body may execute zero times.

## 6.8 For loops

```ebnf
for_statement ::=
    "for", identifier, "in", expression, block ;
```

<a class="vx-rule" id="stmt.for.iterable-must-be-range" href="#stmt.for.iterable-must-be-range">stmt.for.iterable-must-be-range</a> The identifier introduces one loop-local variable, the **loop variable**. The
expression after `in` is the **iterable**. After any enclosing parentheses are
removed, the iterable must be a range expression
([5.8 Ranges](expressions.md#58-ranges)); any other iterable is a type error.
This is the only position where a range may appear: a range anywhere else is a
type error, because a range is not a value in v0.1. Iterating over an array is
[Planned](conformance.md#18-specification-examples). See
[decision 36](../decisions/statements.md#d36).

```vortex
// statements: valid
for index in 0..4 {
    print(index);
}
```

```vortex
// statements: type error
let values = [1, 2, 3];
for value in values {    // type error: the iterable must be a range
    print(value);
}
```

A `for` statement whose iterable is `start..end` or `start..=end` executes as
follows:

- <a class="vx-rule" id="stmt.for.endpoints-evaluated-once" href="#stmt.for.endpoints-evaluated-once">stmt.for.endpoints-evaluated-once</a> `start` and then `end` are evaluated once, before the first iteration.
  Later changes to variables used in them do not affect the loop.
- <a class="vx-rule" id="stmt.for.endpoint-types" href="#stmt.for.endpoint-types">stmt.for.endpoint-types</a> After [literal typing](types-and-values.md#literal-typing), `start` and
  `end` must have the same integer type; otherwise the program has a type
  error. An integer literal endpoint takes the type of the other endpoint;
  when both endpoints are literals, they have type `i32`.
- <a class="vx-rule" id="stmt.for.loop-variable-immutable" href="#stmt.for.loop-variable-immutable">stmt.for.loop-variable-immutable</a> The loop variable has that integer type. It is immutable: assigning to it is
  a semantic error. Each iteration binds a new loop variable that holds that
  iteration's value.
- <a class="vx-rule" id="stmt.for.exclusive-range" href="#stmt.for.exclusive-range">stmt.for.exclusive-range</a> For `start..end`, the body runs once for each value from `start` through
  `end - 1`, in increasing order, and zero times when `start >= end`.
- <a class="vx-rule" id="stmt.for.inclusive-range" href="#stmt.for.inclusive-range">stmt.for.inclusive-range</a> For `start..=end`, the body runs once for each value from `start` through
  `end`, in increasing order, and zero times when `start > end`. The loop ends
  after the iteration for `end` without computing `end + 1`, so an inclusive
  range that ends at the largest value of its type causes no overflow error.

<a class="vx-rule" id="stmt.for.continue-next-value" href="#stmt.for.continue-next-value">stmt.for.continue-next-value</a> In a `for` loop, `continue` moves on to the next value
([6.9](#69-break-and-continue)). See
[decision 13](../decisions/statements.md#d13).

<a class="vx-rule" id="stmt.for.variable-scope" href="#stmt.for.variable-scope">stmt.for.variable-scope</a> The loop variable is visible only in the body. It belongs to the scope of the
body block, so it must not reuse a visible name, and a local declared directly
in the body must not reuse it. Nested loops therefore need different variable
names. C-style `for` syntax and multiple loop bindings are not accepted.

## 6.9 Break and continue

<a class="vx-rule" id="stmt.break-continue.meaning" href="#stmt.break-continue.meaning">stmt.break-continue.meaning</a> `break;` exits the nearest enclosing loop. `continue;` begins the next
iteration of the nearest enclosing loop.

<a class="vx-rule" id="stmt.break-continue.outside-loop" href="#stmt.break-continue.outside-loop">stmt.break-continue.outside-loop</a> Both statements are syntactically recognizable anywhere a statement is
allowed, but semantic analysis must reject either one outside a loop.

## 6.10 Return

```ebnf
return_statement ::=
    "return", [ expression ], ";" ;
```

<a class="vx-rule" id="stmt.return.type-rules" href="#stmt.return.type-rules">stmt.return.type-rules</a> `return;` may appear only in a `void` function. `return expression;` may
appear only in a non-`void` function, and the expression must be compatible
with the function's return type (the same type after
[literal typing](types-and-values.md#literal-typing)). A `return` statement
that breaks either rule is a type error: `return;` in a non-`void` function,
`return expression;` in a `void` function whatever the expression's type
(including a call to a `void` function, so `return f();` is a type error even
when `f` returns `void`), and a returned value of the wrong type. See
[decision 8](../decisions/statements.md#d8).

<a class="vx-rule" id="stmt.terminating.definition" href="#stmt.terminating.definition">stmt.terminating.definition</a> A **terminating statement** is a statement after which execution cannot
continue with the next statement. In v0.1:

- <a class="vx-rule" id="stmt.terminating.return" href="#stmt.terminating.return">stmt.terminating.return</a> a `return` statement terminates;
- <a class="vx-rule" id="stmt.terminating.block" href="#stmt.terminating.block">stmt.terminating.block</a> a block terminates when it contains at least one statement and its last
  statement terminates;
- <a class="vx-rule" id="stmt.terminating.if" href="#stmt.terminating.if">stmt.terminating.if</a> an `if` statement terminates when it has an `else` branch and both its block
  and its `else` branch terminate (an `else if` branch is an `if` statement, so
  the rule applies to it again);
- <a class="vx-rule" id="stmt.terminating.loops-never" href="#stmt.terminating.loops-never">stmt.terminating.loops-never</a> no other statement terminates. `while` and `for` statements never terminate,
  whatever their condition or range, and neither do `break` and `continue`.

<a class="vx-rule" id="stmt.return.body-must-terminate" href="#stmt.return.body-must-terminate">stmt.return.body-must-terminate</a> The body of a non-`void` function must be a terminating block. Otherwise the
end of the body is reachable, and the program has a semantic error. The rule
looks only at the form of the statements, never at the values of conditions,
so a body that ends with a `while true` loop needs a `return` after the loop.

<a class="vx-rule" id="stmt.terminating.unreachable-code" href="#stmt.terminating.unreachable-code">stmt.terminating.unreachable-code</a> Statements that follow a `return`, `break` or `continue` in the same block are
valid and never run. An implementation may warn about them. They still count
when deciding which statement is last in a block. See
[decision 9](../decisions/statements.md#d9).

```vortex
// items: semantic error
fn sign(value: i32) -> i32 {
    if value > 0 {
        return 1;
    } else if value < 0 {
        return -1;
    }
}   // semantic error: the last if in the chain has no else, so the body does not terminate
```

## 6.11 Scope and lifetime

<a class="vx-rule" id="stmt.scope.no-shadowing" href="#stmt.scope.no-shadowing">stmt.scope.no-shadowing</a> A local variable exists from its declaration until execution leaves its block.
An inner block can read visible outer declarations. A name declared inside the
inner block is not visible after that block ends. An inner block must not
declare a name that is visible from an outer scope, because Vortex has no
shadowing: a declaration never hides a visible name
([decision record](../decisions/names.md#d2)).

<a class="vx-rule" id="stmt.scope.reference-lifetime" href="#stmt.scope.reference-lifetime">stmt.scope.reference-lifetime</a> The lifetime of a value and the visibility of its name are related but
distinct compiler concepts. A reference cannot outlive the storage it refers
to: [References and mutability](references.md) 9.7 limits references to
parameters and `let` bindings without `mut`, and a borrow made by a `let`
lasts until the end of its block (9.8). Decision record:
[Where references may appear, and how long a borrow lasts](../decisions/references.md#d41).

## 6.12 Excluded statements

<a class="vx-rule" id="stmt.excluded.list" href="#stmt.excluded.list">stmt.excluded.list</a> Vortex v0.1 has no `switch`, `match`, `do while`, exception handling, `defer`, unsafe
block, parallel loop, kernel launch, label, or `goto` statement.
