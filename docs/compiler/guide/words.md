# Word list

<p class="page-intro">Every technical word used in the compiler guide, in alphabetical order, explained in plain language. Each stage page explains its own words too; this page puts them all in one place.</p>

For the exact meaning of words that describe the Vortex language itself, the
[specification glossary](../../specification/glossary.md) is the
[authority](../../specification/conformance.md#11-document-authority).
The definitions here are written for understanding, not for settling
arguments.

<!-- TERMS -->

ABI (application binary interface)
: The low-level agreement about how values are laid out and passed between functions in machine code.

abstract syntax tree (AST)
: A tree that keeps only the meaningful parts of a program and drops punctuation whose job the shape already shows.

address
: The number of a byte in memory, used to say where a value starts.

addressable
: Naming a real storage place, such as a variable, field or array element, that a reference can point at.

aggregate
: A value made of other values; in v0.1, arrays and structs.

alignment
: A rule that a value must start at an address that is a multiple of some number.

argument
: The value supplied for one parameter at a particular call.

assembly language
: A readable text form of machine instructions; an assembler turns it into machine code.

associativity
: Which way repeated operators of the same precedence group; left-associative means `a - b - c` is `(a - b) - c`.

back edge
: An edge that leads to an earlier block, which is what makes a loop.

back end
: The second half of a compiler, which turns a checked program into code for a particular kind of machine.

basic block
: A straight run of instructions that is entered only at the top and left only at the bottom.

block
: A pair of braces with statements inside; in Vortex every block creates a new scope.

borrow
: The stretch of a program during which a reference made with `&` or `&mut` is live; in Vortex a `let` borrow lasts to the end of its block and an argument borrow lasts for the call.

boundary case
: The value at the edge: the last one that works and the first one that fails.

bounds check
: A runtime check that an index is at least zero and below the size of its dimension.

branch
: An instruction that moves execution to another block; a conditional branch chooses between two blocks based on a `bool`, and a jump always goes to the same block.

build
: The process that turns the compiler's own source code into the `vortex` program.

build system
: The tool that knows the recipe for building the compiler.

byte
: The unit a file is stored in; one character may need several bytes.

bytecode
: A compact list of simple instructions meant to be run by another program (a virtual machine) rather than directly by the processor.

call stack
: The record of calls that have started and not yet finished; each call adds an entry and each return removes one.

caller, callee
: The function that makes a call, and the function being called.

calling convention
: The agreement between caller and callee about where arguments and the return value go.

cascade
: A burst of false errors caused by one real mistake.

cast
: An explicit conversion to a numeric type, written as the type's keyword followed by one value in parentheses, such as `f32(count)`. It looks like a call but is not one.

category
: The kind of problem a diagnostic reports; the specification lists eight.

character
: One Unicode scalar value: in plain terms, one letter, digit, symbol or space as Unicode numbers them. An accented letter typed as a plain letter followed by a separate accent counts as two. See [decision 15](../../decisions/lexical.md#d15).

clean checkout
: A fresh copy of the project's files with nothing left over from earlier builds.

clean configuration
: A build that starts from nothing, with no leftover files and no settings that exist only on one machine.

code generator
: The compiler stage that turns the intermediate representation into instructions for a real processor.

column-major order
: Storing a two-dimensional array one whole column after another.

comment
: Text for human readers that the compiler skips; in v0.1 it runs from `//` to the end of the line.

compatible
: Allowed to be used together under the type rules; in v0.1 this means exactly the same type, because there are no automatic conversions.

compile time
: While the compiler is working on the source, before any executable exists.

compiler
: A program that translates programs: it reads source code, checks it, and writes out code a machine can run.

constant evaluation
: Working out an expression's value during compilation: every array dimension, and every checked operation whose deciding operands are integer constant expressions.

constant expression
: In v0.1, an integer constant expression: integer literals combined with `+`, `-`, `*`, `/`, `%` and parentheses, with an optional `-` before a literal; names and calls never count. Array dimensions must be constant expressions.

contiguous
: Stored side by side with no gaps.

control-flow graph (CFG)
: A function drawn as basic blocks joined by arrows that show where execution can go next.

control-flow path
: One possible route through a function body, following one choice at every `if`, loop and `return`.

declaration
: A place in the program that introduces a name.

decoded value
: The value a literal actually stands for, with escape sequences turned into real characters.

default type
: The type a literal gets when nothing around it asks for another: `i32` for whole numbers, `f32` for decimals.

diagnostic
: A message from the compiler about a problem in the source.

dimension
: One size in an array's shape.

dot product
: Multiplying two equal-length lists position by position and adding the results.

driver
: The program a person runs to use the compiler (`vortex`); it reads the command line, runs the stages in order and reports the outcome through its exit status: 0 for success, 1 when the source has errors, 2 for usage and file problems.

duplicate declaration
: Two declarations of the same name in the same scope.

dynamic rule
: A rule that depends on values known only while the program runs.

early return
: A return that ends a function before its last statement.

edge
: One arrow in a control-flow graph.

empty compiler
: A compiler whose driver runs and exits properly but does not translate anything yet.

encoding
: The rule that says which bytes stand for which characters.

end-of-file token
: A token with no spelling that marks the end of the input.

end-to-end test
: A test that runs the whole compiler on a source file, runs the result, and checks what comes out.

entry point
: The function a program starts running from; in Vortex, `main`.

error recovery
: What the parser does after a syntax error so it can keep checking the rest of the file.

escape sequence
: A backslash and a character inside a literal that stand for something hard to type, such as `\n` for a line break.

executable
: A file of machine code that the operating system can start as a program.

exit status
: A small number a program hands back when it finishes; by convention zero means success. A compiled Vortex program exits with 0 when `main` returns and with 101 after a runtime error.

expected failure
: A test that is known to fail and is marked that way on purpose, usually because it covers a known limitation.

expected output
: A written record of what should happen for a test case: the printed output, or the error's category and location.

expression
: A piece of code that produces a value.

field
: One named part of a struct.

front end
: The first half of a compiler, which reads the source code, works out its structure and meaning, and rejects programs that break the rules.

function call
: Running a function's body from somewhere else, then carrying on from just after the call.

grammar
: The written rules for which token sequences form valid programs.

IEEE 754
: The international standard for floating-point arithmetic.

implementation limit
: A point where a compiler supports less than the language allows, which the specification requires to be documented.

infinity
: A special floating-point value produced, for example, by dividing a nonzero number by zero.

initializer
: The expression after `=` in a `let` declaration, which gives the variable its first value and, when no type is written, its type.

inner dimension
: The size that must match to multiply two matrices: the columns of A and the rows of B.

instruction
: One basic operation a processor can carry out.

integer overflow
: An integer calculation whose true answer does not fit in its type.

intermediate representation (IR)
: A way of writing the program down inside the compiler that is no longer source text but not yet machine code; it sits between the front end and the back end.

invalid program
: A program written on purpose to break one rule, which the compiler must reject.

join point
: A block that two or more paths lead into.

keyword
: A word the language reserves, such as `let` or `while`, that cannot be used as a name.

known answer
: A result worked out independently, which a test compares the program's output against.

known limitation
: Something the language describes that this compiler does not yet do, written down so users are not surprised.

layout
: The full description of a value's size, alignment and the offset of every element or field.

lexer (scanner)
: The stage that groups characters into tokens.

lexical error
: A diagnostic saying some characters cannot form any valid token.

lifetime
: How long a variable exists while the program runs, as opposed to where its name is visible.

line and column
: A position a person can find: which row of the file, and how far along it. Both count from 1; the column counts characters, and a tab counts as one.

linker
: A tool that joins separately compiled pieces of machine code, such as your program and a runtime library, into one executable.

literal
: A value written directly in the source, such as 10 or "Vortex".

local variable
: A variable declared with `let` inside a function, which exists only while its block runs.

longest match (maximal munch)
: When several tokens could start at the same place, the lexer takes the longest valid one.

lookahead
: Peeking at upcoming tokens without using them up.

lookup
: Searching for the declaration a use refers to, starting in the use's own scope and moving outward.

loop nest
: Loops placed inside other loops.

loop variable
: The variable a `for` loop introduces. It takes the integer type of the range's ends, cannot be assigned, and is visible only inside the loop body.

lowering
: Rewriting a program in a simpler, more machine-like form, for example turning a syntax tree into a list of small ordered steps.

machine code
: Instructions in the numeric form a processor executes directly.

marker
: Symbols printed under a source line to show which characters a diagnostic means; a single `^` is called a caret.

matrix
: A rectangle of numbers arranged in rows and columns; in v0.1, a two-dimensional array.

matrix multiplication
: Combining matrices A and B into C, where each cell of C comes from one row of A and one column of B.

memory
: The large, numbered storage area a running program reads and writes.

mutable, immutable
: Allowed, or not allowed, to change after creation; in Vortex a place is mutable only when its root variable is declared with `let mut` or is a `&mut` reference, and parameters are immutable.

naive
: Written in the most direct way, with no attempt to make it fast.

name error
: The diagnostic category for unknown, duplicated or out-of-scope names and unresolved named types.

name (identifier)
: A word the programmer chose, such as width or Point; case-sensitive.

name resolution
: The stage that links each use of a name to its declaration and reports unknown, duplicate and out-of-scope names.

namespace
: A separate set of names. In Vortex, all functions, structs and `print` share one namespace, while each struct's field names form their own and do not clash with other names.

NaN
: "Not a number", a special floating-point value produced by operations such as `0.0 / 0.0`.

nanopass
: A style of compiler design that uses many very small passes, each doing one job.

negative test
: A test that passes only if the compiler rejects a bad program for the right reason at the right place.

nested scope
: A scope inside another; inner code sees outer names, but not the other way round.

node
: One item in a tree, standing for one construct.

note
: An extra line on a diagnostic, often pointing at a related place in the source.

object file
: Machine code that is not yet a complete program and may refer to code that lives elsewhere, such as `print`.

offset
: A position given as the number of bytes before it in the file.

padding
: Unused bytes placed between or after the parts of a value so that each part meets its alignment rule.

parameter
: A named, typed input in a function's declaration; in Vortex it cannot be assigned, though a `&mut` parameter can write the storage it refers to.

parse tree (concrete syntax tree)
: A tree with a node for every grammar rule the parser used, including punctuation.

parser
: The part of the compiler that reads tokens, checks them against the grammar, and builds a syntax tree.

pass
: One trip through the whole program by one part of the compiler, reading one representation and producing another or a list of problems.

phi
: In SSA form, a marker at the point where paths meet that picks which version of a value to use.

place
: A name followed by any index and field suffixes, such as `points[i].x`: something an assignment can target or `&` can borrow.

Pratt parsing
: A well-known technique for parsing expressions with many precedence levels, named after Vaughan Pratt.

precedence
: Which operator groups first when there are no parentheses.

primary span
: The one stretch of source a diagnostic is about, where its marker points.

primitive type
: A type built into the language and named by a keyword, such as `i32` or `bool`.

proven safe
: Known by the compiler, from the source alone, to be unable to fail.

rank
: The number of dimensions an array has.

rectangular matrix
: A matrix whose numbers of rows and columns differ.

recursive descent
: A hand-written parser in which each grammar rule becomes one piece, and rules call the pieces for the rules they contain.

reference
: A safe way to reach an existing value without copying it; `&T` can read it and `&mut T` can also change it.

register
: A small, very fast storage slot inside the processor.

regression
: Something that used to work and has stopped working.

release
: A version of the compiler that is given a name and offered to others as working, with its limits stated.

release gate
: The list of checks that must all pass before a release may be named.

reserved word
: A word kept back for a future version of the language, such as `i64` or `const`, that cannot be used as a name. See [decision 29](../../decisions/lexical.md#d29).

return value
: The value a function hands back to its caller.

root, child, leaf
: The top node of a tree, a node directly below another, and a node with no children.

row, column
: One horizontal line of a matrix, and one vertical line.

row-major order
: Storing a two-dimensional array one whole row after another; every Vortex array uses this order.

rule (production)
: One entry in the grammar describing the shape of one construct.

run time
: While the finished executable is running.

runtime
: The small library of code that ships inside every compiled program and provides services such as printing and error reporting.

runtime check
: A small test placed in the generated code before an operation that might be invalid.

runtime error
: What happens when a runtime check fails: the program stops and reports the problem.

runtime library
: A small body of code shipped with the language that generated programs call for services such as printing.

saturating
: Handling overflow by clamping to the largest or smallest value; v0.1 does not do this.

scope
: The region of source text where a declaration's name can be used.

shadowing
: Declaring a name in an inner scope that an outer scope already declares, so the inner one would take over inside; Vortex forbids it, and such a declaration is a name error.

shape
: The list of an array's sizes, one per dimension.

short-circuit evaluation
: Evaluating the right side of `&&` or `||` only when the left side has not already decided the answer.

size
: How many bytes a value takes up.

source code
: The text of a program as a person wrote it.

source file
: The file of Vortex text handed to the compiler.

source manager
: The part of the compiler that holds the source text and turns positions into locations, without interpreting any syntax.

source span
: The start and length, in bytes, of the source text a token or node came from.

spelling (lexeme)
: The exact characters a token was made from.

square matrix
: A matrix with as many rows as columns.

stack exhaustion
: Running out of the memory set aside for the call stack, usually through deep recursion; a Vortex program must then stop with a report.

stack frame
: One call's entry on the call stack, holding its parameters, its local variables and where to return to.

standard error
: A second text stream, kept apart from standard output, for error messages; the compiler writes its diagnostics there, and a compiled program writes its runtime error line there.

standard output
: The text stream a program writes to by default, which tests can capture.

statement
: A piece of code that does something; in Vortex a statement produces no value.

static rule
: A rule the compiler can check by reading the program, before it runs.

static single assignment (SSA) form
: A way of writing a program in which every variable is assigned exactly once, which makes many later analyses simpler.

symbol table
: The compiler's record of which names are declared in which scopes and what each refers to.

synchronization point
: A token, such as `;` or `}`, where the parser can safely start again after an error.

syntax error
: A report that the tokens do not match the grammar.

target
: The kind of machine and operating system the generated program is meant to run on.

terminating statement
: A statement after which a function cannot carry on to the next statement: a `return`, a block whose last statement terminates, or an `if` with an `else` whose branches both terminate. A loop never terminates.

test
: One small automatic check with a pass or fail answer.

test case
: One source file plus a description of what should happen to it.

test runner
: A program that finds test cases, runs the compiler on each, compares the result with the expected output, and reports pass or fail.

test suite
: The whole collection of automated tests for the compiler.

token
: One unit of the language, such as a keyword, name, number or symbol, with a kind, a spelling and a location.

token kind
: Which sort of token something is, such as keyword, identifier or integer literal.

token stream
: The whole sequence of tokens for a file, ending with an end-of-file token.

tokenizing
: Grouping characters into tokens.

tolerance
: How far apart two floating-point results may be and still count as equal.

toolchain
: The outside programs the compiler depends on, such as an assembler, a linker or a C compiler.

tree-walk interpreter
: A program that runs code by visiting the nodes of its syntax tree one by one, without producing machine code.

type
: A label that says what kind of value something is and what can be done with it.

type checking
: Working out the type of every expression and checking that operations, calls, assignments and returns use types that fit together.

type inference
: Working out a type the programmer did not write; in Vortex, only for local variables, from their starting values.

undefined behavior
: A situation where the rules say nothing about what happens next.

use
: A place where a name appears in order to refer to something already declared.

UTF-8
: The most common text encoding: one byte for plain English letters and digits, two to four bytes for other characters.

valid and invalid pair
: Two nearly identical test programs, one accepted and one rejected, that together show a rule is enforced exactly where it should be.

valid program
: A program the specification says must be accepted.

version number
: The name given to a release, such as v0.1.0, so people can say exactly which compiler they used.

vertical slice
: A thin piece of work that goes through every stage at once, such as compiling one tiny program all the way to an executable.

visible
: A declaration is visible at a point if its name can be used there.

warning
: A diagnostic that points out a likely mistake without rejecting the program; Vortex never requires one, and it never changes the result of compiling.

well-formed, ill-formed
: Following every required rule, or breaking at least one; ill-formed programs must be rejected.

whitespace
: Spaces, tabs and line breaks; they separate tokens and mean nothing else.

wrapping
: Handling overflow by silently dropping the high bits; v0.1 does not do this.
