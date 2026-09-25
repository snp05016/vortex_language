# 8. Data in memory

<p class="page-intro">Strings, arrays, structs and references in compiled programs. This is the stage where you work out, and write down, where every part of a value lives in memory.</p>

Until now, every value the generated code handled was small: one integer, one
floating-point number, one boolean. Each fitted in a single slot. Stage 8 adds
values made of many parts. An array of sixteen numbers, a struct with three
fields, and a string of text all need room for several pieces at once, and the
generated code needs a way to find each piece.

This work sits on the right-hand slope of the
[compiler mountain](index.md#the-shape-of-the-whole-thing), in lowering and
code generation. The front end already did the hard checking back in
[stage 5](stage-5-types-and-rules.md). By the time a program reaches this
stage, every array shape is a known list of integers, every field name is
resolved, and every reference has passed the mutability rules. What is left is
to give all of that a physical form.

--8<-- "includes/remember/compiler__guide__stage-8-data-in-memory.md"

!!! goals "In this stage"

    - Write a layout document that records the size, alignment, offsets and
      padding of every v0.1 type.
    - Compute the offset of an array element in row-major order, for arrays
      of rank 1 and higher.
    - Store struct fields in declaration order and account for padding
      between and after them.
    - Store a string literal's UTF-8 bytes and distinguish a byte count from
      a character count.
    - Pass a reference that reaches the caller's storage without a copy, and
      pass an array or struct by value so that the callee cannot change the
      caller's value.

## What this stage is for

The roadmap's
[Milestone 8](../../roadmap.md#milestone-8-strings-arrays-structs-and-references)
lists the items: UTF-8 string literals for printing, fixed-size arrays of one
or more dimensions, array literals and repeat arrays such as `[0.0; 16]`,
expression dimensions such as `[0.0; 2 + 2, 8]`, struct creation and field
access, and shared and mutable references in function calls.

One item on that list is different from the rest. Besides implementing the
layout, it asks for a document: "document sizes, alignment, padding, and how
references are represented". The specification fixes the order: arrays are
contiguous and row-major, and struct fields are stored in declaration order
([arrays, 7.8](../../specification/arrays.md#78-memory-and-layout),
[structs, 8.8](../../specification/structs.md#88-layout),
[decision](../../decisions/arrays.md#d43)). Sizes, alignment and padding are
implementation-defined, so your layout document records them. This page
explains what the document covers.

## Words for this stage

memory
: The large, numbered storage area a running program reads and writes. Think
  of it as one very long row of small boxes.

byte
: One of those small boxes. A byte holds 8 bits. An `f32` is 32 bits, so it
  fills four bytes.

address
: The number of a byte in memory. If you know where a value starts, its
  address is the number of its first byte.

size
: How many bytes a value takes up.

alignment
: A rule that a value must start at an address that is a multiple of some
  number, often its own size. Many processors read a four-byte number faster,
  or only correctly, when it starts at a multiple of four.

padding
: Unused bytes placed between or after parts of a value so that each part
  meets its alignment rule.

layout
: The complete description of where each part of a value sits: its size, its
  alignment, and the offset of every element or field.

offset
: The distance, in bytes, from the start of a value to one of its parts.

contiguous
: Stored side by side with no gaps. The arrays chapter says an array "is
  stored contiguously".

aggregate
: A value made of other values. In Vortex v0.1 the aggregates are arrays and
  structs.

rank
: The number of dimensions an array has. `[f32; 4]` has rank 1 and
  `[f32; 2, 3]` has rank 2.

shape
: The list of an array's sizes, one per dimension. The shape of
  `[f32; 2, 3]` is `[2, 3]`.

row-major order
: Storing a two-dimensional array one whole row after another, so that
  neighbours in a row are neighbours in memory. Every Vortex array uses it.

column-major order
: Storing a two-dimensional array one whole column after another, so that
  neighbours in a column are neighbours in memory.

field
: One named part of a struct, such as `x` in `Point`.

reference
: A value that lets code reach some existing storage without making a copy of
  what is stored there. `&T` is a shared reference and `&mut T` is a mutable
  one.

UTF-8
: A standard way of storing text as bytes. Plain English letters take one byte
  each; many other characters take two, three or four.

ABI
: Short for application binary interface. The low-level agreement about how
  values are laid out and passed between functions in machine code.

## Memory, addresses and layout

A running program sees memory as a single long row of bytes, each with a
number. Every value the program stores occupies some run of those bytes. For a
single `i32`, the whole story is "four bytes, starting here". For an aggregate,
you need more: where does the third element start, where does the field
`charge` start, how many bytes does the whole thing take?

The answers to those questions are the **layout**. A layout is not something
the generated code discovers while it runs. It is fixed before any code is
produced, because every array shape and every struct definition is known at
compile time. That is exactly why Vortex v0.1 insists on compile-time
dimensions: the types chapter says types determine "storage layout", and the
arrays chapter says an array's dimensions "are known before storage layout is
generated".

Your layout document needs to answer, for every v0.1 type:

- how many bytes it takes;
- what alignment it needs;
- for an array, where element `[i, j, ...]` sits relative to the start;
- for a struct, where each field sits, and whether there is padding;
- for a reference, what is actually stored when a reference is passed around.

Some of these answers come almost for free. `i32` and `u32` are 32 bits,
`f32` is 32 bits and `f64` is 64 bits, straight from the
[types chapter](../../specification/types-and-values.md). Others are left open
on purpose. The spec says a `bool` is `true` or `false` but the
[language tour](../../language-tour/04-variables-and-types.md) adds that Vortex
"does not promise" it uses exactly one bit. A `char` holds one Unicode scalar
value, a number up to 0x10FFFF outside the surrogate range 0xD800 to 0xDFFF
([decision 15](../../decisions/lexical.md#d15)), and "is not defined as a
one-byte value". The width of `usize` is implementation-defined: your compiler
must document it, and on every v0.1 target it is 64 bits, eight bytes
([decision](../../decisions/numbers.md#d42)). Each of those is a line in your
layout document. [I6](../../decisions/implementation.md#i6) suggests an
answer for each: 4 bytes for `i32`, `u32`, `f32` and `char`, 8 for `f64` and
`usize`, and 1 for `bool`, each aligned to its own size.

## Arrays in memory

A one-dimensional array is the easy case. `[f32; 4]` is four `f32` values
placed side by side, sixteen bytes in all. Element `values[2]` starts eight
bytes after the start, because two four-byte elements come before it.

Two dimensions are where an order is needed. Memory is a single row, but a
`[f32; 2, 3]` has two rows and three columns. Something has to decide which
cell comes after which. There are two common answers.

<figure class="vx-figure">
<svg viewBox="0 0 760 350" role="img" aria-labelledby="s8-grid-title s8-grid-desc">
<title id="s8-grid-title">A two-by-three array and the same array laid out in memory in row-major order</title>
<desc id="s8-grid-desc">At the top, a grid with two rows and three columns labelled a[0, 0] through a[1, 2]. Below it, a single strip of six memory cells holding the same elements in row-major order: the whole of row 0, then the whole of row 1, at byte offsets 0, 4, 8, 12, 16 and 20. A highlight walks through the cells in memory order, in both pictures at once.</desc>
<text class="vx-text" x="90" y="30">a: [f32; 2, 3] as you index it</text>
<text class="vx-text-muted" x="130" y="68" text-anchor="middle">column 0</text>
<text class="vx-text-muted" x="210" y="68" text-anchor="middle">column 1</text>
<text class="vx-text-muted" x="290" y="68" text-anchor="middle">column 2</text>
<text class="vx-text-muted" x="80" y="110" text-anchor="end">row 0</text>
<text class="vx-text-muted" x="80" y="160" text-anchor="end">row 1</text>
<rect class="vx-box" x="90" y="80" width="80" height="50"/>
<rect class="vx-box" x="170" y="80" width="80" height="50"/>
<rect class="vx-box" x="250" y="80" width="80" height="50"/>
<rect class="vx-box" x="90" y="130" width="80" height="50"/>
<rect class="vx-box" x="170" y="130" width="80" height="50"/>
<rect class="vx-box" x="250" y="130" width="80" height="50"/>
<rect class="vx-box-accent" x="90" y="80" width="80" height="50">
<animate attributeName="x" values="90;170;250;90;170;250" keyTimes="0;0.1667;0.3333;0.5;0.6667;0.8333" dur="7.2s" calcMode="discrete" repeatCount="indefinite"/>
<animate attributeName="y" values="80;80;80;130;130;130" keyTimes="0;0.1667;0.3333;0.5;0.6667;0.8333" dur="7.2s" calcMode="discrete" repeatCount="indefinite"/>
</rect>
<text class="vx-mono" x="130" y="110" text-anchor="middle">a[0, 0]</text>
<text class="vx-mono" x="210" y="110" text-anchor="middle">a[0, 1]</text>
<text class="vx-mono" x="290" y="110" text-anchor="middle">a[0, 2]</text>
<text class="vx-mono" x="130" y="160" text-anchor="middle">a[1, 0]</text>
<text class="vx-mono" x="210" y="160" text-anchor="middle">a[1, 1]</text>
<text class="vx-mono" x="290" y="160" text-anchor="middle">a[1, 2]</text>
<text class="vx-text" x="390" y="100">Row-major: offset of a[r, c]</text>
<text class="vx-mono" x="390" y="124">(r × 3 + c) × 4 bytes</text>
<text class="vx-text-muted" x="390" y="152">Column-major would store a[0, 0], a[1, 0],</text>
<text class="vx-text-muted" x="390" y="168">a[0, 1], a[1, 1], a[0, 2], a[1, 2] instead.</text>
<text class="vx-text" x="40" y="218">The same array in memory, row-major (the order Vortex requires)</text>
<rect class="vx-box" x="40" y="230" width="110" height="50"/>
<rect class="vx-box" x="150" y="230" width="110" height="50"/>
<rect class="vx-box" x="260" y="230" width="110" height="50"/>
<rect class="vx-box" x="370" y="230" width="110" height="50"/>
<rect class="vx-box" x="480" y="230" width="110" height="50"/>
<rect class="vx-box" x="590" y="230" width="110" height="50"/>
<rect class="vx-box-accent" x="40" y="230" width="110" height="50">
<animate attributeName="x" values="40;150;260;370;480;590" keyTimes="0;0.1667;0.3333;0.5;0.6667;0.8333" dur="7.2s" calcMode="discrete" repeatCount="indefinite"/>
</rect>
<text class="vx-mono" x="95" y="260" text-anchor="middle">a[0, 0]</text>
<text class="vx-mono" x="205" y="260" text-anchor="middle">a[0, 1]</text>
<text class="vx-mono" x="315" y="260" text-anchor="middle">a[0, 2]</text>
<text class="vx-mono" x="425" y="260" text-anchor="middle">a[1, 0]</text>
<text class="vx-mono" x="535" y="260" text-anchor="middle">a[1, 1]</text>
<text class="vx-mono" x="645" y="260" text-anchor="middle">a[1, 2]</text>
<text class="vx-text-muted" x="95" y="298" text-anchor="middle">byte 0</text>
<text class="vx-text-muted" x="205" y="298" text-anchor="middle">byte 4</text>
<text class="vx-text-muted" x="315" y="298" text-anchor="middle">byte 8</text>
<text class="vx-text-muted" x="425" y="298" text-anchor="middle">byte 12</text>
<text class="vx-text-muted" x="535" y="298" text-anchor="middle">byte 16</text>
<text class="vx-text-muted" x="645" y="298" text-anchor="middle">byte 20</text>
<line class="vx-line" x1="44" y1="314" x2="366" y2="314"/>
<line class="vx-line" x1="374" y1="314" x2="696" y2="314"/>
<text class="vx-text-muted" x="205" y="334" text-anchor="middle">all of row 0</text>
<text class="vx-text-muted" x="535" y="334" text-anchor="middle">all of row 1</text>
</svg>
<figcaption>Figure 1. A two-by-three array of <code>f32</code>, first as the program indexes it and then as it sits in memory. The highlight walks the cells in memory order. In row-major order it moves along a row before dropping to the next one. The byte offsets assume four-byte elements with no gaps.</figcaption>
</figure>

**Row-major order** stores all of row 0, then all of row 1. **Column-major
order** stores all of column 0, then all of column 1, and so on. Both orders
are in wide use in numerical software. Vortex uses row-major order: the last
index varies fastest. Write it into your layout document.

The order never changes what a program computes. `a[1, 2]` means row 1,
column 2, however the bytes are arranged, and the arrays chapter notes that a
v0.1 program "cannot observe addresses, so layout does not change results".
What the order changes is speed. A processor reads memory in chunks, and
reading neighbouring bytes is much cheaper than jumping around. In matrix
code, the innermost loop usually
walks along one row or down one column. If the loop walks along the same
direction the memory runs, every step reads the next few bytes. If it walks
across, every step jumps a whole row ahead. For the small arrays of v0.1 the
difference hardly shows, but it is the first thing the post-v0.1 optimization
work in the roadmap will care about. Because the order is fixed, the optimizer
can rely on it.

--8<-- "includes/examples/build-v0.1/stage-8-data-in-memory/row_major_offset.cpp.md"

??? check "For `b: [f32; 3, 5]`, at what byte offset does `b[2, 1]` start, with four-byte elements?"

    `(2 × 5 + 1) × 4 = 44`. Rows 0 and 1 come first, five elements each, so
    row 2 starts 10 elements in, and column 1 is one element further. Using
    the number of rows (3) in place of the number of columns (5) would give
    `(2 × 3 + 1) × 4 = 28`, a plausible but wrong address.

Arrays of higher rank follow the same idea. A rank-3 array is a list of
rank-2 blocks, and the last index changes fastest as you move through memory.

### Nested arrays are a different type

Vortex has two ways to write something that looks like a grid. `[f32; 3, 2]`
is one array of rank 2. `[[f32; 2]; 3]` is an array of rank 1 whose three
elements happen to be arrays. The
[type equality rule](../../specification/types-and-values.md#411-type-equality)
includes rank, so these are different types, and a program that mixes them is
rejected in the front end. A nested list literal may still fill a
`[f32; 3, 2]` when that type is written, because each inner list is checked as
one row ([decision](../../decisions/arrays.md#d21)). The result is a value of
the rank-2 type, not of the nested one.

Both are stored as six numbers in a row, in the same order; the specification
requires that. What is not allowed is letting that shared layout leak back
into the language, so that one type is quietly accepted where the other was
required.

??? check "`[f32; 3, 2]` and `[[f32; 2]; 3]` end up with the same six numbers in the same order. Why are they still different types?"

    Rank is part of a type's identity, separate from its layout. The [type
    equality rule](../../specification/types-and-values.md#411-type-equality)
    compares rank, so a rank-1 array of rank-1 arrays is never the same type
    as a rank-2 array, no matter how the bytes happen to line up.

### Expression dimensions and total size

The roadmap asks for expression dimensions in array types and repeat arrays,
such as `[0.0; 2 + 2, 8]`. By this stage those expressions are already
numbers. The type checker in [stage 5](stage-5-types-and-rules.md) evaluated
`2 + 2` to `4` when it resolved the array type, before any shapes were compared
([decision](../../decisions/arrays.md#d52)). The tour puts it plainly: code
generation "receives the final fixed layout rather than evaluating dimensions
at runtime".

So `[f32; 2 + 2, 8]` arrives here as shape `[4, 8]`: thirty-two elements,
128 bytes under four-byte `f32`. The arrays chapter also makes it a
constant-evaluation error when "the total size of the array exceeds the
implementation's documented limit", a limit your compiler documents like the
other entries in
[Conformance 1.10](../../specification/conformance.md#110-implementation-defined-behavior-and-limits).
Stage 5 reports that error before layout, but the layout step is the first
consumer that depends on the limit, so make sure the two agree about it.

### Repeat arrays and element lists

A repeat array such as `[0.0; 16]` or `[0.0; 4, 4]` fills every element with
one value. The spec is precise about one detail that is easy to get wrong: the
value before the `;` "is evaluated once, and its resulting value fills the
array". If that value came from a function call, the call happens one time,
not sixteen.

An element-list array such as `[1.0, 2.0, 3.0]` works the other way: each
element has its own expression, and the
[expressions chapter](../../specification/expressions.md#510-evaluation-order)
requires them to be evaluated left to right. Both rules are visible to a
program whose expressions print something, so both deserve a test.

Storing into an element has an order too. For `values[i] = next();` the index
`i` is evaluated first, then `next()`, then the bounds check, then the store. A
compound assignment such as `values[i] += next();` loads, adds and checks the
addition between the bounds check and the store
([decision 38](../../decisions/statements.md#d38)). A `next` that prints makes
the order visible.

Zero-length arrays never reach this stage: stage 5 rejects a zero extent as a
constant-evaluation error ([decision](../../decisions/arrays.md#d10)), so every
array has at least one element.

## Strings

For v0.1, strings are small in scope. The roadmap asks for "basic UTF-8 string
literals for printing and diagnostics", and the types chapter lists what is
not yet specified: mutation, indexing, interpolation, concatenation, searching
and parsing. What the compiler needs is to take a literal such as
`"line one\nline two"`, with its escape sequences already decoded by the
[lexer](stage-2-lexer.md), store its bytes somewhere in the executable, and
hand them to `print`.

```vortex
// program: valid
fn main() {
    print("Hello, world!");
    print("λ is one character but two bytes");
}
```

`print` writes a `String` argument's bytes exactly as stored, without quotes,
and then a line feed
([Programs and declarations 3.9](../../specification/declarations.md#39-built-in-functions),
[decision](../../decisions/program.md#d4)). This program writes two lines:
`Hello, world!` and `λ is one character but two bytes`.

Two decisions go in the layout document. The first is how the program knows
where a string ends. A string's size is not part of its type the way an array's
is, so something must carry it: a stored length, or a special end marker, or
something else. The second is the difference between characters and bytes.
`λ` is one character and two bytes in UTF-8. Any count your runtime reports,
and any test that checks printed output, must be clear about which of the two
it means. [I6](../../decisions/implementation.md#i6) suggests a stored length:
a `String` is the address of its UTF-8 bytes and a length in bytes, and every
count the runtime keeps is in bytes.

--8<-- "includes/examples/build-v0.1/stage-8-data-in-memory/utf8_length.cpp.md"

??? check "A string literal holds one `λ` and nothing else. Is its stored length 1 or 2?"

    2. `λ` is one character but two bytes in UTF-8, and I6's stored length
    counts bytes, not characters. A runtime that counted characters instead
    would have to decode the UTF-8 bytes to answer "how long is this
    string?".

## Structs in memory

A struct is a named group of fields. The layout question for a struct is the
same as for an array (where does each part start?) with one new wrinkle:
fields can have different sizes and different alignment rules.

```vortex
// items: valid
struct Particle {
    mass: f32,
    charge: i32,
    alive: bool,
}
```

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-labelledby="s8-struct-title s8-struct-desc">
<title id="s8-struct-title">The fields of a Particle struct placed in memory</title>
<desc id="s8-struct-desc">On the left, the three field declarations of Particle. On the right and below, a strip of memory showing mass at bytes 0 to 3, charge at bytes 4 to 7, alive starting at byte 8, then a gap marked as possible padding. Each field lights up together with its place in memory.</desc>
<text class="vx-text" x="40" y="36">The declaration</text>
<text class="vx-mono" x="40" y="66">struct Particle {</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<text class="vx-mono" x="72" y="90">mass: f32,</text>
<rect class="vx-box-accent" x="40" y="190" width="200" height="54"/>
<text class="vx-mono" x="140" y="222" text-anchor="middle">mass</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<text class="vx-mono" x="72" y="114">charge: i32,</text>
<rect class="vx-box-accent" x="240" y="190" width="200" height="54"/>
<text class="vx-mono" x="340" y="222" text-anchor="middle">charge</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<text class="vx-mono" x="72" y="138">alive: bool,</text>
<rect class="vx-box-accent" x="440" y="190" width="110" height="54"/>
<text class="vx-mono" x="495" y="222" text-anchor="middle">alive</text>
</g>
<text class="vx-mono" x="40" y="162">}</text>
<text class="vx-text" x="330" y="36">Questions the layout document answers</text>
<text class="vx-text-muted" x="330" y="62">Where does each field start?</text>
<text class="vx-text-muted" x="330" y="84">How many bytes is a bool?</text>
<text class="vx-text-muted" x="330" y="106">Is there padding, and where?</text>
<text class="vx-text-muted" x="330" y="128">What is the whole struct's size and alignment?</text>
<rect class="vx-box" x="550" y="190" width="170" height="54"/>
<text class="vx-text-muted" x="635" y="222" text-anchor="middle">padding, if any</text>
<text class="vx-text-muted" x="40" y="266">byte 0</text>
<text class="vx-text-muted" x="240" y="266">byte 4</text>
<text class="vx-text-muted" x="440" y="266">byte 8</text>
<text class="vx-text-muted" x="550" y="266">size decided by you</text>
<text class="vx-mono" x="40" y="292">p.charge reads 4 bytes starting 4 bytes into p</text>
</svg>
<figcaption>Figure 2. One possible layout for <code>Particle</code>: fields in declared order, each four-byte field at a multiple of four, and room at the end that may be padding. The field order is fixed by the specification; offsets, padding and alignment are implementation-defined, and your layout document pins them down.</figcaption>
</figure>

The AST keeps the fields in declared order, and the
[structs chapter](../../specification/structs.md#88-layout) keeps that order in
memory. It requires declaration order and leaves padding and alignment to the
implementation, which must document them, so fields may not be reordered to
save padding. Whatever the padding, **field access** becomes the same thing
array indexing became: a known offset from the start of the value. `p.charge`
in Figure 2 means "the four bytes that start four bytes into `p`". The field
name is gone by the time the machine code runs.

--8<-- "includes/examples/build-v0.1/stage-8-data-in-memory/struct_layout.cpp.md"

??? check "In the C++ example, the last field ends at byte 9, but `sizeof(Reading)` is 12. What are the other 3 bytes for?"

    Trailing padding. C++ rounds a struct's size up to a multiple of its
    alignment, 4 here, so that in an array of `Reading` every element's
    `value` still starts at a multiple of four. The padding sits between
    every pair of elements, not only at the end of the array. I6 suggests the
    same rule for Vortex structs.

Nested structs and arrays of structs follow from the same rules. The spec's
example `points[index].position.x` is an array index, then a field offset,
then another field offset, all added up. If each piece has a documented
layout, the combination needs no new decision. Stage 5 has already rejected any
struct that contains itself ([decision](../../decisions/operators.md#d45)), so
every struct has a finite size and working out sizes from the inside out
always ends.

A few struct rules from the spec affect this stage directly:

- **Struct types are nominal.** `Position { x: f32, y: f32 }` and
  `Velocity { x: f32, y: f32 }` may have identical layouts, yet they remain
  different types. Identical bytes do not make them interchangeable.
- **Structs are values.** A struct expression "creates a value rather than an
  object with independent identity". Every initialization, assignment,
  by-value argument and return copies the whole struct, and there are no
  moves; how the copy happens at the machine level is left to you, as long as
  the program cannot tell ([decision](../../decisions/references.md#d25)).
- **Every struct has at least one field.** An empty struct such as
  `struct Marker {}` is a syntax error
  ([decision](../../decisions/operators.md#d26)), so the layout document never
  needs a zero-size struct.

## References and passing without copying

A **reference** is how one part of a program reaches storage that belongs to
another part. The roadmap's test for this stage is specific: "Add tests that
pass arrays into functions through references, without copying them." The
spec's own example shows why it matters.

```vortex
// program: valid
fn scale(values: &mut [f32; 4], factor: f32) {
    for index in 0..4 {
        values[index] *= factor;
    }
}

fn main() {
    let mut values = [1.0, 2.0, 3.0, 4.0];
    scale(&mut values, 2.0);
    print(values[3]);
}
```

After the call, `main` must see the changed values. That is only possible if
`scale` worked on `main`'s own array rather than on a copy of it.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-labelledby="s8-ref-title s8-ref-desc">
<title id="s8-ref-title">A mutable reference pointing from a function parameter to the caller's array</title>
<desc id="s8-ref-desc">On the left, the function main owns an array of four numbers. On the right, the function scale has one parameter, values, of type ampersand mut array of four f32. An arrow flows from the parameter back into main's array. There is no second copy of the array.</desc>
<rect class="vx-box-strong" x="30" y="40" width="380" height="200"/>
<text class="vx-text" x="50" y="66">main</text>
<text class="vx-mono" x="50" y="96">let mut values = [1.0, 2.0, 3.0, 4.0];</text>
<rect class="vx-box" x="60" y="120" width="80" height="50"/>
<rect class="vx-box" x="140" y="120" width="80" height="50"/>
<rect class="vx-box" x="220" y="120" width="80" height="50"/>
<rect class="vx-box" x="300" y="120" width="80" height="50"/>
<text class="vx-mono" x="100" y="150" text-anchor="middle">1.0</text>
<text class="vx-mono" x="180" y="150" text-anchor="middle">2.0</text>
<text class="vx-mono" x="260" y="150" text-anchor="middle">3.0</text>
<text class="vx-mono" x="340" y="150" text-anchor="middle">4.0</text>
<text class="vx-text-muted" x="60" y="196">the only storage for the array</text>
<text class="vx-text-muted" x="60" y="220">after the call it holds 2.0, 4.0, 6.0, 8.0</text>
<rect class="vx-box-strong" x="470" y="40" width="260" height="200"/>
<text class="vx-text" x="490" y="66">scale</text>
<rect class="vx-box-accent" x="500" y="120" width="200" height="50"/>
<text class="vx-mono" x="600" y="142" text-anchor="middle">values</text>
<text class="vx-text-muted" x="600" y="160" text-anchor="middle">&amp;mut [f32; 4]</text>
<text class="vx-text-muted" x="500" y="196">holds a way to reach main's</text>
<text class="vx-text-muted" x="500" y="214">array, not the array itself</text>
<path class="vx-flow" d="M500 145 L392 145"/>
<polygon class="vx-arrowhead" points="382,145 394,139 394,151"/>
<text class="vx-mono" x="40" y="280">scale(&amp;mut values, 2.0);</text>
<text class="vx-text-muted" x="290" y="280">one array, reached from two places</text>
</svg>
<figcaption>Figure 3. A mutable reference. The parameter in <code>scale</code> does not hold four numbers; it holds a way to reach the four numbers that live in <code>main</code>. Writes through it land in <code>main</code>'s array.</figcaption>
</figure>

In the language, a reference is not a number. The spec rules out pointer
arithmetic, null references, address casts and raw pointers. Inside the
executable, though, a reference has to be *something*, and on most machines
the natural something is the address where the referenced storage starts.
What a reference occupies when it is passed to a function is part of your
layout document; I6 suggests exactly that: an 8-byte address.

The rules about who may hold which reference were checked in
[stage 5](stage-5-types-and-rules.md): a shared reference `&T` only reads, a
mutable reference `&mut T` needs a mutable place, and while a `&mut` borrow is
live its variable is used only through it. Stage 8 does not re-check these
rules, but it must not weaken them. They give you a guarantee the
[references chapter](../../specification/references.md#98-aliasing) states:
the storage behind a `&mut` parameter is not reachable through any other
parameter of the same call. The code you generate may rely on it, because the
front end has checked it ([decision](../../decisions/references.md#d41)).

Passing an array *without* a reference is also legal. A parameter of type
`[f32; 4]` receives a copy of the array
([decision](../../decisions/references.md#d25)). Parameters are immutable
([decision](../../decisions/references.md#d23)), and a variable passed as
`&mut` cannot appear in another argument of the same call, so the callee can
never tell a real copy from a hidden address of the caller's array. The choice
is yours, and it goes in the layout document with everything else.

--8<-- "includes/examples/build-v0.1/stage-8-data-in-memory/reference_vs_copy.cpp.md"

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- A written layout document: size, alignment and element or field offsets for every v0.1 type, following the roadmap's instruction to document the layout.
- Row-major array order, as the specification requires, recorded in the layout document: the optimizer and every test depend on it.
- Arrays of rank 1 and higher, with element reads and writes: matrix multiplication in stage 10 needs rank 2.
- Array literals evaluated left to right and repeat arrays whose value is evaluated once: both are spec rules a program can observe.
- Shapes that arrive as plain integers from stage 5: code generation never evaluates a dimension at run time.
- String literals stored as UTF-8 bytes and printed exactly: the first visible output of most test programs.
- Struct construction, field reads and field writes through mutable storage.
- Element and field stores in the order of decision 38: the target's indices, then the value, then the check and the store.
- Shared and mutable reference parameters that reach the caller's storage with no copy.
- Array and struct parameters passed by value, with the passing method written down.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Bounds checks on indexing: they are [stage 9](stage-9-runtime-safety.md), though you may build the index path with them in mind.
- String operations beyond literals and `print`: indexing, joining and searching are not specified in v0.1.
- Runtime-sized arrays, slices, vectors or views: excluded from v0.1 by the arrays chapter.
- Manual or heap allocation that the programmer can see: the references chapter excludes manual allocation.
- References stored in structs or arrays, or returned from functions: v0.1 allows references only as parameters and as `let` bindings without `mut`.
- Matching the C ABI or any other outside layout: the spec says ABI compatibility is not yet specified.
- Tiled or blocked copies made for speed: post-v0.1 optimization work, and they never change the row-major layout of a Vortex array.
- Zero-length arrays: they do not exist in v0.1, because stage 5 rejects a zero extent.

</div>
</div>

## What you do not need yet

The right-hand column above lists what can wait. The common thread is that
v0.1 data never changes size while the program runs and never outlives the
block that created it. Everything that would break either of those
properties belongs to a later version.

## How you know it is finished

The roadmap says
[Milestone 8](../../roadmap.md#milestone-8-strings-arrays-structs-and-references)
is complete "when compiled programs can safely create, read, change, and pass
v0.1 data types". In practice, that means compiled and executed programs, with
their printed output checked, for at least these cases:

- A string literal with an escape sequence and a non-ASCII character prints
  exactly the expected bytes.
- A rank-1 array and a rank-2 array are written element by element and read
  back, including the last element in every dimension.
- A **non-square** rank-2 array, such as `[f32; 2, 3]`, is filled and read
  back. A square array cannot tell you if row and column were swapped.
- An array with expression dimensions such as `[0.0; 2 + 2, 8]` behaves the
  same as one written `[0.0; 4, 8]`.
- A repeat array whose value comes from a call that prints shows one line of
  output, not one per element.
- A struct is created, a field is changed through a `let mut` binding, and
  every field reads back correctly, including fields after one of a different
  size.
- An array passed as `&mut` is changed by the callee and the caller sees the
  change.
- An array passed by value can be read in full by the callee, including its
  last element.
- Assigning an array or a struct to a new variable and then changing the
  original leaves the copy unchanged.
- Your layout document exists, is linked from the compiler documentation, and
  matches what the tests observe.

## Traps

**Keeping the layout only in the code.** The spec says sizes, alignment and
padding "must be documented". If the only record is the code generator, nobody
can check it, and
the first optimizer change will move something without anyone noticing.

**Testing only square arrays.** In a 2 by 2 or 4 by 4 array, swapping row and
column in the offset calculation still produces plausible numbers. Use shapes
like 2 by 3 and 3 by 5, and check a cell whose row and column differ.

**Treating nested and multidimensional arrays as the same type.** They share
a layout. They do not share a type. The front end keeps them apart, and
nothing in this stage should blur that.

**Evaluating a repeat value once per element.** It is tempting to generate
"fill each element with this expression". The spec says the expression runs
once. The difference shows the moment the expression has a visible effect.

**Counting characters as bytes.** `char` is not one byte and a UTF-8 string's
length in bytes is not its number of characters. Decide which one each part of
your runtime means and say so.

**Forgetting padding in sizes.** If the struct in Figure 2 needs padding at the
end, an array of `Particle` must include that padding between elements too,
or the second element's fields will be read from the wrong place.

**Copying when the program asked for a reference, or sharing when it asked
for a value.** Both mistakes compile and run. Only a test that changes data on
one side of a call and looks on the other side will catch them.

## Key ideas

!!! recap

    - **Which index of a Vortex array varies fastest in memory, and who
      chooses that?**
      The last one (row-major order). The specification fixes it; your layout
      document records it but does not choose it.
    - **Why are `[f32; 3, 2]` and `[[f32; 2]; 3]` different types even though
      they hold the same six numbers in the same order?**
      Type equality compares rank, and layout is not part of a type's
      identity. Sharing a layout does not make two types interchangeable.
    - **Why can a string's length not be read from its type, the way an
      array's size can?**
      A string's size is not part of its type in v0.1. Something else, such
      as a stored length, has to carry it.
    - **When do a string's length in bytes and its length in characters
      differ, and which one does I6 suggest the runtime keep?**
      They differ as soon as one character takes more than one UTF-8 byte.
      I6 suggests counting bytes, and every count should say which it means.
    - **May the code generator reorder a struct's fields to save padding?**
      No. The structs chapter requires declaration order; only padding and
      alignment are left to you, and your layout document records them.
    - **What does a `&mut [f32; 4]` parameter hold at run time, and what does
      it not hold?**
      A way to reach the caller's array, an 8-byte address in I6's
      suggestion. It does not hold a copy of the four numbers.
    - **Why can the code generator trust that a `&mut` parameter never
      aliases another parameter of the same call?**
      Stage 5 already rejects a call that passes a `&mut` variable in
      another argument too. Stage 8 relies on that guarantee instead of
      checking it again.

## Where this comes back

--8<-- "includes/next/compiler__guide__stage-8-data-in-memory.md"

## How others teach this stage

**Kaleidoscope.** The LLVM tutorial's language has a single type, a
floating-point number, so it never needs aggregate layout. Its chapter 7 is
still worth reading for how it treats plain variables: "Each mutable variable
becomes a stack allocation", read and written through memory, and LLVM tidies
this up afterwards.[^kal7] The concluding chapter lists arrays and
structs as extensions for the reader and points at LLVM's instruction for
computing addresses of parts of aggregates.[^kal10] Vortex differs in that
aggregates are part of v0.1, not an exercise.

**Mapping High Level Constructs to LLVM IR.** This guide shows how familiar
language features look in LLVM's intermediate form. Its page on structures
notes that "structure members are referenced by index rather than by name",
which is the same observation as Figure 2: field names disappear, offsets
remain.[^mapping] Its page on functions covers passing structs by value and
by pointer. Read it for the vocabulary, and remember that it describes C-like
languages, whose rules on copying and aliasing are not Vortex's.

**The LLVM Language Reference.** If you choose LLVM as a backend, its type
system already has what Vortex needs. Its array type places elements one
after another in memory, and it writes a multidimensional array as an array
of arrays, such as a 3 by 4 array of integers.[^langref] Its structure type
inserts padding between fields according to the target's data layout, unless
the struct is declared packed, and that data layout records facts such as
byte order and alignment for the target.[^langref] That is a useful model
for what your own layout document must cover, whatever backend you use. Note
that LLVM's nested arrays are an encoding: Vortex still treats `[f32; 3, 4]`
and `[[f32; 4]; 3]` as different types.

**Ghuloum.** In the incremental Scheme compiler, pairs, vectors and strings
are too big for a single machine word, so they are allocated from a heap, with a small
tag in every pointer so the program can tell types apart at run
time.[^ghuloum] Vortex v0.1 is simpler here. Every size is known before the
program runs, the language has no user-visible allocation, and every type is
checked before code exists, so nothing in the spec calls for run-time type
tags.

[^kal7]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 7, "Extending the Language: Mutable Variables". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl07.html>
[^kal10]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 10, "Conclusion and other useful LLVM tidbits". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl10.html>
[^mapping]: Michael Rodler and Mikael Egevig, *Mapping High Level Constructs to LLVM IR*, pages "Structures" and "Function Definitions and Declarations". <https://mapping-high-level-constructs-to-llvm-ir.readthedocs.io/en/latest/>
[^langref]: LLVM Project, *LLVM Language Reference Manual*, sections "Data Layout", "Array Type" and "Structure Type". <https://llvm.org/docs/LangRef.html>
[^ghuloum]: Abdulaziz Ghuloum, "An Incremental Approach to Compiler Construction", *Proceedings of the 2006 Scheme and Functional Programming Workshop*, University of Chicago Technical Report TR-2006-06, section 3.7, "Heap Allocation". <http://scheme2006.cs.uchicago.edu/11-ghuloum.pdf>
