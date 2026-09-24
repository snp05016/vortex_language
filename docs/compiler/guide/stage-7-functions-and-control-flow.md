# 7. Functions and control flow

<p class="page-intro">This stage teaches generated code to call functions, make decisions and repeat work. When it is done, every v0.1 statement that changes the order of execution works in a compiled program.</p>

The program that printed `14` in [stage 6](stage-6-first-machine-code.md) ran
from top to bottom, once, with no choices. Real programs do not. They call
other functions and come back. They take one branch of an `if` and skip the
other. They go round a loop until some condition changes, and sometimes they
leave the loop early.

None of this is new to the front end. The parser already builds trees for
`if`, `while` and `for`, name resolution already knows which variable each
name refers to, and [stage 5](stage-5-types-and-rules.md) already checked that
every condition is a `bool`, that `break` sits inside a loop, and that every
non-`void` function ends with a statement that always returns. What is new is
the back end's side: turning those structured statements into the jumps a
processor actually follows.

This stage stays on the right slope of the
[overview's mountain](index.md#the-shape-of-the-whole-thing). It widens the
thin path built in stage 6 without changing its shape.

## What this stage is for

[Milestone 7](../../roadmap.md#milestone-7-functions-and-control-flow) lists
five things:

- generate calls to user-defined functions;
- generate `if`, `else`, `while` and `for` control flow;
- generate `break`, `continue` and early `return` behavior;
- support local variables inside nested blocks;
- test nested loops and nested conditionals.

It is complete "when functions and every v0.1 control-flow statement work in
compiled programs". The important word is *work*. The evidence is programs that
run and produce the right output, not IR that looks plausible.

The rules for what each statement means come from the
[statements chapter](../../specification/statements.md) and the
[declarations chapter](../../specification/declarations.md). This stage adds no
rules. Its job is to keep the ones that are there.

## Words for this stage

function call
: Running a function's body from somewhere else in the program, then carrying
  on from just after the call when the function finishes.

caller
: The function that makes a call.

callee
: The function being called.

parameter
: A named, typed input listed in a function's declaration, such as `left` in
  `fn add(left: i32, right: i32)`.

argument
: The value supplied for one parameter at a particular call, such as `3` in
  `add(3, 4)`.

return value
: The value a function hands back to its caller. A `void` function has none.

call stack
: The record of calls that have started and not yet finished. Each new call
  goes on top; each return removes the top.

stack frame
: One entry on the call stack. It holds what a single call needs while it
  runs: its parameters, its local variables, and where to go back to.

calling convention
: The agreement between caller and callee about where arguments are placed and
  where the return value is found.

local variable
: A variable declared with `let` inside a function. It exists only while its
  block is running.

basic block
: A straight run of instructions with one way in, at the top, and one way out,
  at the bottom. Nothing jumps into the middle of it.

control-flow graph (CFG)
: A picture of a function as basic blocks joined by arrows, where each arrow
  shows a place execution can go next.

edge
: One arrow in a control-flow graph.

branch
: An instruction that moves execution to another block. A **conditional
  branch** chooses between two blocks based on a `bool`; an **unconditional
  branch**, often called a **jump**, always goes to the same one.

back edge
: An edge that goes back to an earlier block, which is what makes a loop.

join point
: A block that two or more paths lead into, such as the code after an
  `if ... else`.

short-circuit evaluation
: Evaluating the right side of `&&` or `||` only when the left side has not
  already decided the answer.

early return
: A `return` that ends the function before its last statement.

loop-local variable
: The variable a `for` loop introduces, visible only inside the loop's body.

## Calls and the call stack

A **function call** pauses the **caller**, runs the **callee**, and then
resumes the caller with the callee's **return value** in hand. Each call needs
its own private workspace, because the same function may be running more than
once at the same moment. If `square` is called twice in one expression, the
two calls must not share a `value`.

That workspace is a **stack frame**, and the frames live on the **call
stack**. The name describes the behavior exactly. A call pushes a new frame on
top. A return pops it off, and the frame underneath becomes the active one
again. Calls always finish in the reverse order to how they started, so a stack
is all the bookkeeping needed.

```vortex
// program: valid
fn square(value: i32) -> i32 {
    return value * value;
}

fn sum_squares(left: i32, right: i32) -> i32 {
    return square(left) + square(right);
}

fn main() {
    print(sum_squares(3, 4));
}
```

This prints `25`. Figure 1 shows the stack at each step.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-labelledby="t7-stack-title t7-stack-desc">
<title id="t7-stack-title">The call stack while computing sum_squares of 3 and 4</title>
<desc id="t7-stack-desc">Six snapshots of the call stack. First only main. Then main with sum_squares on top. Then square of 3 on top of those. Then square returns 9 and its frame is gone. Then square of 4 is pushed. Finally sum_squares returns 25 and only main remains.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<text class="vx-text" x="69" y="40" text-anchor="middle">1</text>
<line class="vx-line" x1="10" y1="250" x2="128" y2="250"/>
<rect class="vx-box-accent" x="14" y="206" width="110" height="40" rx="4"/>
<text class="vx-mono" x="69" y="231" text-anchor="middle">main</text>
<text class="vx-text-muted" x="69" y="272" text-anchor="middle">program starts</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<text class="vx-text" x="193" y="40" text-anchor="middle">2</text>
<line class="vx-line" x1="134" y1="250" x2="252" y2="250"/>
<rect class="vx-box" x="138" y="206" width="110" height="40" rx="4"/>
<text class="vx-mono" x="193" y="231" text-anchor="middle">main</text>
<rect class="vx-box-accent" x="138" y="162" width="110" height="40" rx="4"/>
<text class="vx-mono" x="193" y="187" text-anchor="middle">sum_squares</text>
<text class="vx-text-muted" x="193" y="272" text-anchor="middle">call sum_squares</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<text class="vx-text" x="317" y="40" text-anchor="middle">3</text>
<line class="vx-line" x1="258" y1="250" x2="376" y2="250"/>
<rect class="vx-box" x="262" y="206" width="110" height="40" rx="4"/>
<text class="vx-mono" x="317" y="231" text-anchor="middle">main</text>
<rect class="vx-box" x="262" y="162" width="110" height="40" rx="4"/>
<text class="vx-mono" x="317" y="187" text-anchor="middle">sum_squares</text>
<rect class="vx-box-accent" x="262" y="118" width="110" height="40" rx="4"/>
<text class="vx-mono" x="317" y="143" text-anchor="middle">square(3)</text>
<text class="vx-text-muted" x="317" y="272" text-anchor="middle">call square(3)</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<text class="vx-text" x="441" y="40" text-anchor="middle">4</text>
<line class="vx-line" x1="382" y1="250" x2="500" y2="250"/>
<rect class="vx-box" x="386" y="206" width="110" height="40" rx="4"/>
<text class="vx-mono" x="441" y="231" text-anchor="middle">main</text>
<rect class="vx-box-accent" x="386" y="162" width="110" height="40" rx="4"/>
<text class="vx-mono" x="441" y="187" text-anchor="middle">sum_squares</text>
<text class="vx-text-muted" x="441" y="272" text-anchor="middle">9 comes back</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<text class="vx-text" x="565" y="40" text-anchor="middle">5</text>
<line class="vx-line" x1="506" y1="250" x2="624" y2="250"/>
<rect class="vx-box" x="510" y="206" width="110" height="40" rx="4"/>
<text class="vx-mono" x="565" y="231" text-anchor="middle">main</text>
<rect class="vx-box" x="510" y="162" width="110" height="40" rx="4"/>
<text class="vx-mono" x="565" y="187" text-anchor="middle">sum_squares</text>
<rect class="vx-box-accent" x="510" y="118" width="110" height="40" rx="4"/>
<text class="vx-mono" x="565" y="143" text-anchor="middle">square(4)</text>
<text class="vx-text-muted" x="565" y="272" text-anchor="middle">call square(4)</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<text class="vx-text" x="689" y="40" text-anchor="middle">6</text>
<line class="vx-line" x1="630" y1="250" x2="748" y2="250"/>
<rect class="vx-box-accent" x="634" y="206" width="110" height="40" rx="4"/>
<text class="vx-mono" x="689" y="231" text-anchor="middle">main</text>
<text class="vx-text-muted" x="689" y="272" text-anchor="middle">25 comes back</text>
<text class="vx-text-muted" x="689" y="290" text-anchor="middle">then print runs</text>
</g>
</svg>
<figcaption>Figure 1. The call stack grows by one frame at each call and shrinks by one at each return. The highlighted frame is the one running. In step 3 and step 5 there are two different calls to <code>square</code> at different times, each with its own <code>value</code>.</figcaption>
</figure>

Several rules from the docs shape what a call must do.

**Arguments are evaluated left to right.** The
[expressions chapter](../../specification/expressions.md#510-evaluation-order)
requires left-to-right evaluation for call arguments. With `print` inside a
function, the order becomes visible in the output, so it is testable. The same
rule covers the operands of every operator: in `square(left) + square(right)`,
the left call runs first ([decision 38](../../decisions/statements.md#d38)).

**Each parameter receives its argument's value.** Stage 5 checked that the
count and types match. Passing arrays and structs by reference, without
copying, is [stage 8](stage-8-data-in-memory.md)'s work.

**The return value arrives where the call was.** A call can sit inside a
larger expression, as `square(left) + square(right)` does. A `void` call
produces no value, so it may appear only as an expression statement; stage 5
has already rejected any other use
([decision](../../decisions/operators.md#d44)).

**Functions can be called from anywhere in the file.** The
[declarations chapter](../../specification/declarations.md#31-program-structure)
makes every top-level name visible throughout the file
([decision record](../../decisions/names.md#d3)), so `main` may call a function
defined below it. Include a test for that.

**A function may call itself, and two functions may call each other.** The
same rule makes both legal, and the call stack handles them without any
special case, because each call gets its own frame.

The stack is finite, though. A recursion that never stops, or goes too deep,
runs out of stack, and Vortex requires the program to stop with a report
rather than crash; [stage 9](stage-9-runtime-safety.md) adds that check
([record 46](../../decisions/diagnostics.md#d46)).

How arguments and return values physically travel between caller and callee
is the **calling convention**. It depends on the back end chosen in stage 6.
If you generate LLVM IR or C, the tool mostly handles it. If you generate
assembly directly, you follow the platform's rules yourself. Either way, calls
to the runtime's `print` must match what the runtime expects.

## Basic blocks and control-flow graphs

Inside one function, the back end thinks in **basic blocks**. A basic block is
a straight run of instructions that is always executed from top to bottom:
nothing jumps into its middle, and it leaves only at its end. The LLVM
reference manual describes each block as a list of instructions that "ends
with a terminator instruction (such as a branch or function return)".[^langref]
The *Mapping High Level Constructs* guide puts it the same way for control
flow in general: instructions are grouped into blocks, and each block ends with
an instruction that changes the flow.[^mapping]

Draw every block as a box and every possible next step as an arrow, and you
have a **control-flow graph**. It is the picture this stage is really about.
An `if` becomes a fork. A loop becomes a cycle, closed by a **back edge**.
`break`, `continue` and `return` become extra arrows that leave a block early.
The Rust compiler uses the same picture: its middle representation, MIR, "is
basically a Control-Flow Graph".[^rustc-overview]

You do not have to build a separate graph data structure to use the idea. It
is a way of thinking about what the generated code must do, and a way to draw
test cases.

## If and else

An `if` evaluates its condition, which stage 5 has already proved is a `bool`,
and then takes one of two edges. With an `else`, both edges lead to a block of
their own, and both of those blocks end by jumping to a **join point**, the
code after the whole statement. Without an `else`, the false edge goes straight
to the join point.

An `else if` chain adds nothing new. The statements chapter says
`else if` "is represented as an `else` branch containing another `if`
statement". So a chain of four conditions is four nested forks, each hanging
off the false edge of the one before.

Two details make `if` harder than it first looks.

**A branch that returns does not reach the join point.** If the `then` block
ends with `return`, it has no edge to the code after the `if`. Code
generation must not add one. This is the same structure stage 5 checked when it
made sure that the body of every non-`void` function ends in a terminating
statement ([decision 9](../../decisions/statements.md#d9)). Because of that
check, the code for a non-`void` function never reaches its closing brace; only
a `void` function returns there.

**`&&` and `||` are control flow too.** The expressions chapter requires that
they "evaluate the right operand only when required", and calls this behavior
"observable" and something lowering must preserve. In
`index < 4 && values[index] > 0.0`, the right side must not run when `index` is
4. So an ordinary-looking expression contains a hidden fork, and it needs the
same treatment as an `if`.

## While loops

A `while` evaluates its condition "before every iteration, so the body may
execute zero times", in the words of the statements chapter. In the graph,
that means the condition gets its own block, often called the loop header. The
body leads back to the header along a back edge. The header's false edge leads
out of the loop.

`break` and `continue` add two more arrows. `break` goes to the block after
the loop. `continue` goes back to the header, where the condition is checked
again. Here is a loop that uses both:

```vortex
// program: valid
fn main() {
    let mut count = 0;
    let mut total = 0;
    while count < 10 {
        count += 1;
        if count % 2 == 0 {
            continue;
        }
        if total > 10 {
            break;
        }
        total += count;
    }
    print(total);
}
```

It adds up odd numbers until the total passes 10, and prints `16`.

<figure class="vx-figure">
<svg viewBox="0 0 760 350" role="img" aria-labelledby="t7-cfg-title t7-cfg-desc">
<title id="t7-cfg-title">Control-flow graph of a while loop with continue and break</title>
<desc id="t7-cfg-desc">An entry block sets count and total to zero and leads to the loop header, which tests count less than 10. The true edge leads to the body, which adds one to count and tests whether count is even. If even, a continue edge returns to the header. If odd, the next block tests total greater than 10. If yes, a break edge leaves to the block after the loop, which prints total. If no, the next block adds count to total and a back edge returns to the header. The header's false edge also leads to the block after the loop. A dot travels one possible route through the graph.</desc>
<rect class="vx-box" x="250" y="12" width="260" height="36" rx="4"/>
<text class="vx-mono" x="380" y="35" text-anchor="middle">count = 0, total = 0</text>
<rect class="vx-box-strong" x="250" y="84" width="260" height="36" rx="4"/>
<text class="vx-mono" x="380" y="107" text-anchor="middle">count &lt; 10 ?</text>
<rect class="vx-box" x="250" y="156" width="260" height="36" rx="4"/>
<text class="vx-mono" x="380" y="179" text-anchor="middle">count += 1; even ?</text>
<rect class="vx-box" x="250" y="228" width="260" height="36" rx="4"/>
<text class="vx-mono" x="380" y="251" text-anchor="middle">total &gt; 10 ?</text>
<rect class="vx-box" x="250" y="300" width="260" height="36" rx="4"/>
<text class="vx-mono" x="380" y="323" text-anchor="middle">total += count</text>
<rect class="vx-box-accent" x="560" y="84" width="180" height="36" rx="4"/>
<text class="vx-mono" x="650" y="107" text-anchor="middle">print(total)</text>
<text class="vx-text-muted" x="650" y="74" text-anchor="middle">after the loop</text>
<line class="vx-line" x1="380" y1="48" x2="380" y2="84"/>
<polygon class="vx-arrowhead" points="375,76 380,84 385,76"/>
<line class="vx-line" x1="380" y1="120" x2="380" y2="156"/>
<polygon class="vx-arrowhead" points="375,148 380,156 385,148"/>
<text class="vx-text-muted" x="388" y="142">true</text>
<line class="vx-line" x1="380" y1="192" x2="380" y2="228"/>
<polygon class="vx-arrowhead" points="375,220 380,228 385,220"/>
<text class="vx-text-muted" x="388" y="214">odd</text>
<line class="vx-line" x1="380" y1="264" x2="380" y2="300"/>
<polygon class="vx-arrowhead" points="375,292 380,300 385,292"/>
<text class="vx-text-muted" x="388" y="286">no</text>
<line class="vx-line" x1="510" y1="102" x2="560" y2="102"/>
<polygon class="vx-arrowhead" points="552,97 560,102 552,107"/>
<text class="vx-text-muted" x="516" y="94">false</text>
<path class="vx-line" d="M510 246 L650 246 L650 120"/>
<polygon class="vx-arrowhead" points="645,128 650,120 655,128"/>
<text class="vx-text-accent" x="560" y="240">break</text>
<path class="vx-line" d="M250 174 L150 174 L150 112 L250 112"/>
<polygon class="vx-arrowhead" points="242,107 250,112 242,117"/>
<text class="vx-text-accent" x="158" y="150">continue</text>
<path class="vx-line" d="M250 318 L60 318 L60 92 L250 92"/>
<polygon class="vx-arrowhead" points="242,87 250,92 242,97"/>
<text class="vx-text-muted" x="68" y="212">back edge</text>
<circle class="vx-dot" r="6">
<animateMotion dur="14s" repeatCount="indefinite" path="M380 30 L380 102 L380 174 L380 246 L380 318 L60 318 L60 92 L250 92 L380 102 L380 174 L150 174 L150 112 L250 112 L380 102 L380 174 L380 246 L650 246 L650 102"/>
</circle>
</svg>
<figcaption>Figure 2. The loop above as a control-flow graph. The dot walks one possible route: once through the whole body and back along the back edge, once out through <code>continue</code>, and finally out through <code>break</code> to the code after the loop. Every arrow in the figure must exist in the generated code, and no others.</figcaption>
</figure>

Two things in the figure are worth checking in your own output. First,
`continue` goes to the header, not to the top of the body, because the
condition must be tested again. Second, there are two ways to reach
`print(total)`: the header's false edge and `break`. Both must arrive with the
same view of `total`. If you use SSA, that block is a join point and needs a
phi, as [stage 6](stage-6-first-machine-code.md#ssa-if-you-use-llvm)
described.

## For loops over integer ranges

The statements chapter defines the `for` loop in a few sentences, and it is
worth reading them exactly:

- `for`, an identifier, `in`, an expression, and a block;
- the identifier "introduces one loop-local variable";
- the expression after `in` must be a range, possibly in parentheses;
- the loop variable "is visible only in the body";
- C-style `for` syntax and multiple loop bindings are not accepted.

The [expressions chapter](../../specification/expressions.md) adds what a range
means. `start..end` excludes `end`; `start..=end` includes it. Both endpoints
must have the same integer type, and the loop variable takes that type
([statements, 6.8](../../specification/statements.md#68-for-loops)). The
[statements tour](../../language-tour/09-statements.md) gives the plain example:
`for index in 0..4` visits `0`, `1`, `2` and `3`.

In the control-flow graph, a `for` loop has the same shape as a `while`: a
header that decides whether there is another value, a body, and a back edge.
The difference is a step that moves the loop variable to the next value.
`continue` in a `for` loop "begins the next iteration", so it must pass
through that step. A `continue` that jumps straight to the header would test
the same value again and never finish.

### What the specification says about `for` loops {#what-the-specification-does-not-yet-say}

[Decision 13](../../decisions/statements.md#d13) answers the questions code
generation depends on. Each answer is visible in program behavior, so each
needs a test.

- **When the endpoints are evaluated.** Once, start then end, before the first
  iteration, so a body that changes `end` does not change the loop.
- **The loop variable's type.** It has the endpoints' type.
- **Whether the loop variable can be assigned.** It cannot: it is immutable,
  and it is a fresh variable in each iteration.
- **Ranges that contain no values.** `start..end` runs zero times when
  `start >= end`, and `start..=end` zero times when `start > end`.
- **Inclusive ranges at the top of a type.** An inclusive loop must stop after
  the iteration for `end` without computing `end + 1`, so a range that ends at
  the largest value of its type never trips the overflow check.

## Break, continue and early return

All three are jumps that leave the normal top-to-bottom order.

`break;` "exits the nearest enclosing loop" and `continue;` "begins the next
iteration of the nearest enclosing loop", in the statements chapter's words.
*Nearest* is the key word. In nested loops, a `break` in the inner loop leaves
only the inner one. Vortex v0.1 has no labels, so there is no way to break out
of two loops at once, and generated code must not behave as though there were.

An early `return` leaves every enclosing block and loop at once and goes
straight back to the caller. In a non-`void` function it carries a value.
In a `void` function it is a bare `return;`, which the tour's
`print_positive` example uses to finish early.

After any of these three, the rest of the block cannot run. Statements there
are still valid and still checked
([decision 9](../../decisions/statements.md#d9)), but the generated code must
never reach them.

## Nested blocks and local variables

A block `{ ... }` creates a nested scope, and blocks can sit inside `if`
branches, loop bodies, or on their own. The statements chapter separates two
ideas here. A local variable's name is visible from its declaration to the end
of its block; that is scope, which stage 4 handled. The variable itself "exists
from its declaration until execution leaves its block"; that is its
**lifetime**, and it is what code generation has to get right.

Blocks nest strictly, one inside another, so their local variables come and go
in last-in, first-out order. *Crafting Interpreters* makes exactly this
observation to explain why locals can live on a stack: when a block ends, it
takes the most recent locals with it.[^ci-locals]

A `let` inside a loop body runs on every pass through the body. Each iteration
gets a fresh variable with a freshly computed initializer. That is easy to get
right by accident and easy to get wrong when optimizing, so it deserves a
test. One related rule is settled in stage 4: an inner block may not declare a
local with the same name as a visible outer one, because Vortex has no
shadowing ([decision record](../../decisions/names.md#d2)). So at any point in
a program each name means exactly one variable, and code generation never has
to choose between two.

## Tests for nested control flow

The roadmap asks for nested loops and nested conditionals by name. A good test
set makes each edge of the control-flow graph matter to the output, so that a
missing or misplaced jump changes what the program prints.

| Case | What it proves |
| --- | --- |
| `break` inside an inner loop | Only the inner loop ends; the outer loop continues. |
| `continue` inside an inner loop | The inner loop's next iteration runs, not the outer one's. |
| Early `return` from inside two loops | The function ends at once and the caller gets the value. |
| `else if` chain with four branches | Exactly one branch runs, and it is the right one. |
| `if` without `else` whose branch returns | The code after the `if` runs only when the condition is false. |
| `while` whose condition starts false | The body runs zero times. |
| `for index in 0..4` and `0..=4` | The body sees exactly the documented values. |
| `for i in 0..end` whose body changes `end` | The loop runs the number of times fixed at the start. |
| `for i in 5..2` and `for i in 5..=2` | The body runs zero times. |
| `for i in last - 1..=last` with `last` the largest `u32` | Two iterations, then the loop ends without an overflow error. |
| `continue` inside a `for` loop | The loop still advances and finishes. |
| `&&` with a printing call on the right | The call runs only when the left side is `true`. |
| `let` inside a loop body | Each iteration starts from the initializer again. |
| A call to a function defined later in the file | Declaration order does not matter. |
| A function that calls itself | Each call has its own parameters and locals. |
| Two functions that call each other | Mutual recursion works, and each call has its own frame. |
| Calls as arguments, each printing | Arguments are evaluated left to right. |
| `f() + g()` where both print | The left operand runs first. |

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- Calls to user-defined functions with parameters and return values: the
  first Milestone 7 bullet.
- A frame per call, so repeated and recursive calls do not share locals.
- Left-to-right argument evaluation: the expressions chapter requires it.
- `if`, `else` and `else if` as forks that join correctly.
- `while` with the condition tested before every iteration.
- `for` over integer ranges, with `..` and `..=` behaving as documented.
- `break`, `continue` and early `return`, each going to the right block.
- Short-circuit `&&` and `||`.
- Local variables in nested blocks with the right lifetimes.
- The `for` loop rules of decision 13, each with a test.
- End-to-end tests of nested loops and nested conditionals that check output.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Passing arrays and structs by reference: this is
  [stage 8](stage-8-data-in-memory.md).
- Overflow, division and bounds checks inside loops: these are
  [stage 9](stage-9-runtime-safety.md).
- `switch`, `match`, `do while`, labels, `goto`, exceptions and `defer`: the
  statements chapter excludes them from v0.1.
- Loop transformations, tiling, vectorization and parallel loops: the roadmap
  places them after v0.1.
- Iterating over anything other than integer ranges: v0.1 specifies only
  ranges.
- Removing unreachable code: an optimization, and optimization comes after
  v0.1.

</div>
</div>

## What you do not need yet

The right-hand column above is the list. Almost everything tempting at this
stage is some kind of optimization, and Vortex v0.1 is about getting correct
behavior first. A loop that runs slowly and prints the right answer passes
Milestone 7. A fast loop that misplaces one `continue` does not.

## How you know it is finished

The roadmap's completion condition for
[Milestone 7](../../roadmap.md#milestone-7-functions-and-control-flow) is that
"functions and every v0.1 control-flow statement work in compiled programs".
In practice:

- every row of the test table above has at least one end-to-end test that
  compiles, runs and compares output;
- every statement form in the statements chapter that changes control flow is
  covered, both alone and nested;
- each `for` loop rule of decision 13 has a test;
- every test from milestones 0 to 6 still passes, including the program that
  prints `14`.

## Traps

**Sending `continue` to the wrong place.** In a `while`, `continue` must go to
the condition. In a `for`, it must go through the step that advances the loop
variable. Jumping to the top of the body skips the check in one case and
loops forever in the other.

**Treating `&&` and `||` as ordinary operators.** Evaluating both sides and
combining them gives the right `bool` and the wrong program, because the right
side may have effects or may be unsafe to evaluate. The spec calls
short-circuiting observable.

**Adding a fall-through edge after a `return`.** A block that ends in `return`
has no successor inside the function. Falling into the next block produces
behavior the source never asked for.

**Breaking out of the wrong loop.** `break` and `continue` belong to the
nearest enclosing loop. With nested loops, it is easy to record only the
outermost one, or whichever was entered last in the compiler's own
bookkeeping rather than in the source.

**Sharing locals between calls.** If local variables are tied to the function
rather than to each call, the first recursive call will overwrite its caller's
values. The call stack exists to prevent that.

**Deciding `for` semantics in the code generator.** It is tempting to let the
lowering decide, say, whether the range end is read once or before every
iteration, because of how the code happened to come out. That is language
behavior, and decision 13 has already fixed it: the end is read once. Test
each rule instead of trusting the lowering.

**Testing only the happy path.** A loop that always runs to completion never
exercises `break`. A condition that is always true never exercises the false
edge. Each edge in Figure 2 should be taken by at least one test.

## How others teach this stage

**Kaleidoscope chapter 5** adds `if/then/else` and a `for` loop to a language
that had neither.[^kal5] The `if` becomes a condition, separate then and else
blocks, and a merge block where a phi picks the value, which is the fork and
join described above. Its `for` loop is worth reading for the shape of the
blocks, but its meaning differs from Vortex's: Kaleidoscope emits the body
before computing the end condition, so its loop body runs at least once, and
its step defaults to 1.0 because every value is a double.[^kal5] A Vortex
`while` tests its condition before every iteration, and Vortex ranges are
integer ranges.

***Crafting Interpreters*, "Jumping Back and Forth"** compiles the same
statements to jumps in a bytecode virtual machine.[^ci-jump] It emits a jump
before it knows how far to jump and fills in the distance later, jumps over
the `else` after the `then` branch runs, and makes `while` jump back to before
the condition. It also compiles `and` and `or` with jumps, which is the
short-circuit point above. The chapter includes a short account of why
structured statements are enough: any control flow built from `goto` can be
rewritten with sequencing, loops and branches.[^ci-jump] Its challenges suggest
adding `continue`, which Vortex already requires. The companion chapter "Local
Variables" explains why locals fit on a stack.[^ci-locals]

***Mapping High Level Constructs to LLVM IR*** shows how an if-then-else
becomes blocks joined by conditional and unconditional branches, and how the
same thing can later be optimized into a single choice between two
values.[^mapping] Its functions page shows how definitions, calls and returns
look in LLVM IR. It is the reference to keep open if you chose LLVM in stage 6.

**Nora Sandler's *Writing a C Compiler*** covers the same ground in three
chapters of its first part: "If Statements and Conditional Expressions",
"Loops" and "Functions".[^sandler-book] Unlike Vortex, it reaches them before
it adds any type other than `int`.

[^langref]: LLVM Project, "LLVM Language Reference Manual". <https://llvm.org/docs/LangRef.html>
[^mapping]: Michael Rodler and Mikael Egevig, *Mapping High Level Constructs to LLVM IR*. <https://mapping-high-level-constructs-to-llvm-ir.readthedocs.io/en/latest/>
[^rustc-overview]: Rust Compiler Development Guide, "Overview of the compiler". <https://rustc-dev-guide.rust-lang.org/overview.html>
[^ci-locals]: Robert Nystrom, *Crafting Interpreters*, chapter "Local Variables". <https://craftinginterpreters.com/local-variables.html>
[^kal5]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 5, "Extending the Language: Control Flow". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl05.html>
[^ci-jump]: Robert Nystrom, *Crafting Interpreters*, chapter "Jumping Back and Forth". <https://craftinginterpreters.com/jumping-back-and-forth.html>
[^sandler-book]: Nora Sandler, *Writing a C Compiler: Build a Real Programming Language from Scratch*, No Starch Press, 2024. <https://nostarch.com/writing-c-compiler>
