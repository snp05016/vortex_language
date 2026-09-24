# 3. The parser and the syntax tree

<p class="page-intro">The parser reads the lexer's tokens and decides how they fit together. What it hands on is a syntax tree: the program's structure, written down so that every later stage can walk it.</p>

After the [lexer](stage-2-lexer.md), the compiler has a flat row of tokens.
`let`, `result`, `=`, `2`, `+`, `3`, `*`, `4`, `;`. Nothing in that row says
that `3 * 4` belongs together, or that the whole calculation is the starting
value of `result`. The parser is the stage that works this out.

It does two things at once. It checks that the tokens are arranged in a shape
the language allows, and it records that shape as a tree. If the shape is
wrong, it says so and points at the place. If the shape is right, the tree it
builds becomes the thing every later stage reads instead of the source text.

On the [mountain in the overview](index.md#the-shape-of-the-whole-thing), this
is the second step up the left slope. It is also the stage where the front end
first starts to look like a compiler rather than a text processor, so it is
worth taking slowly.

## What this stage is for

The parser answers one question: does this sequence of tokens have a valid
Vortex shape? It does not ask whether the program makes sense. A program can
have a perfect shape and still use a name that was never declared, or add a
number to a boolean. Those are problems for [names and
scopes](stage-4-names-and-scopes.md) and [types and
rules](stage-5-types-and-rules.md).

When the shape is valid, the parser produces a **syntax tree** that keeps
everything later stages need: which operator was written, what order things
came in, and where in the file each piece came from. When the shape is not
valid, the parser reports a **syntax error**, then tries to carry on so that
one mistake does not hide the next ten.

## Words for this stage

parser
: The part of the compiler that reads tokens, checks that they follow the
  grammar, and builds a syntax tree.

grammar
: The written rules for which token sequences form valid programs. Vortex's
  grammar is in the [formal grammar chapter](../../specification/grammar.md).

rule
: One entry in the grammar, such as "a `while` statement is `while`, then an
  expression, then a block". Also called a **production**.

expression
: A piece of code that produces a value, such as `2 + 3`, `width`, or
  `square(4.0)`.

statement
: A piece of code that does something, such as `let x = 1;`, `return x;`, or
  a `while` loop. Statements do not produce values in Vortex.

node
: One item in a tree. In a syntax tree, each node stands for one construct:
  one addition, one `if` statement, one function.

root, child, leaf
: The **root** is the node at the top of a tree. A node directly below
  another is its **child**. A node with no children is a **leaf**; literals
  and names are usually leaves.

parse tree
: A tree with one node for every grammar rule the parser used, including
  punctuation. Also called a **concrete syntax tree**.

abstract syntax tree (AST)
: A tree that keeps only the parts of the program that carry meaning, and
  drops punctuation whose job is already shown by the tree's shape. When this
  guide says "syntax tree", it means this kind.

precedence
: Which operator groups first when there are no parentheses. In Vortex, `*`
  has higher precedence than `+`, so `2 + 3 * 4` means `2 + (3 * 4)`.

associativity
: Which way operators of the same precedence group when they repeat. Most
  Vortex binary operators are **left-associative**: `a - b - c` means
  `(a - b) - c`.

lookahead
: Peeking at the next token (or the next few) without using it up, so the
  parser can choose which rule applies.

syntax error
: A report that the tokens do not match the grammar, such as a missing `;` or
  an unclosed `(`.

error recovery
: What the parser does after a syntax error so it can keep checking the rest
  of the file.

synchronization point
: A token where the parser can safely start again after an error, such as
  `;`, `}`, or the next `fn`.

cascade
: A burst of false errors caused by one real mistake, because the parser got
  confused about where it was.

recursive descent
: A way of writing a parser by hand in which each grammar rule becomes one
  piece of the parser, and rules that contain other rules call those pieces.

Pratt parsing
: A well-known technique for parsing expressions with many precedence
  levels, named after Vaughan Pratt.

source span
: The start and length of the stretch of source text a token or node came
  from. Error messages use it to point at the right place.

## The grammar is the contract

Everything the parser accepts or rejects comes from the [formal
grammar](../../specification/grammar.md). The grammar is written in EBNF, a
compact notation where `[ x ]` means "x is optional" and `{ x }` means "x
may repeat". Here is the rule for a local variable:

```text
variable_declaration ::=
    "let", [ "mut" ], identifier,
    [ ":", type ],
    "=", expression, ";" ;
```

Read it left to right and it tells you what the parser must see: `let`,
maybe `mut`, a name, maybe a colon and a type, then `=`, an expression, and a
semicolon. So `let width = 128;` fits, and `let mut total: f32 = 0.0;` fits.
`let width;` does not, because the `=` and the expression are not optional.

The grammar chapter also marks, for every rule, which checks belong to the
parser and which belong to later stages. Read those notes before you write
anything. They answer most "should the parser reject this?" questions in
advance.

## The parser checks form, not meaning

This is the most important boundary on the page. The
[architecture page](../architecture.md#separation-of-concerns) states it
plainly: "The parser checks form." It uses this line as its example:

```vortex
// fragment
let value: MissingType = unknown_name + true;
```

Every token is where the grammar allows it. There is a name, a colon, a type
name, an equals sign, an expression, a semicolon. So the parser must accept
it. `MissingType` and `unknown_name` are unknown, and adding `true` to
anything is wrong, but those are problems of meaning. Name resolution and type
checking will report them.

Compare this line, which the parser must reject:

```vortex
// statements: syntax error
let value i32 = 10; // invalid: the ':' before the type is missing
```

Here the token sequence itself is wrong. No later stage could make sense of
it, because it never had a valid shape.

The [parser design notes](../parser-design.md#2-what-the-parser-does-and-does-not-do)
give a longer list. In plain words, the parser is responsible for:

- semicolons being present where the grammar needs them;
- `(`, `[` and `{` being closed in the right order;
- each parameter having the `name: type` form;
- each operator having the operands it needs.

And it must leave these alone:

- whether a name was declared (name resolution);
- whether `value + other` makes sense for their types (type checking);
- whether an assignment target is mutable, whether `break;` is inside a loop,
  whether `main` has the right signature (semantic checks);
- whether an array dimension is an integer constant expression (stage 5,
  while it resolves array types).

The last one surprises people. `[f32; rows, 8]` is a valid shape, so the
parser accepts it, even though stage 5 always rejects it: a v0.1 dimension may
contain only integer literals and arithmetic, so any name there is a
constant-evaluation error ([decision](../../decisions/arrays.md#d11)). The
grammar is explicit that dimensions are full expressions and that the parser
"must not reduce dimensions to integer tokens". Deciding what a dimension is
worth happens later.

## Parse trees and syntax trees

There are two ways to draw the structure of `let result = 2 + 3 * 4;`.

A **parse tree** records every rule the parser passed through. For Vortex's
grammar, the `3` alone sits under a primary expression, under a postfix
expression, under a unary expression, under a multiplicative expression, and
so on up through every precedence level. The `let`, the `=` and the `;` all
get nodes too. It is a faithful record of the parse, and it is mostly noise.

An **abstract syntax tree** keeps what matters and drops the rest. Nystrom
puts the difference in one line: "An AST elides productions that aren't
needed by later phases."[^ci-repr] The `;` is gone, because the fact that
this is a complete statement is already shown by the node. The eleven layers
above the `3` are gone, because the tree's shape already says that `3` is the
left operand of `*`.

<figure class="vx-figure">
<svg viewBox="0 0 760 390" role="img" aria-labelledby="grow-title grow-desc">
<title id="grow-title">A syntax tree growing from the tokens of one declaration</title>
<desc id="grow-desc">The nine tokens of let result = 2 + 3 * 4 semicolon sit in a row at the top. Below them a tree is built. The leaves 2, 3 and 4 are finished first, then the multiplication node, then the addition node, and last the declaration node for result at the root.</desc>
<text class="vx-text-muted" x="78" y="16">Tokens from the lexer</text>
<rect class="vx-box" x="78" y="24" width="60" height="32"/>
<text class="vx-mono" x="108" y="45" text-anchor="middle">let</text>
<rect class="vx-box" x="146" y="24" width="60" height="32"/>
<text class="vx-mono" x="176" y="45" text-anchor="middle">result</text>
<rect class="vx-box" x="214" y="24" width="60" height="32"/>
<text class="vx-mono" x="244" y="45" text-anchor="middle">=</text>
<rect class="vx-box" x="282" y="24" width="60" height="32"/>
<text class="vx-mono" x="312" y="45" text-anchor="middle">2</text>
<rect class="vx-box" x="350" y="24" width="60" height="32"/>
<text class="vx-mono" x="380" y="45" text-anchor="middle">+</text>
<rect class="vx-box" x="418" y="24" width="60" height="32"/>
<text class="vx-mono" x="448" y="45" text-anchor="middle">3</text>
<rect class="vx-box" x="486" y="24" width="60" height="32"/>
<text class="vx-mono" x="516" y="45" text-anchor="middle">*</text>
<rect class="vx-box" x="554" y="24" width="60" height="32"/>
<text class="vx-mono" x="584" y="45" text-anchor="middle">4</text>
<rect class="vx-box" x="622" y="24" width="60" height="32"/>
<text class="vx-mono" x="652" y="45" text-anchor="middle">;</text>
<text class="vx-text-muted" x="30" y="104">Syntax tree</text>
<line class="vx-line" x1="380" y1="128" x2="380" y2="178"/>
<line class="vx-line" x1="380" y1="212" x2="280" y2="262"/>
<line class="vx-line" x1="380" y1="212" x2="480" y2="262"/>
<line class="vx-line" x1="480" y1="296" x2="410" y2="336"/>
<line class="vx-line" x1="480" y1="296" x2="550" y2="336"/>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<rect class="vx-box-accent" x="290" y="94" width="180" height="34"/>
<text class="vx-text" x="380" y="116" text-anchor="middle">declare result</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<rect class="vx-box-strong" x="350" y="178" width="60" height="34"/>
<text class="vx-text" x="380" y="200" text-anchor="middle">+</text>
</g>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<rect class="vx-box" x="250" y="262" width="60" height="34"/>
<text class="vx-mono" x="280" y="284" text-anchor="middle">2</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<rect class="vx-box-strong" x="450" y="262" width="60" height="34"/>
<text class="vx-text" x="480" y="284" text-anchor="middle">*</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<rect class="vx-box" x="380" y="336" width="60" height="34"/>
<text class="vx-mono" x="410" y="358" text-anchor="middle">3</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<rect class="vx-box" x="520" y="336" width="60" height="34"/>
<text class="vx-mono" x="550" y="358" text-anchor="middle">4</text>
</g>
<text class="vx-text-muted" x="486" y="116">keeps: name, no mut, no written type</text>
<text class="vx-text-muted" x="30" y="236">no nodes for = or ;</text>
<text class="vx-text-muted" x="30" y="254">their job is shown by the shape</text>
</svg>
<figcaption>Figure 1. The tree for <code>let result = 2 + 3 * 4;</code>. Each node lights up when it is complete: the literals first, then <code>*</code> once both its operands exist, then <code>+</code>, and the declaration last. Nine tokens became six nodes.</figcaption>
</figure>

Compare Figure 1 with Step 3 of the [walk-through in the
overview](index.md#follow-one-line-through-the-compiler). It is the same
tree. It is the thing the next three stages will read, over and over, so what
it keeps and what it drops are decisions you live with for a long time.

The syntax tree is not a copy of the text with the spaces removed. It is a
statement of what the text means structurally. "The initializer of `result`
is an addition. Its left side is `2`. Its right side is a multiplication of
`3` and `4`." Every later stage reads it in that form.

## Precedence and associativity

Most of the work in parsing expressions comes down to two questions. When
two different operators compete for the same operand, which one gets it? And
when the same operator repeats, which pair goes first?

### Precedence decides between different operators

In `2 + 3 * 4`, the `3` sits between `+` and `*`. Both want it. Precedence
settles it: the operator with the higher precedence takes its operands
first. Vortex lists its levels in the [expressions
chapter](../../specification/expressions.md#52-precedence-and-associativity)
and again in the [grammar](../../specification/grammar.md#expressions-and-precedence).
From loosest to tightest, the ones you meet most are:

| Level | Operators | Groups |
| ---: | --- | --- |
| 1 | `..` `..=` (range) | does not chain |
| 2, 3 | `\|\|`, then `&&` | left |
| 4 to 6 | `\|`, `^`, `&` (bitwise) | left |
| 7, 8 | `==` `!=`, then `<` `<=` `>` `>=` | does not chain |
| 9 | `<<` `>>` | left |
| 10 | `+` `-` | left |
| 11 | `*` `/` `%` | left |
| 12 | prefix `+` `-` `!` `~` `&` `&mut` | right |
| 13 | calls, indexing, field access | left, chained |

`*` is on level 11 and `+` on level 10, so `3 * 4` groups first, and the sum
takes `2` and that product. The result is 14, not 20. In the tree, the tighter
operator always ends up lower down, nearer the leaves, because it has to be
worked out before the looser one can use its value.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-labelledby="climb-title climb-desc">
<title id="climb-title">Precedence levels deciding how 2 + 3 * 4 groups</title>
<desc id="climb-desc">On the left, a ladder of Vortex precedence levels, tightest at the top and loosest at the bottom, with the multiplicative and additive rungs highlighted. On the right, the expression 2 + 3 * 4. First the tokens 3 * 4 are grouped because multiplication is on a higher rung. Then the addition joins 2 with that group. A tree below shows the same grouping.</desc>
<text class="vx-text-muted" x="30" y="20">binds tighter</text>
<rect class="vx-box" x="30" y="30" width="310" height="34"/>
<text class="vx-text" x="44" y="52">13. calls, indexing, fields</text>
<rect class="vx-box" x="30" y="74" width="310" height="34"/>
<text class="vx-text" x="44" y="96">12. prefix - ! ~ &amp; &amp;mut</text>
<rect class="vx-box-accent" x="30" y="118" width="310" height="34"/>
<text class="vx-text-accent" x="44" y="140">11. multiplicative  * / %</text>
<rect class="vx-box-accent" x="30" y="162" width="310" height="34"/>
<text class="vx-text-accent" x="44" y="184">10. additive  + -</text>
<rect class="vx-box" x="30" y="206" width="310" height="34"/>
<text class="vx-text" x="44" y="228">9 to 7. shift, comparison, equality</text>
<rect class="vx-box" x="30" y="250" width="310" height="34"/>
<text class="vx-text" x="44" y="272">6 to 2. bitwise, &amp;&amp;, ||</text>
<rect class="vx-box" x="30" y="294" width="310" height="34"/>
<text class="vx-text" x="44" y="316">1. range  .. ..=</text>
<text class="vx-text-muted" x="30" y="348">binds looser</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box" x="416" y="22" width="254" height="62"/>
</g>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box-accent" x="512" y="30" width="150" height="46"/>
</g>
<rect class="vx-box-strong" x="426" y="36" width="36" height="34"/>
<text class="vx-mono" x="444" y="58" text-anchor="middle">2</text>
<rect class="vx-box-strong" x="472" y="36" width="36" height="34"/>
<text class="vx-mono" x="490" y="58" text-anchor="middle">+</text>
<rect class="vx-box-strong" x="520" y="36" width="36" height="34"/>
<text class="vx-mono" x="538" y="58" text-anchor="middle">3</text>
<rect class="vx-box-strong" x="566" y="36" width="36" height="34"/>
<text class="vx-mono" x="584" y="58" text-anchor="middle">*</text>
<rect class="vx-box-strong" x="612" y="36" width="36" height="34"/>
<text class="vx-mono" x="630" y="58" text-anchor="middle">4</text>
<text class="vx-text-muted" x="416" y="108">First: * is on a tighter rung, so 3 * 4 groups.</text>
<text class="vx-text-muted" x="416" y="126">Then: + joins 2 with that group.</text>
<line class="vx-line" x1="540" y1="186" x2="480" y2="236"/>
<line class="vx-line" x1="540" y1="186" x2="610" y2="236"/>
<line class="vx-line" x1="610" y1="270" x2="560" y2="306"/>
<line class="vx-line" x1="610" y1="270" x2="660" y2="306"/>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-strong" x="518" y="152" width="44" height="34"/>
<text class="vx-text" x="540" y="174" text-anchor="middle">+</text>
<rect class="vx-box" x="458" y="236" width="44" height="34"/>
<text class="vx-mono" x="480" y="258" text-anchor="middle">2</text>
</g>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box-accent" x="588" y="236" width="44" height="34"/>
<text class="vx-text" x="610" y="258" text-anchor="middle">*</text>
<rect class="vx-box" x="538" y="306" width="44" height="34"/>
<text class="vx-mono" x="560" y="328" text-anchor="middle">3</text>
<rect class="vx-box" x="638" y="306" width="44" height="34"/>
<text class="vx-mono" x="660" y="328" text-anchor="middle">4</text>
</g>
</svg>
<figcaption>Figure 2. Precedence as a ladder. The operator on the higher rung groups first and lands lower in the tree. The inner box and the <code>*</code> subtree light up together, then the outer box and the <code>+</code> node.</figcaption>
</figure>

Parentheses override the ladder. `(2 + 3) * 4` puts the addition inside the
multiplication and gives 20. Notice that the syntax tree for that version has
no node for the parentheses. They did their job by changing the shape, and
once the shape exists they have nothing left to say.

### Associativity decides between copies of the same operator

Precedence cannot help with `a - b - c`, because both operators are on the
same level. Associativity settles it. Vortex's binary arithmetic operators
group to the left, so the first pair goes first.

This matters because the two groupings give different answers. With `a = 10`,
`b = 4` and `c = 3`, grouping from the left gives `(10 - 4) - 3 = 3`.
Grouping from the right gives `10 - (4 - 3) = 9`. A parser that builds the
wrong shape will produce a program that compiles, runs, and prints the wrong
number, with nothing to warn anyone.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-labelledby="assoc-title assoc-desc">
<title id="assoc-title">Two ways to group a - b - c</title>
<desc id="assoc-desc">On the left, the correct left-associative tree: the root subtraction has a left child that subtracts b from a, and a right child c. It computes 3 when a is 10, b is 4 and c is 3. On the right, a dashed rejected tree that groups b - c first and computes 9.</desc>
<text class="vx-text" x="200" y="24" text-anchor="middle">Left-grouped: what Vortex requires</text>
<line class="vx-line" x1="200" y1="84" x2="130" y2="144"/>
<line class="vx-line" x1="200" y1="84" x2="280" y2="144"/>
<line class="vx-line" x1="130" y1="178" x2="80" y2="238"/>
<line class="vx-line" x1="130" y1="178" x2="180" y2="238"/>
<rect class="vx-box-strong vx-pulse" x="176" y="50" width="48" height="34"/>
<text class="vx-text" x="200" y="72" text-anchor="middle">-</text>
<rect class="vx-box-accent" x="106" y="144" width="48" height="34"/>
<text class="vx-text" x="130" y="166" text-anchor="middle">-</text>
<rect class="vx-box" x="256" y="144" width="48" height="34"/>
<text class="vx-mono" x="280" y="166" text-anchor="middle">c</text>
<rect class="vx-box" x="56" y="238" width="48" height="34"/>
<text class="vx-mono" x="80" y="260" text-anchor="middle">a</text>
<rect class="vx-box" x="156" y="238" width="48" height="34"/>
<text class="vx-mono" x="180" y="260" text-anchor="middle">b</text>
<text class="vx-mono" x="200" y="304" text-anchor="middle">(10 - 4) - 3 = 3</text>
<line class="vx-line" x1="380" y1="40" x2="380" y2="300"/>
<text class="vx-text" x="560" y="24" text-anchor="middle">Right-grouped: wrong for Vortex</text>
<line class="vx-line" x1="560" y1="84" x2="490" y2="144"/>
<line class="vx-line" x1="560" y1="84" x2="630" y2="144"/>
<line class="vx-line" x1="630" y1="178" x2="580" y2="238"/>
<line class="vx-line" x1="630" y1="178" x2="680" y2="238"/>
<rect class="vx-box-bad" x="536" y="50" width="48" height="34"/>
<text class="vx-text" x="560" y="72" text-anchor="middle">-</text>
<rect class="vx-box-bad" x="466" y="144" width="48" height="34"/>
<text class="vx-mono" x="490" y="166" text-anchor="middle">a</text>
<rect class="vx-box-bad" x="606" y="144" width="48" height="34"/>
<text class="vx-text" x="630" y="166" text-anchor="middle">-</text>
<rect class="vx-box-bad" x="556" y="238" width="48" height="34"/>
<text class="vx-mono" x="580" y="260" text-anchor="middle">b</text>
<rect class="vx-box-bad" x="656" y="238" width="48" height="34"/>
<text class="vx-mono" x="680" y="260" text-anchor="middle">c</text>
<text class="vx-mono" x="560" y="304" text-anchor="middle">10 - (4 - 3) = 9</text>
</svg>
<figcaption>Figure 3. The same five tokens, two shapes, two answers. For a left-associative operator, the earlier operator ends up lower in the tree, so it runs first.</figcaption>
</figure>

A few Vortex levels need extra care:

- **Prefix operators group to the right.** `- -value` means `-(-value)`. The
  operator nearest the operand applies first.
- **Ranges do not chain.** `start..middle..finish` is a syntax error, not a
  range of ranges. The grammar allows at most one range operator. A range also
  parses anywhere an expression can, as in `let span = 0..10;`. Build the tree
  as usual: [stage 5](stage-5-types-and-rules.md) rejects it with a type
  error, because v0.1 allows a range only as the iterable of a `for` loop
  ([decision 36](../../decisions/statements.md#d36)).
- **Comparisons do not chain.** `a < b < c` is a syntax error, and so are
  `a == b == c` and `a < b == c`: an operand of a comparison or equality
  operator cannot itself be one unless it is in parentheses
  ([decision](../../decisions/operators.md#d37)). After one comparison, a
  second comparison operator is reported as a syntax error, not folded in.
  `(a < b) == c` parses.
- **`&` means two things.** In front of an operand it takes a reference; between
  two operands it is bitwise AND. Its position in the expression tells them
  apart, and the tree must record which one it was.

## Ways to write a parser

This guide does not tell you how to code the parser, but it helps to know
the names of the usual approaches, so you can read about them.

The most common approach for a hand-written parser is **recursive descent**.
Each grammar rule becomes one part of the parser, and a rule that contains
another rule hands over to that part. Nystrom calls it top-down, because it
starts at the outermost rule and works down into the nested pieces.[^ci-parse]
The Kaleidoscope tutorial uses it for everything except binary
expressions.[^kal2] Vortex's own [parser design
notes](../parser-design.md#1-assignment-objective) plan a recursive-descent
parser.

For expressions, there are two common options. One gives every precedence
level its own rule, so the grammar itself encodes the ladder; *Crafting
Interpreters* does this.[^ci-parse] Vortex's grammar is written this way
too, with one rule per level from `range_expression` down to
`primary_expression`. The other option keeps a table of operator strengths
and lets one piece of the parser consult it. Kaleidoscope does this, calling
it operator-precedence parsing.[^kal2] A well-known version of the table
approach is **Pratt parsing**, from Vaughan Pratt's 1973 paper.[^pratt]
Aleksey Kladov describes it as recursive descent with an addition that
handles precedence and associativity directly.[^matklad-pratt]

Either option can parse Vortex correctly. The one rule the design notes
insist on is to pick one: "Do not implement two competing expression
parsers." Two parsers for the same expressions will disagree somewhere, and
the disagreement will show up as a bug far from its cause.

### Places where the grammar needs care

A few Vortex constructs look alike at first glance. The parser has to tell
them apart from the tokens alone.

- **Assignment or expression?** Both `value = 10;` and `calculate(value);`
  start with a name. Assignment is a statement in Vortex, not an expression,
  and the grammar only allows a name followed by index or field suffixes as
  its target. So `(left + right) = 10;` and `42 = value;` are syntax errors,
  while `points[index].x = 0.0;` is fine.
- **Which kind of `[`?** `[1, 2, 3]` is a list of elements, `[0.0; 16]` is a
  repeated value, and `[f32; 16]` is an array type. In expressions the token
  after the first item (`,` or `]` versus `;`) tells the two value forms
  apart. The type form only appears where the grammar expects a type.
- **A struct value or a block?** In `if ready { print(1); }` the name
  `ready` is followed by `{`, just as `Point` is in
  `Point { x: 1.0, y: 2.0 }`. The grammar says a name followed by `{` begins a
  struct expression "only when its contents have `field: value` form".
  Conditions of `if`, `while` and `for` are exactly where this matters.
- **Casts.** `f32(count)` looks like a call, but `f32` is a keyword. The
  [grammar](../../specification/grammar.md#expressions-and-precedence) gives
  casts their own production: one of the five numeric type keywords, then
  `(`, one expression and `)` ([decision](../../decisions/numbers.md#d1)). A
  type keyword may begin an expression only in this form, so the parser knows
  it has a cast from the first token and builds a cast node, not a call.
  `bool(flag)`, a bare `f32`, and `f32(a, b)` are syntax errors.

## When the source is wrong

A parser that stops at the first error is easy to write and annoying to use.
The roadmap asks for more: the parser must "recover from simple syntax errors
so one mistake does not hide every later error".

Nystrom lists what a parser must do when it meets bad input. It must detect
and report the error, and it must not crash or hang. He adds that a good
parser should also "Report as many distinct errors as there are", while
keeping cascades to a minimum.[^ci-parse] Those two wishes pull against each
other. Reporting more means continuing past an error, and continuing past an
error means guessing where the program makes sense again.

The usual answer is to throw away tokens until reaching a
**synchronization point**, a token after which the parser can be fairly sure
where it is. Nystrom calls this panic mode, and synchronizes at statement
boundaries.[^ci-parse] The [diagnostics
chapter](../../specification/diagnostics.md#105-recovery) names Vortex's
points: `;` for a simple statement, `}` for a block or declaration body, and
the next top-level `fn` or `struct`.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-labelledby="recover-title recover-desc">
<title id="recover-title">A parser recovering from two syntax errors in one function</title>
<desc id="recover-desc">Five source lines. Line 1 parses. Line 2 has a missing expression after a plus sign; the parser reports it, skips to the semicolon, and resumes. Line 3 parses. Line 4 has a missing type after a colon; the parser reports it and skips to the semicolon. Line 5 parses. Two separate errors are reported and the good lines are kept.</desc>
<text class="vx-text-muted" x="20" y="56">1</text>
<text class="vx-text-muted" x="20" y="100">2</text>
<text class="vx-text-muted" x="20" y="144">3</text>
<text class="vx-text-muted" x="20" y="188">4</text>
<text class="vx-text-muted" x="20" y="232">5</text>
<text class="vx-text-muted" x="20" y="276">6</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<rect class="vx-box" x="40" y="36" width="340" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<rect class="vx-box-bad" x="40" y="80" width="340" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<rect class="vx-box" x="40" y="124" width="340" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<rect class="vx-box-bad" x="40" y="168" width="340" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<rect class="vx-box" x="40" y="212" width="340" height="30"/>
</g>
<rect class="vx-box-accent vx-pulse" x="208" y="84" width="14" height="22"/>
<rect class="vx-box-accent vx-pulse" x="161" y="172" width="14" height="22"/>
<text class="vx-mono" x="50" y="56">fn main() {</text>
<text class="vx-mono" x="80" y="100">let width = 128 +;</text>
<text class="vx-mono" x="80" y="144">let height = 64;</text>
<text class="vx-mono" x="80" y="188">let depth: = 3;</text>
<text class="vx-mono" x="80" y="232">print(width);</text>
<text class="vx-mono" x="50" y="276">}</text>
<line class="vx-line" x1="380" y1="95" x2="420" y2="95"/>
<line class="vx-line" x1="380" y1="183" x2="420" y2="183"/>
<text class="vx-text" x="428" y="56">parsed</text>
<text class="vx-text" x="428" y="92">error 1: expected an expression</text>
<text class="vx-text-muted" x="428" y="110">after +, found ;. Resume after the ;</text>
<text class="vx-text" x="428" y="144">parsed</text>
<text class="vx-text" x="428" y="180">error 2: expected a type after :</text>
<text class="vx-text-muted" x="428" y="198">found =. Skip to the ; and resume</text>
<text class="vx-text" x="428" y="232">parsed</text>
<text class="vx-text-muted" x="428" y="276">two real errors, no false ones</text>
</svg>
<figcaption>Figure 4. Recovery at <code>;</code>. Each error is reported at the token where the shape broke, then the parser discards tokens up to the next <code>;</code> and starts a fresh statement. The good lines still become tree nodes. The message wording is only illustrative; Vortex has not fixed it.</figcaption>
</figure>

Recovery is a guess, and guesses go wrong. If line 2 had been
`let width = 128` with the `;` missing, skipping to the next `;` would
swallow all of line 3 as well. The parser would then report nothing wrong
with line 3 (good) but also build no node for `height` (bad), and name
resolution would later complain that `height` is unknown. That second
complaint is a cascade. The roadmap only asks for recovery from *simple*
errors, and the diagnostics chapter allows a compiler to stop after a
documented maximum number of errors. Aim for recovery that is right in the
common cases and quiet when it is unsure.

Line 2 of Figure 4 shows the case where recovery can help. The parser read the
name `width` before the error, so it can still record the declaration, marked
as broken and already reported. That is the suggested default,
[implementation choice I11](../../decisions/implementation.md#i11): later
stages then report nothing about `width`, so `print(width)` on line 5 draws no
false error.

Two properties matter more than cleverness. The parser must always move
forward, so recovery can never loop on the same token. And it must respect
nesting: the [design notes](../parser-design.md#14-diagnostics-and-recovery)
warn that skipping past a `}` without tracking how many `{` are open "may
discard the rest of a valid function".

## What the tree must keep

Later stages cannot look back at the source text. If the tree does not keep
something, it is gone. The [architecture
page](../architecture.md#frontend-invariants) lists what the front end must
preserve. In plain words:

- **Order.** Top-level declarations, statements in a block, call arguments,
  struct fields, array elements, indices and dimensions all stay in the order
  they were written. Vortex evaluates call arguments and array elements left
  to right, so order is part of the meaning.
- **The exact operator.** `+=` is not `=`. `..` (end excluded) is not `..=`
  (end included). `&` is not `&mut`. `-42` is a negation applied to the
  literal `42`, not a negative literal.
- **Different things stay different.** A type is not an expression. A named
  type such as `Point` is not a primitive type such as `i32`. A repeat array
  `[0.0; 4]` is not a list array `[0.0, 0.0, 0.0, 0.0]`, even though they
  hold the same values. An `else if` is an `else` branch containing another
  `if`.
- **Where everything came from.** Every node needs enough source position
  for an error message to point at it. A type error found in stage 5 will
  point at a node the parser built.
- **One parent per node.** Each node belongs to exactly one place in the
  tree. The tree is a tree, not a web.

The parser also keeps things it does not understand yet. Array dimensions
stay as expressions. Names stay as spellings. A named type like `Point` or
`Grid` stays a name, even when the program declares no struct called `Grid`;
[stage 4](stage-4-names-and-scopes.md) reports that. A planned type name such
as `u64` never gets this far: it is reserved for a future version, and the
lexer rejects it ([decision 29](../../decisions/lexical.md#d29)).

## Printing the tree

The roadmap's finish line for this milestone is a printed tree: "the
compiler can print a stable syntax tree for every v0.1 language construct".
Printing is how you look at what the parser built, and how tests check it.
The driver's `--ast` option prints the tree to standard output and stops after
parsing, without writing an executable
([decision](../../decisions/program.md#d20)).

Nystrom builds a small printer early for exactly this reason: when debugging
a parser, it helps to see whether the tree has the structure you
expected.[^ci-repr] A printed tree should show every node's kind, the
details it stores (the operator, the name, whether `mut` was written), its
children in order, and ideally its source position. The suggested default,
[implementation choice I5](../../decisions/implementation.md#i5), is an
indented S-expression: each node in parentheses on its own line, starting
with its kind, then what it stores and `@line:column`, with each child
indented two more spaces.

"Stable" means the same input always prints the same text. That rules out
anything that changes between runs, such as memory addresses, and anything
that depends on the order of an unordered collection. A stable printout can
be saved next to a test program and compared on every run. When the parser
changes, the difference in the printout shows exactly what changed.

The [architecture page](../architecture.md#testing-strategy) adds a warning
worth repeating. A test that only checks that parsing "returned" proves
almost nothing. Tests must look inside the tree: is the root an addition, is
its right child a multiplication, are there two dimension expressions and is
the first one an addition.

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- A parser for every v0.1 declaration, statement, type and expression: the
  roadmap asks for all of them.
- Functions, parameters, return types and `main` parsed as ordinary
  functions: `main` is checked later.
- `let`, `let mut`, assignment with all six assignment operators, blocks,
  `return`: the statement forms.
- `if`, `else`, `else if`, `while`, `for name in expression`, `break`,
  `continue`: all control flow.
- Every precedence level and associativity from the expressions chapter:
  a wrong shape means a wrong answer.
- Calls, casts, multidimensional indexing and field access, chained in
  source order.
- List arrays, repeat arrays and array types, with dimensions kept as
  expressions.
- Struct declarations with at least one field (`struct Marker {}` is a syntax
  error, [decision](../../decisions/operators.md#d26)) and struct expressions;
  shared and mutable references.
- Syntax errors that name what was expected, what was found, and where.
- Recovery at `;`, `}`, `fn` and `struct` that always makes progress.
- A stable tree printer.
- Tests with a valid program and its nearest invalid form for each rule.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Checking that names exist: stage 4.
- Types of any kind, including the default `i32` for bare integers: stage 5.
- Mutability and `break` outside a loop: stage 5.
- The `main` check: [stage 4](stage-4-names-and-scopes.md#exactly-one-main)
  ([decision](../../decisions/program.md#d6)).
- Working out array dimensions: stage 5, while the type checker resolves
  array types ([decision](../../decisions/arrays.md#d52)).
- Rejecting a zero extent: stage 5 reports it as a constant-evaluation error
  ([decision](../../decisions/arrays.md#d10)); the parser accepts `[f32; 0]`.
- Constant folding such as turning `2 + 2` into `4`: never in the parser.
  Stage 5 works out array dimensions and constant checks, and the tree keeps
  what was written.
- Error nodes for editors and a parser that never gives up: useful for an
  editor, not needed for v0.1.
- Parsing modules, generics, methods or anything outside v0.1: reject them,
  mostly as syntax errors (a reserved word such as `const` is already a
  lexical error).

</div>
</div>

## What you do not need yet

The right-hand column above is the list. The general rule is simple: if a
check needs to know what a name refers to or what type a value has, it does
not belong in the parser. The [parser design
notes](../parser-design.md#2-what-the-parser-does-and-does-not-do) say the
same thing: do not maintain the final symbol table, do not decide the type of
an integer literal, and do not execute or fold expressions while parsing.

## How you know it is finished

[Milestone 3](../../roadmap.md#milestone-3-parser-and-syntax-tree) is complete
"when the compiler can print a stable syntax tree for every v0.1 language
construct". Put more concretely, you are done when:

- every example in the [grammar](../../specification/grammar.md) and the
  [language tour](../../language-tour/README.md) whose
  [label](../../specification/conformance.md#18-specification-examples) is not
  `lexical error`, `syntax error`, `planned` or `fragment` parses, once
  completed as its kind says, and prints a tree whose shape you have checked
  by hand at least once ([decision 28](../../decisions/documentation.md#d28));
- `2 + 3 * 4` prints with `+` at the root and `*` as its right child, and
  `a - b - c` prints with the first `-` lower than the second;
- every example in the grammar labelled `lexical error` or `syntax error` is
  rejected, with an error that points at the right token;
- `a < b < c` and `a == b == c` are rejected as syntax errors, while
  `(a < b) == c` parses;
- a file with two unrelated syntax errors reports both, and no false third
  one;
- `let value: MissingType = unknown_name + true;`, written inside `main`, is
  accepted, because its problems are not the parser's to report;
- all the lexer tests from [stage 2](stage-2-lexer.md) still pass.

The roadmap's general evidence list applies too: the AST output is "stable
enough to inspect", and diagnostics point at the relevant source span.

## Traps

**Checking meaning in the parser.** It is tempting to reject `break;`
outside a loop, or an undeclared name, while you are right there. Don't. The
spec assigns those to later stages, and a parser that does them makes the
later stages harder to test, because some errors now have two possible
sources.

**Folding precedence into the lexer.** Precedence is about how tokens
combine, which is the parser's business. The lexer should hand over `*` as
`*` and nothing more.

**Right-grouping by accident.** The most natural first attempt at a
recursive rule often groups `a - b - c` as `a - (b - c)`. It passes every
test that only uses `+` and `*`, because those give the same answer either
way. Test with `-` and `/`.

**Recovery that eats good code.** Skipping to the next `}` sounds safe until
the error is inside a nested block and the skip throws away the rest of the
function. Track nesting, or skip to the nearer `;`.

**Recovery that never moves.** If the error handler can return to the same
token it failed on, the parser loops forever on bad input. Every error path
must consume at least one token or stop.

**Losing information you think nobody needs.** Dropping the difference
between `..` and `..=`, or reducing `[f32; 2 + 2]` to `[f32; 4]`, feels
harmless. Later stages need both, and once the tree loses them no stage can
get them back.

**Testing only that it "parsed".** A parser can return a tree of the wrong
shape and still pass a test that checks for success. Compare printed trees.

## How others teach this stage

**Kaleidoscope, chapter 2.** "Implementing a Parser and AST" combines
recursive descent with operator-precedence parsing for binary expressions,
and keeps a small table of operator precedences.[^kal2] Read it to see how
little machinery a working expression parser needs. It differs from Vortex
in error handling: on an error it reports and gives up on the current
construct, and the top level skips a token and tries again. Vortex needs
more than that, because the roadmap requires one mistake not to hide the
rest.

**Crafting Interpreters, "Representing Code" and "Parsing Expressions".**
The first explains grammars and the difference between parse trees and
syntax trees, and builds a tree printer for debugging.[^ci-repr] The second
builds a recursive-descent parser with one rule per precedence level,
explains why `6 / 3 - 1` is ambiguous without precedence and associativity,
and introduces panic-mode recovery with synchronization at statement
boundaries.[^ci-parse] This is the closest match to what Vortex needs. The
main difference is size: Lox has fewer precedence levels and no array or
struct syntax.

**Aleksey Kladov on Pratt parsing.** A short, careful
explanation of Pratt parsing in terms of "binding power", where slightly
unequal strengths on the two sides of an operator decide
associativity.[^matklad-pratt] Read it if you choose a table-driven
expression parser.

**Aleksey Kladov, "Resilient LL Parsing Tutorial".** A parser meant for
editors, which must cope with code that is half written. It always produces
a tree, marking broken parts with error nodes, and stops loops early when it
sees a token that belongs to an outer construct.[^matklad-resilient] Vortex
v0.1 does not need this much, but the idea of knowing which tokens mean
"stop, this belongs to someone else" is the same idea as Vortex's
synchronization points.

**The Rust compiler.** The rustc development guide says the parser turns
tokens into an AST, and that it tries to recover from errors by accepting a
somewhat larger language than Rust itself while reporting an
error.[^rustc-overview] It is a reminder that production compilers treat
recovery as a design goal, not an afterthought.

[^kal2]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 2, "Implementing a Parser and AST". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl02.html>
[^ci-repr]: Robert Nystrom, *Crafting Interpreters*, chapter "Representing Code". <https://craftinginterpreters.com/representing-code.html>
[^ci-parse]: Robert Nystrom, *Crafting Interpreters*, chapter "Parsing Expressions". <https://craftinginterpreters.com/parsing-expressions.html>
[^pratt]: Vaughan R. Pratt, "Top Down Operator Precedence", *Proceedings of the 1st ACM SIGACT-SIGPLAN Symposium on Principles of Programming Languages* (POPL 1973), pp. 41-51. <https://doi.org/10.1145/512927.512931>
[^matklad-pratt]: Aleksey Kladov, "Simple but Powerful Pratt Parsing", 13 April 2020. <https://matklad.github.io/2020/04/13/simple-but-powerful-pratt-parsing.html>
[^matklad-resilient]: Aleksey Kladov, "Resilient LL Parsing Tutorial", 21 May 2023. <https://matklad.github.io/2023/05/21/resilient-ll-parsing-tutorial.html>
[^rustc-overview]: Rust Compiler Development Guide, "Overview of the compiler". <https://rustc-dev-guide.rust-lang.org/overview.html>
