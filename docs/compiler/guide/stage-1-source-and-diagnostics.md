# 1. Source text and error messages

<p class="page-intro">The compiler learns to read a Vortex file, to name any place in it by line and column, and to report a problem at that place in one consistent format.</p>

A compiler spends a surprising share of its life saying no. The
[specification](../../specification/index.md) lists many programs that must be
rejected, and for each one the programmer deserves to be told what went wrong
and where. This stage builds the machinery for the "where" and for the shape of
the message, before any stage has a reason to use it.

It may feel backwards to build error reporting before there is anything to
report. The reason is that every later stage will report errors, and if each
one invents its own way of doing it, you end up with five formats and five sets
of position bugs. Building it once, first, means the lexer's very first error
already looks right.

On the [overview page](index.md#the-shape-of-the-whole-thing) this stage sits
at the foot of the mountain, under the words "Source text". The
[architecture page](../architecture.md#pass-contracts) calls the part that
holds the file a **source manager**, and gives it one rule: it provides "stable
source storage and locations" and must not interpret language syntax. The
[roadmap](../../roadmap.md#milestone-1-source-files-and-diagnostics) calls this
stage Milestone 1.

## What this stage is for

This stage has three jobs. It reads a source file and keeps its text available,
unchanged, for as long as the compilation runs. It lets any part of the
compiler describe a place in that text precisely, and turn that description
into a file name, a line number and a column number a person can find. And it
provides one way to report a problem: a single format, used by every stage,
that shows the offending line with a marker under the exact spot.

Nothing here understands Vortex. The source manager does not know what a
keyword is, and the diagnostic format does not know what a type is. That
ignorance is deliberate. It keeps this stage small and lets every later stage
rely on it.

## Words for this stage

source file
: The file of Vortex text the user hands to the compiler. The
  [conformance chapter](../../specification/conformance.md#12-programs) defines
  it as "a sequence of bytes provided to the compiler", which must be valid
  UTF-8.

character
: One symbol of text as a person would count it: a letter, a digit, a space, a
  `λ`. When Vortex counts columns, a character is one Unicode scalar value (one
  numbered Unicode character), so a letter written as a base letter plus a
  separate accent mark counts as two.

byte
: The unit a file is actually stored in. A byte can hold one of 256 values.
  Some characters fit in one byte; others need several.

encoding
: The rule that says which bytes stand for which characters. **UTF-8** is the
  most common encoding. In UTF-8, the ASCII characters (plain English letters,
  digits and punctuation) take one byte each, and other characters take two to
  four.

offset
: A position in the file, given as the number of bytes before it: "28 bytes
  in". Simple for a program to store and compare, useless for a person to find.

line and column
: A position a person can find. The line is which row of the file; the column
  is how far along that row. Both are counted from 1.

source span
: A stretch of source text, given as a start offset and a length, both in
  bytes. The [glossary](../../specification/glossary.md) uses exactly this
  definition. Every token, every tree node and every error message carries one.

diagnostic
: A message from the compiler about a problem in the source. The
  specification requires only errors. A compiler may add warnings, which point
  out likely mistakes but never reject a program
  ([Diagnostics 10.1](../../specification/diagnostics.md#101-required-diagnostic-data)).

category
: The kind of problem a diagnostic reports, such as a lexical error or a type
  error. The specification lists eight.

primary span
: The one span a diagnostic is about: the place the marker points at.

note
: An extra line attached to a diagnostic, often pointing at a second, related
  span, such as the declaration that explains why the primary span is wrong.

marker
: The row of symbols printed under a source line to show exactly which
  characters the diagnostic means. Clang calls a single `^` the caret.

## Reading the file

The roadmap's first item for this stage is "Read a Vortex source file". The
interesting part is what happens after the file is read. The text has to stay
where it is, in full and unchanged, until the compiler finishes, because every
error message may need to quote any line of it. The
[lexical structure chapter](../../specification/lexical-structure.md#28-token-source-data)
makes the same point from the other side: if tokens refer back into the
original text, "that buffer must remain alive for the complete lifetime of the
tokens".

The file might not be readable at all. The path might be wrong, or the file
might be a folder. That is a problem with the command, not with the Vortex
program, so it has no line or column. It still needs a clear message on
standard error and exit status 2, the status the
[command-line decision](../../decisions/program.md#d20) gives to problems
outside the source program. Decide how the message looks, and keep it visibly
different from a diagnostic about the source.

An empty file is not an error at this stage. The
[conformance chapter](../../specification/conformance.md#12-programs) says an
empty source file is grammatically valid, though it fails the rule that an
executable needs a `main` function. That failure is reported much later, by
[stage 4](stage-4-names-and-scopes.md).

## Characters, bytes and encoding

A file on disk is a row of bytes, not a row of characters. For plain English
text the two are the same length, which makes it easy to forget they are
different things. Vortex will not let you forget for long, because the
specification allows non-ASCII characters inside comments and inside character
and string literals. Its own example is `'λ'`.

<figure class="vx-figure">
<svg viewBox="0 0 760 270" role="img" aria-labelledby="bytes-title bytes-desc">
<title id="bytes-title">Characters and bytes in one line</title>
<desc id="bytes-desc">The line let s = 'λ'; is twelve characters long. Stored as UTF-8 it is thirteen bytes, because the Greek letter lambda takes two bytes, CE and BB. The semicolon is character 12 but byte 13.</desc>
<text class="vx-text" x="20" y="30">12 characters</text>
<rect class="vx-box" x="62" y="44" width="44" height="40"/>
<text class="vx-mono" x="84" y="69" text-anchor="middle">l</text>
<text class="vx-text-muted" x="84" y="100" text-anchor="middle">1</text>
<rect class="vx-box" x="110" y="44" width="44" height="40"/>
<text class="vx-mono" x="132" y="69" text-anchor="middle">e</text>
<text class="vx-text-muted" x="132" y="100" text-anchor="middle">2</text>
<rect class="vx-box" x="158" y="44" width="44" height="40"/>
<text class="vx-mono" x="180" y="69" text-anchor="middle">t</text>
<text class="vx-text-muted" x="180" y="100" text-anchor="middle">3</text>
<rect class="vx-box" x="206" y="44" width="44" height="40"/>
<text class="vx-text-muted" x="228" y="69" text-anchor="middle">·</text>
<text class="vx-text-muted" x="228" y="100" text-anchor="middle">4</text>
<rect class="vx-box" x="254" y="44" width="44" height="40"/>
<text class="vx-mono" x="276" y="69" text-anchor="middle">s</text>
<text class="vx-text-muted" x="276" y="100" text-anchor="middle">5</text>
<rect class="vx-box" x="302" y="44" width="44" height="40"/>
<text class="vx-text-muted" x="324" y="69" text-anchor="middle">·</text>
<text class="vx-text-muted" x="324" y="100" text-anchor="middle">6</text>
<rect class="vx-box" x="350" y="44" width="44" height="40"/>
<text class="vx-mono" x="372" y="69" text-anchor="middle">=</text>
<text class="vx-text-muted" x="372" y="100" text-anchor="middle">7</text>
<rect class="vx-box" x="398" y="44" width="44" height="40"/>
<text class="vx-text-muted" x="420" y="69" text-anchor="middle">·</text>
<text class="vx-text-muted" x="420" y="100" text-anchor="middle">8</text>
<rect class="vx-box" x="446" y="44" width="44" height="40"/>
<text class="vx-mono" x="468" y="69" text-anchor="middle">&#x27;</text>
<text class="vx-text-muted" x="468" y="100" text-anchor="middle">9</text>
<rect class="vx-box-accent" x="494" y="44" width="44" height="40"/>
<text class="vx-mono" x="516" y="69" text-anchor="middle">λ</text>
<text class="vx-text-muted" x="516" y="100" text-anchor="middle">10</text>
<rect class="vx-box" x="542" y="44" width="44" height="40"/>
<text class="vx-mono" x="564" y="69" text-anchor="middle">&#x27;</text>
<text class="vx-text-muted" x="564" y="100" text-anchor="middle">11</text>
<rect class="vx-box" x="590" y="44" width="44" height="40"/>
<text class="vx-mono" x="612" y="69" text-anchor="middle">;</text>
<text class="vx-text-muted" x="612" y="100" text-anchor="middle">12</text>
<rect class="vx-box" x="62" y="160" width="40" height="40"/>
<text class="vx-mono" x="82" y="185" text-anchor="middle">6C</text>
<text class="vx-text-muted" x="82" y="216" text-anchor="middle">1</text>
<line class="vx-line" x1="84" y1="108" x2="82" y2="156"/>
<rect class="vx-box" x="106" y="160" width="40" height="40"/>
<text class="vx-mono" x="126" y="185" text-anchor="middle">65</text>
<text class="vx-text-muted" x="126" y="216" text-anchor="middle">2</text>
<line class="vx-line" x1="132" y1="108" x2="126" y2="156"/>
<rect class="vx-box" x="150" y="160" width="40" height="40"/>
<text class="vx-mono" x="170" y="185" text-anchor="middle">74</text>
<text class="vx-text-muted" x="170" y="216" text-anchor="middle">3</text>
<line class="vx-line" x1="180" y1="108" x2="170" y2="156"/>
<rect class="vx-box" x="194" y="160" width="40" height="40"/>
<text class="vx-mono" x="214" y="185" text-anchor="middle">20</text>
<text class="vx-text-muted" x="214" y="216" text-anchor="middle">4</text>
<line class="vx-line" x1="228" y1="108" x2="214" y2="156"/>
<rect class="vx-box" x="238" y="160" width="40" height="40"/>
<text class="vx-mono" x="258" y="185" text-anchor="middle">73</text>
<text class="vx-text-muted" x="258" y="216" text-anchor="middle">5</text>
<line class="vx-line" x1="276" y1="108" x2="258" y2="156"/>
<rect class="vx-box" x="282" y="160" width="40" height="40"/>
<text class="vx-mono" x="302" y="185" text-anchor="middle">20</text>
<text class="vx-text-muted" x="302" y="216" text-anchor="middle">6</text>
<line class="vx-line" x1="324" y1="108" x2="302" y2="156"/>
<rect class="vx-box" x="326" y="160" width="40" height="40"/>
<text class="vx-mono" x="346" y="185" text-anchor="middle">3D</text>
<text class="vx-text-muted" x="346" y="216" text-anchor="middle">7</text>
<line class="vx-line" x1="372" y1="108" x2="346" y2="156"/>
<rect class="vx-box" x="370" y="160" width="40" height="40"/>
<text class="vx-mono" x="390" y="185" text-anchor="middle">20</text>
<text class="vx-text-muted" x="390" y="216" text-anchor="middle">8</text>
<line class="vx-line" x1="420" y1="108" x2="390" y2="156"/>
<rect class="vx-box" x="414" y="160" width="40" height="40"/>
<text class="vx-mono" x="434" y="185" text-anchor="middle">27</text>
<text class="vx-text-muted" x="434" y="216" text-anchor="middle">9</text>
<line class="vx-line" x1="468" y1="108" x2="434" y2="156"/>
<rect class="vx-box-accent" x="458" y="160" width="40" height="40"/>
<text class="vx-mono" x="478" y="185" text-anchor="middle">CE</text>
<text class="vx-text-muted" x="478" y="216" text-anchor="middle">10</text>
<line class="vx-line" x1="516" y1="108" x2="478" y2="156"/>
<rect class="vx-box-accent" x="502" y="160" width="40" height="40"/>
<text class="vx-mono" x="522" y="185" text-anchor="middle">BB</text>
<text class="vx-text-muted" x="522" y="216" text-anchor="middle">11</text>
<line class="vx-line" x1="516" y1="108" x2="522" y2="156"/>
<rect class="vx-box" x="546" y="160" width="40" height="40"/>
<text class="vx-mono" x="566" y="185" text-anchor="middle">27</text>
<text class="vx-text-muted" x="566" y="216" text-anchor="middle">12</text>
<line class="vx-line" x1="564" y1="108" x2="566" y2="156"/>
<rect class="vx-box" x="590" y="160" width="40" height="40"/>
<text class="vx-mono" x="610" y="185" text-anchor="middle">3B</text>
<text class="vx-text-muted" x="610" y="216" text-anchor="middle">13</text>
<line class="vx-line" x1="612" y1="108" x2="610" y2="156"/>
<text class="vx-text" x="20" y="244">13 bytes, if the file is stored as UTF-8</text>
<text class="vx-text-muted" x="20" y="264">The semicolon is at column 12 counted in characters, and at column 13 counted in bytes.</text>
</svg>
<figcaption>Figure 1. The same line counted two ways. The highlighted character, the Greek letter lambda, is one character but two bytes in UTF-8, so every column after it differs by one depending on what you count.</figcaption>
</figure>

Which encoding the file itself is in used to be an open question. The
[lexical structure chapter](../../specification/lexical-structure.md#source-encoding)
now settles this ([decision 15](../../decisions/lexical.md#d15)): a source file
must be UTF-8, and bytes that are not valid UTF-8 are a lexical error at their
position. A few more rules come with it:

- a byte order mark (an invisible marker some editors put at the start of a
  file) is ignored when it is the first character of the file, and takes no
  column; anywhere else it is an error;
- control characters other than tab, line feed and carriage return are errors,
  even inside a comment or a literal;
- a carriage return must be followed by a line feed.

This stage, where bytes become characters, is the natural place to find
invalid UTF-8, but the diagnostic's category is still lexical error. The other
rules are checked by the lexer in [stage 2](stage-2-lexer.md).

Columns count characters, so Figure 1's semicolon is reported at column 12,
while spans keep byte offsets
([Conformance 1.7](../../specification/conformance.md#17-source-locations),
[decision 16](../../decisions/diagnostics.md#d16)).

For most test files these rules change nothing, because most test files are
plain ASCII. That is exactly why they need tests of their own: one file with
bytes that are not valid UTF-8, and one that starts with a byte order mark.

## Positions and spans

Inside the compiler, the simplest way to name a place is an offset: how many
bytes come before it. A span is then an offset and a length. People need
something else. They need a file name, a line, and a column. The compiler has
to be able to turn one into the other whenever a message is printed.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-labelledby="span-title span-desc">
<title id="span-title">A source span inside one line</title>
<desc id="span-desc">Line 2 of a small file is drawn as thirty character cells. The cells from column 17 to column 30, holding an unterminated string, are outlined. The span starts at offset 28, which is line 2, column 17, and is 14 bytes long.</desc>
<text class="vx-text-muted" x="10" y="46">line 1</text>
<text class="vx-mono" x="56" y="46">fn main() {</text>
<text class="vx-text-muted" x="10" y="106">line 2</text>
<text class="vx-text-muted" x="61" y="76" text-anchor="middle">1</text>
<text class="vx-text-muted" x="149" y="76" text-anchor="middle">5</text>
<text class="vx-text-muted" x="259" y="76" text-anchor="middle">10</text>
<text class="vx-text-muted" x="413" y="76" text-anchor="middle">17</text>
<text class="vx-text-muted" x="589" y="76" text-anchor="middle">25</text>
<text class="vx-text-muted" x="699" y="76" text-anchor="middle">30</text>
<rect class="vx-box" x="50" y="84" width="660" height="32"/>
<rect class="vx-box-accent vx-pulse" x="402" y="80" width="308" height="40"/>
<text class="vx-text-muted" x="61" y="105" text-anchor="middle">·</text>
<text class="vx-text-muted" x="83" y="105" text-anchor="middle">·</text>
<text class="vx-text-muted" x="105" y="105" text-anchor="middle">·</text>
<text class="vx-text-muted" x="127" y="105" text-anchor="middle">·</text>
<text class="vx-mono" x="149" y="105" text-anchor="middle">l</text>
<text class="vx-mono" x="171" y="105" text-anchor="middle">e</text>
<text class="vx-mono" x="193" y="105" text-anchor="middle">t</text>
<text class="vx-text-muted" x="215" y="105" text-anchor="middle">·</text>
<text class="vx-mono" x="237" y="105" text-anchor="middle">v</text>
<text class="vx-mono" x="259" y="105" text-anchor="middle">a</text>
<text class="vx-mono" x="281" y="105" text-anchor="middle">l</text>
<text class="vx-mono" x="303" y="105" text-anchor="middle">u</text>
<text class="vx-mono" x="325" y="105" text-anchor="middle">e</text>
<text class="vx-text-muted" x="347" y="105" text-anchor="middle">·</text>
<text class="vx-mono" x="369" y="105" text-anchor="middle">=</text>
<text class="vx-text-muted" x="391" y="105" text-anchor="middle">·</text>
<text class="vx-mono" x="413" y="105" text-anchor="middle">&quot;</text>
<text class="vx-mono" x="435" y="105" text-anchor="middle">u</text>
<text class="vx-mono" x="457" y="105" text-anchor="middle">n</text>
<text class="vx-mono" x="479" y="105" text-anchor="middle">t</text>
<text class="vx-mono" x="501" y="105" text-anchor="middle">e</text>
<text class="vx-mono" x="523" y="105" text-anchor="middle">r</text>
<text class="vx-mono" x="545" y="105" text-anchor="middle">m</text>
<text class="vx-mono" x="567" y="105" text-anchor="middle">i</text>
<text class="vx-mono" x="589" y="105" text-anchor="middle">n</text>
<text class="vx-mono" x="611" y="105" text-anchor="middle">a</text>
<text class="vx-mono" x="633" y="105" text-anchor="middle">t</text>
<text class="vx-mono" x="655" y="105" text-anchor="middle">e</text>
<text class="vx-mono" x="677" y="105" text-anchor="middle">d</text>
<text class="vx-mono" x="699" y="105" text-anchor="middle">;</text>
<line class="vx-line" x1="402" y1="128" x2="402" y2="150"/>
<line class="vx-line" x1="710" y1="128" x2="710" y2="150"/>
<line class="vx-line" x1="402" y1="140" x2="710" y2="140"/>
<text class="vx-text-accent" x="556" y="170" text-anchor="middle">the span</text>
<text class="vx-text-muted" x="556" y="188" text-anchor="middle">starts at offset 28, length 14</text>
<text class="vx-text-muted" x="10" y="216">line 3</text>
<text class="vx-mono" x="56" y="216">}</text>
<text class="vx-text-muted" x="10" y="258">Offset 28 means 28 bytes come before the opening quote in the whole file:</text>
<text class="vx-text-muted" x="10" y="278">12 on line 1 (11 plus a one-byte line break) and 16 on line 2. As a line and column, that is 2:17.</text>
</svg>
<figcaption>Figure 2. A span inside one line of a three-line file. The outlined cells are the unterminated string the specification uses as its example of a lexical error. The compiler can store the span as an offset and a length, and turn it into "line 2, column 17" only when it prints a message.</figcaption>
</figure>

The example in Figure 2 is taken from the
[diagnostics chapter](../../specification/diagnostics.md#104-examples), which
requires a "lexical error at the unterminated string". Notice where the span
ends. A string in Vortex cannot contain a line break, so an unterminated
string stops at the end of its line, and the span stops there too.

Several small questions hide inside "line and column".
[Conformance 1.7](../../specification/conformance.md#17-source-locations) now
answers each one, and each answer still needs a test.

**Where counting starts.** Lines and columns count from 1, as most people
expect of the first line of a file. Offsets are for the compiler, not for
people: the first byte of the file has 0 bytes before it, so its offset is 0.

**What a line break is.** A line feed, or a carriage return followed by a line
feed. Files written on Windows usually end each line with the pair; files
written elsewhere use a line feed alone. The pair counts as one break, so no
column comes out one too high, and a carriage return on its own is a lexical
error. Offsets still count both bytes of the pair.

**What a tab is worth.** One column. An editor shows a tab several columns
wide, so a marker printed under the line must copy the tab, or widen it the
same way the quoted line is shown, to land under the right character; that is
presentation, not position.

**Where the end of the file is.** A position like any other: its offset is the
file's length in bytes, even when the last line has no line break. Some errors
happen there, such as a string still open when the file runs out.

The [conformance chapter](../../specification/conformance.md#17-source-locations)
adds one more rule that later stages will lean on. The span of a larger piece
of source covers all of it: the location of `left + right` covers both operands
and the operator, while each part keeps its own narrower span. At this stage
that only means a span must be able to cover any stretch of text, including
several lines.

## One format for every diagnostic

The roadmap asks for "one consistent diagnostic format for errors". The
[diagnostics chapter](../../specification/diagnostics.md#101-required-diagnostic-data)
says what each diagnostic should contain: a category, a concise primary
message, a primary span, the unexpected or invalid construct, the expected form
or violated rule when that helps, and optional notes pointing at related
places. It also says diagnostics "must not rely only on an internal enum name
or raw token number", which rules out the laziest possible message.

<figure class="vx-figure">
<svg viewBox="0 0 760 260" role="img" aria-labelledby="diag-title diag-desc">
<title id="diag-title">The parts of one diagnostic</title>
<desc id="diag-desc">An example error message for assigning to an immutable variable, with each part labelled: the category, the primary message, the file, line and column, the source line as written, a marker under the primary span with a short label, and a related span at the declaration shown as a note.</desc>
<text class="vx-mono" style="font-variant-ligatures: none" x="24.0" y="60">semantic error: cannot assign to immutable variable `value`</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="39.6" y="84">--&gt; scale.vx:3:5</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="47.4" y="108">|</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="31.8" y="132">3 |</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="94.2" y="132">value = 20;</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="47.4" y="156">|</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="94.2" y="156">^^^^^ assigned here</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="47.4" y="180">|</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="31.8" y="204">2 |</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="94.2" y="204">let value = 10;</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="47.4" y="228">|</text>
<text class="vx-mono" style="font-variant-ligatures: none" x="125.4" y="228">----- declared here without mut</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<line class="vx-line" x1="502" y1="56" x2="512" y2="56"/>
<text class="vx-text-accent" x="520" y="60">category and primary message</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<line class="vx-line" x1="182" y1="80" x2="512" y2="80"/>
<text class="vx-text-accent" x="520" y="84">file, line and column</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<line class="vx-line" x1="198" y1="128" x2="512" y2="128"/>
<text class="vx-text-accent" x="520" y="132">the source line, as written</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<line class="vx-line" x1="260" y1="152" x2="512" y2="152"/>
<text class="vx-text-accent" x="520" y="156">marker on the primary span</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<line class="vx-line" x1="385" y1="212" x2="512" y2="212"/>
<text class="vx-text-accent" x="520" y="216">a note at a related span</text>
</g>
<text class="vx-text-muted" x="24" y="30">An illustration only: Vortex has not fixed its diagnostic wording or layout.</text>
</svg>
<figcaption>Figure 3. One possible layout for a diagnostic, with its parts labelled. The program assigns to <code>value</code>, which was declared without <code>mut</code>. The specification requires a semantic error at the assignment, with an optional note at the declaration. The labels light up in reading order.</figcaption>
</figure>

The eight categories are listed in the
[diagnostics chapter](../../specification/diagnostics.md#102-categories):
lexical, syntax, name, type, semantic, constant-evaluation, runtime, and
implementation-limit. The last one deserves a word. While the compiler is
unfinished, it will meet source it recognizes but cannot yet handle. The
[conformance chapter](../../specification/conformance.md#13-implementation-conformance)
says it should then report an explicit "not implemented" diagnostic, and that
crashing or silently skipping the construct is not acceptable. Having the
category from the start makes that honest answer the easy one. Later the
category gains a second use: a running program that exceeds a documented limit
of the implementation, such as its stack size, stops with an
implementation-limit report ([stage 9](stage-9-runtime-safety.md),
[record 46](../../decisions/diagnostics.md#d46)).

The specification also asks that a diagnostic describe the language rule that
was broken, "rather than forcing users to understand the compiler pass that
happened to detect it". A user who writes `let value: bool = 10;` should read
about a `bool` and an integer, not about the internals of a type checker.

The exact wording and layout are left open. The specification calls exact
prose an implementation detail. So the decisions for your compiler are these:
the order of the parts, how a location is written, how the category is shown,
a house style for message text, and how notes and related spans appear.
Figure 3 shows one answer. It borrows its layout from Rust's compiler, whose
developer guide describes the same parts under the names level, message,
primary span, secondary span and sub-diagnostic.[^rustc-diag]

Where diagnostics go is not open: the `vortex` command writes them to standard
error, keeps standard output for the `--tokens` and `--ast` printouts, and
exits with status 1 when the source has any error
([decision](../../decisions/program.md#d20)). A suggested layout is recorded
as an implementation choice ([I2](../../decisions/implementation.md#i2)). It
is closer to Clang's than to Figure 3: a header line
`<file>:<line>:<column>: error[<category>]: <message>`, then the source line as
written, with a row of `^` under the primary span.

## Showing the source line

The roadmap's last item for this stage is "Show the relevant source line and
point to the problem". This is the part of a diagnostic people actually read.
A line number alone makes the reader stop, open the file, count, and find the
spot. The quoted line with a marker under it lets them see the problem at once.

Evan Czaplicki, writing about Elm's error messages, made the case plainly: the
message should show the code exactly as the programmer wrote it, with line
numbers, so the reader can compare it with their editor without effort.[^elm]
That is a small requirement with a large effect, and it is exactly what the
roadmap asks for.

A few cases need a decision:

- a span that covers several lines (a whole block, later on): show the first
  line only, or every line, or the first and last;
- a span of zero length, such as the point where a missing semicolon should
  have been: the marker still needs one visible symbol;
- a line with tabs before the span: the marker has to line up under the right
  character;
- a very long line: whether to show it whole or trim it around the span.

The suggested default in [I2](../../decisions/implementation.md#i2) answers
the first three: mark a span over several lines on its first line only, make
every marker at least one `^` wide, and copy the tabs before the span into the
marker line.

## Testing it before there is a lexer

The roadmap says this stage is complete "when the compiler can report a
readable error at an exact location in a source file". At this point nothing
in the compiler can find a real error yet. That is fine. The diagnostic part can
be tested on its own, by asking it to report a made-up problem at a chosen
position in a chosen file and checking the printed result character by
character.

Good test files for this stage are chosen to break position logic: a file with
a non-ASCII character before the target column, a file whose lines end with
both line-break styles, a file with a tab before the target, a target on the
last line with no final line break, and an empty file. Each should produce the
position you predicted by hand, written into the expected output before you
run anything.

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- Reading a file given on the command line, with a clear message and exit
  status 2 when it cannot be read.
- The whole source text kept unchanged until the compilation ends: every
  diagnostic may quote it.
- A way to name any position and any span, including the end of the file.
- Conversion from a position to file name, line and column.
- UTF-8 decoding as the
  [lexical structure chapter](../../specification/lexical-structure.md#source-encoding)
  requires: invalid UTF-8 reported as a lexical error, and a leading byte order
  mark skipped.
- Positions exactly as Conformance 1.7 defines them: byte offsets, lines and
  columns from 1, columns counted in characters, CR LF as one line break, a
  tab as one column.
- One diagnostic format with every part the
  [specification](../../specification/diagnostics.md#101-required-diagnostic-data)
  requires, used by every stage.
- All eight categories available, including implementation-limit.
- The source line printed with a marker under the primary span.
- Room for notes that point at a second span.
- Tests that check printed positions against hand-worked answers.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Recovering after an error and reporting the next one: that is parser work
  in [stage 3](stage-3-parser-and-tree.md).
- Colored output: pleasant, but it changes nothing a test can check.
- Suggested fixes: they need a compiler that understands the program.
- Error codes with long explanations: useful once there are many messages.
- Machine-readable output for editors: nothing in v0.1 asks for it.
- Warnings: the specification allows them but never requires them, and they
  never change whether a program compiles
  ([record 48](../../decisions/diagnostics.md#d48)).
- Several source files or modules: modules come after v0.1.
- Reports from a running program: runtime errors arrive in
  [stage 9](stage-9-runtime-safety.md).

</div>
</div>

## What you do not need yet

Everything in the right-hand column is about richer messages or more of them.
This stage is about one message, correct in every detail. A plain diagnostic
with the right position beats a colorful one that points one column to the
left.

## How you know it is finished

The roadmap's condition is that "the compiler can report a readable error at an
exact location in a source file". Read "exact" strictly. You are finished
when:

- the compiler reads a file given on the command line, and gives a clear
  message and exit status 2 when it cannot;
- a diagnostic at a chosen position prints the right file name, line and column
  for ASCII text, non-ASCII text, tabs, both line-break styles, and the last
  line of a file with no final line break;
- the printed source line is the line as written, and the marker sits under
  exactly the characters of the span;
- tests show positions following Conformance 1.7 in each of those cases;
- a file with invalid UTF-8 gets a lexical error where the bad bytes are, and
  a leading byte order mark shifts no column;
- all of this runs as part of the one test command from
  [stage 0](stage-0-workbench.md).

## Traps

**Counting bytes and calling them columns.** A compiler that counts bytes
reports a column one or more too high after any non-ASCII character, and the
marker lands on the wrong character. Vortex columns count characters: keep
offsets in bytes, and count characters only when you turn an offset into a
column.

**Two-character line breaks.** A file saved on Windows can make every position
at the end of a line off by one. The bug only shows up for people who use
those files, which is why it needs its own test.

**Losing the source text.** If the text is thrown away after the lexer runs, a
type error found much later cannot quote its line. Keep it for the whole
compilation.

**Message text scattered across the compiler.** The
[architecture page](../architecture.md#diagnostics) asks that user-facing
wording not be written into tree-building or token-handling code. Keep the
format in one place, so improving it later is one change.

**Describing the compiler instead of the program.** "Unexpected token kind 17"
tells the user about your enum. "Expected `:` before the type" tells them
about their code. The specification asks for the second kind.

**Crashing instead of reporting.** When something unexpected happens, the
tempting shortcut is to stop the program. The conformance chapter treats a
crash as non-conforming. Report an implementation-limit error instead.

## How others teach this stage

**Clang, "Expressive Diagnostics".** The Clang project's page on its
diagnostics is a short tour of what good error output looks like. It explains
why every diagnostic carries full column information and a caret under the
problem, why the related source range is highlighted, and why the wording
should say only what the caret does not already show.[^clang-diag] Its example
layout, a `file:line:column:` prefix followed by the source line and a caret
line, is close to what Figure 3 shows. Its fix-it hints are the "suggested
fixes" this page tells you to leave for later.

**Elm, "Compiler Errors for Humans".** Evan Czaplicki's post is about the
reader of the message. It argues that a bare location like `program.x:43:22`
makes people do translation work, and that the fix is to show the code exactly
as written, with line numbers, and to give a hint about what went
wrong.[^elm] It also describes a catalog of programs that trigger error
messages, used to find and improve bad ones, which is the same idea as the
invalid-program folder from [stage 0](stage-0-workbench.md).

**The Rust compiler development guide, "Errors and lints".** This chapter
shows the anatomy of a Rust diagnostic, part by part, and gives a style guide
for message text: plain simple English, lowercase, no final full stop, and
code names in backticks.[^rustc-diag] It also advises making the primary span
as small as possible while still showing the problem. The specification adopts
none of these rules; the suggested default in
[I2](../../decisions/implementation.md#i2) follows its message style, and the
guide is a good model of what a written-down house style looks like.

**Crafting Interpreters, "Scanning".** Nystrom's scanner stores only a line
number on each token and says that more careful implementations also keep the
column and length. He points out that the column only has to be worked out
when a message is actually shown, because most tokens never appear in
one.[^ci-scan] Vortex asks for more than his first scanner does, since the
roadmap requires columns, but the observation about when to compute them is a
useful one.

[^clang-diag]: Clang Project, "Expressive Diagnostics". <https://clang.llvm.org/diagnostics.html>
[^elm]: Evan Czaplicki, "Compiler Errors for Humans", 30 June 2015. <https://elm-lang.org/news/compiler-errors-for-humans>
[^rustc-diag]: Rust Compiler Development Guide, "Errors and lints". <https://rustc-dev-guide.rust-lang.org/diagnostics.html>
[^ci-scan]: Robert Nystrom, *Crafting Interpreters*, chapter "Scanning". <https://craftinginterpreters.com/scanning.html>
