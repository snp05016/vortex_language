# 2. The lexer

<p class="page-intro">The lexer reads the characters of a Vortex file and groups them into tokens: the words, numbers and symbols that the rest of the compiler works with.</p>

When you read the line `let total = 0b101;` you do not see eighteen separate
characters. You see a keyword, a name, an equals sign, a number and a
semicolon. The **lexer** is the part of the compiler that does this grouping.
It is also called a **scanner**, and the grouping itself is often called
**tokenizing**.

This is the first stage that knows anything about Vortex. It uses the source
text and positions from [stage 1](stage-1-source-and-diagnostics.md), and it
hands its result to the parser in [stage 3](stage-3-parser-and-tree.md). On the
[overview page](index.md#the-shape-of-the-whole-thing) it is the first step up
the mountain. The rules it follows are all in one short chapter of the
specification, [Lexical structure](../../specification/lexical-structure.md),
and the matching tour chapter is
[Basic source rules](../../language-tour/02-basic-source-rules.md). Read both
before starting. They are short.

The [roadmap](../../roadmap.md#milestone-2-lexer) calls this Milestone 2.

## What this stage is for

The lexer takes the whole source text and produces a sequence of tokens, ending
with one end-of-file token. Each token says what kind of thing it is, exactly
how it was spelled, and where it came from. Spaces, line breaks and comments
separate tokens and then disappear. Characters that cannot form any valid token
produce a lexical error at their position.

The [architecture page](../architecture.md#pass-contracts) sets the contract in
one table row. The lexer takes "source characters" and produces "tokens with
spelling and spans". The one thing it must not do is "resolve names or types".
The lexer knows that `total` is an identifier. It does not know, and must not
try to find out, whether `total` was ever declared.

## Words for this stage

token
: One unit of the language, like a word in a sentence. The
  [glossary](../../specification/glossary.md) defines it as "one lexical unit
  with a kind, source spelling, and source span".

token kind
: Which sort of token it is: a keyword, an identifier, an integer literal, a
  particular operator, and so on.

spelling
: The exact characters the token was made from, as they appear in the file.
  Many books call this the **lexeme**.

token stream
: The whole sequence of tokens for one file, in order, ending with the
  end-of-file token.

keyword
: A word the language reserves for itself, such as `let`, `fn` or `while`. A
  keyword cannot be used as a name.

reserved word
: A word kept back for a future version of the language, such as `i64` or
  `const`. It has no meaning yet and cannot be used as a name.

identifier
: A name the programmer chose, such as `total` or `item_count`.

literal
: A value written directly in the source: `42`, `3.14`, `true`, `'A'`,
  `"Vortex"`.

escape sequence
: A backslash followed by a character, used inside a character or string
  literal to write something that is hard to type directly. `\n` means a line
  feed.

whitespace
: Spaces, tabs and line breaks. In Vortex they separate tokens and carry no
  other meaning.

comment
: Text for human readers that the compiler skips. In v0.1 a comment starts
  with `//` and runs to the end of the line.

longest match
: The rule that when several tokens could start at the same place, the lexer
  takes the longest one that is valid. It is also called **maximal munch**.

end-of-file token
: A token with no spelling that marks the end of the input, so later stages
  have a clear place to stop.

lexical error
: A diagnostic saying that some characters cannot form any valid token.

## From characters to tokens

Figure 1 shows the lexer at work on one line. The characters are read from left
to right. Each group that forms a token becomes one token, carrying its kind,
its spelling, and the columns it came from. Spaces separate the groups and
leave nothing behind.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-labelledby="tok-title tok-desc">
<title id="tok-title">Characters grouping into tokens</title>
<desc id="tok-desc">The eighteen characters of let total = 0b101; sit in a row with their column numbers. Below them, six tokens light up one after another: the keyword let at columns 1 to 3, the identifier total at columns 5 to 9, the operator = at column 11, the integer literal 0b101 at columns 13 to 17 with value 5, the punctuation ; at column 18, and an end-of-file token. The spaces become no token.</desc>
<text class="vx-text-muted" x="20" y="24">column</text>
<text class="vx-text-muted" x="74" y="44" text-anchor="middle">1</text>
<rect class="vx-box" x="58" y="52" width="32" height="36"/>
<text class="vx-mono" x="74" y="75" text-anchor="middle">l</text>
<text class="vx-text-muted" x="110" y="44" text-anchor="middle">2</text>
<rect class="vx-box" x="94" y="52" width="32" height="36"/>
<text class="vx-mono" x="110" y="75" text-anchor="middle">e</text>
<text class="vx-text-muted" x="146" y="44" text-anchor="middle">3</text>
<rect class="vx-box" x="130" y="52" width="32" height="36"/>
<text class="vx-mono" x="146" y="75" text-anchor="middle">t</text>
<text class="vx-text-muted" x="182" y="44" text-anchor="middle">4</text>
<rect class="vx-box" x="166" y="52" width="32" height="36"/>
<text class="vx-text-muted" x="182" y="75" text-anchor="middle">·</text>
<text class="vx-text-muted" x="218" y="44" text-anchor="middle">5</text>
<rect class="vx-box" x="202" y="52" width="32" height="36"/>
<text class="vx-mono" x="218" y="75" text-anchor="middle">t</text>
<text class="vx-text-muted" x="254" y="44" text-anchor="middle">6</text>
<rect class="vx-box" x="238" y="52" width="32" height="36"/>
<text class="vx-mono" x="254" y="75" text-anchor="middle">o</text>
<text class="vx-text-muted" x="290" y="44" text-anchor="middle">7</text>
<rect class="vx-box" x="274" y="52" width="32" height="36"/>
<text class="vx-mono" x="290" y="75" text-anchor="middle">t</text>
<text class="vx-text-muted" x="326" y="44" text-anchor="middle">8</text>
<rect class="vx-box" x="310" y="52" width="32" height="36"/>
<text class="vx-mono" x="326" y="75" text-anchor="middle">a</text>
<text class="vx-text-muted" x="362" y="44" text-anchor="middle">9</text>
<rect class="vx-box" x="346" y="52" width="32" height="36"/>
<text class="vx-mono" x="362" y="75" text-anchor="middle">l</text>
<text class="vx-text-muted" x="398" y="44" text-anchor="middle">10</text>
<rect class="vx-box" x="382" y="52" width="32" height="36"/>
<text class="vx-text-muted" x="398" y="75" text-anchor="middle">·</text>
<text class="vx-text-muted" x="434" y="44" text-anchor="middle">11</text>
<rect class="vx-box" x="418" y="52" width="32" height="36"/>
<text class="vx-mono" x="434" y="75" text-anchor="middle">=</text>
<text class="vx-text-muted" x="470" y="44" text-anchor="middle">12</text>
<rect class="vx-box" x="454" y="52" width="32" height="36"/>
<text class="vx-text-muted" x="470" y="75" text-anchor="middle">·</text>
<text class="vx-text-muted" x="506" y="44" text-anchor="middle">13</text>
<rect class="vx-box" x="490" y="52" width="32" height="36"/>
<text class="vx-mono" x="506" y="75" text-anchor="middle">0</text>
<text class="vx-text-muted" x="542" y="44" text-anchor="middle">14</text>
<rect class="vx-box" x="526" y="52" width="32" height="36"/>
<text class="vx-mono" x="542" y="75" text-anchor="middle">b</text>
<text class="vx-text-muted" x="578" y="44" text-anchor="middle">15</text>
<rect class="vx-box" x="562" y="52" width="32" height="36"/>
<text class="vx-mono" x="578" y="75" text-anchor="middle">1</text>
<text class="vx-text-muted" x="614" y="44" text-anchor="middle">16</text>
<rect class="vx-box" x="598" y="52" width="32" height="36"/>
<text class="vx-mono" x="614" y="75" text-anchor="middle">0</text>
<text class="vx-text-muted" x="650" y="44" text-anchor="middle">17</text>
<rect class="vx-box" x="634" y="52" width="32" height="36"/>
<text class="vx-mono" x="650" y="75" text-anchor="middle">1</text>
<text class="vx-text-muted" x="686" y="44" text-anchor="middle">18</text>
<rect class="vx-box" x="670" y="52" width="32" height="36"/>
<text class="vx-mono" x="686" y="75" text-anchor="middle">;</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<rect class="vx-box-accent" x="56" y="96" width="108" height="8"/>
<path class="vx-line" d="M110 104 L110 150 L73 170 L73 200"/>
<rect class="vx-box-strong" x="20" y="200" width="106" height="84"/>
<text class="vx-text-muted" x="73.0" y="222" text-anchor="middle">keyword</text>
<text class="vx-mono" x="73.0" y="246" text-anchor="middle">let</text>
<text class="vx-text-muted" x="73.0" y="270" text-anchor="middle">cols 1 to 3</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<rect class="vx-box-accent" x="200" y="96" width="180" height="8"/>
<path class="vx-line" d="M290 104 L290 150 L191 170 L191 200"/>
<rect class="vx-box-strong" x="138" y="200" width="106" height="84"/>
<text class="vx-text-muted" x="191.0" y="222" text-anchor="middle">identifier</text>
<text class="vx-mono" x="191.0" y="246" text-anchor="middle">total</text>
<text class="vx-text-muted" x="191.0" y="270" text-anchor="middle">cols 5 to 9</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<rect class="vx-box-accent" x="416" y="96" width="36" height="8"/>
<path class="vx-line" d="M434 104 L434 150 L309 170 L309 200"/>
<rect class="vx-box-strong" x="256" y="200" width="106" height="84"/>
<text class="vx-text-muted" x="309.0" y="222" text-anchor="middle">operator</text>
<text class="vx-mono" x="309.0" y="246" text-anchor="middle">=</text>
<text class="vx-text-muted" x="309.0" y="270" text-anchor="middle">col 11</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<rect class="vx-box-accent" x="488" y="96" width="180" height="8"/>
<path class="vx-line" d="M578 104 L578 150 L427 170 L427 200"/>
<rect class="vx-box-strong" x="374" y="200" width="106" height="84"/>
<text class="vx-text-muted" x="427.0" y="222" text-anchor="middle">integer literal</text>
<text class="vx-mono" x="427.0" y="246" text-anchor="middle">0b101</text>
<text class="vx-text-muted" x="427.0" y="270" text-anchor="middle">cols 13 to 17</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<rect class="vx-box-accent" x="668" y="96" width="36" height="8"/>
<path class="vx-line" d="M686 104 L686 150 L545 170 L545 200"/>
<rect class="vx-box-strong" x="492" y="200" width="106" height="84"/>
<text class="vx-text-muted" x="545.0" y="222" text-anchor="middle">punctuation</text>
<text class="vx-mono" x="545.0" y="246" text-anchor="middle">;</text>
<text class="vx-text-muted" x="545.0" y="270" text-anchor="middle">col 18</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<path class="vx-line" d="M714 70 L734 70 L734 170 L663 170 L663 200"/>
<rect class="vx-box" x="610" y="200" width="106" height="84"/>
<text class="vx-text-muted" x="663.0" y="222" text-anchor="middle">end of file</text>
<text class="vx-text-muted" x="663.0" y="246" text-anchor="middle">(no spelling)</text>
<text class="vx-text-muted" x="663.0" y="270" text-anchor="middle">after col 18</text>
</g>
<text class="vx-text-muted" x="20" y="316">The spaces (shown as dots) separate tokens and then disappear. The literal 0b101 has the value 5.</text>
</svg>
<figcaption>Figure 1. The lexer reading <code>let total = 0b101;</code>. Each token lights up in turn, joined to the characters it was made from. The last token is the end-of-file token, which has a position but no characters.</figcaption>
</figure>

Two details in that figure matter for everything later. The first is that the
integer token keeps the spelling `0b101` as well as the value 5. The
[lexical structure chapter](../../specification/lexical-structure.md#28-token-source-data)
says each token "preserves its kind, original spelling, and source span", and
that later stages "must not need to reconstruct spelling from the token kind".
An error message about this literal should be able to quote it as written.

The second is the span. Every token knows where it starts and how long it is,
both counted in bytes
([Conformance 1.7](../../specification/conformance.md#17-source-locations)).
When the parser later finds a problem with `total`, it will point at columns 5
to 9 using nothing but the token's span. If the lexer gets a span wrong by one,
every error message in the compiler inherits the mistake.

The roadmap's completion condition follows directly: a Vortex file "can be
printed as a correct token stream with accurate source locations". So the
driver has a `--tokens` option that prints the tokens it found to standard
output, one per line, with kind, spelling and position, and then stops
([decision](../../decisions/program.md#d20)). That printout is how you, and
your tests, check the lexer. Its exact format is your decision. Keep it plain
and stable, because many expected-output files will depend on it. The
suggested default, [implementation choice I4](../../decisions/implementation.md#i4),
prints one token per line as `<line>:<column> <KIND> '<spelling>'`, ending
with the end-of-file token.

## The kinds of token Vortex has

The specification's lexical chapter lists everything the lexer must recognize.
The table below is a summary. The chapter itself is the rule.

| Kind | Examples | Where the rule is |
| --- | --- | --- |
| Identifier | `value`, `item_count`, `_temporary`, `Matrix2` | [2.3](../../specification/lexical-structure.md#23-identifiers) |
| Keyword | `fn`, `let`, `mut`, `while`, `i32`, `String` | [2.4](../../specification/lexical-structure.md#24-keywords) |
| Integer literal | `0`, `42`, `0b101010` | [2.5](../../specification/lexical-structure.md#25-literals) |
| Floating-point literal | `3.14`, `1.0e-4`, `6.02E+23` | [2.5](../../specification/lexical-structure.md#25-literals) |
| Boolean literal | `true`, `false` | [2.5](../../specification/lexical-structure.md#25-literals) |
| Character literal | `'A'`, `'λ'`, `'\n'` | [2.5](../../specification/lexical-structure.md#25-literals) |
| String literal | `"Vortex"`, `"first line\nsecond line"` | [2.5](../../specification/lexical-structure.md#25-literals) |
| Operator | `+`, `==`, `&&`, `<<`, `+=`, `..`, `..=` | [2.6](../../specification/lexical-structure.md#26-operators-and-punctuation) |
| Punctuation | `(`, `{`, `[`, `;`, `.`, `:`, `,`, `->` | [2.6](../../specification/lexical-structure.md#26-operators-and-punctuation) |
| End of file | (no spelling) | [2.7](../../specification/lexical-structure.md#27-lexical-errors) |

### Keywords and identifiers

An identifier starts with an ASCII letter or an underscore, and continues with
letters, digits and underscores. That shape also fits every keyword. So the
lexer has to tell them apart: a word shaped like an identifier is a keyword if
its spelling is exactly one of the keywords, a lexical error if it is one of
the reserved words described below, and an identifier otherwise.

"Exactly" includes upper and lower case. The specification says "`String` is a
keyword, while `string` is an identifier". Many other languages spell their
string type in lower case, which makes this an easy one to get wrong.

| Spelling | Token kind | Why |
| --- | --- | --- |
| `String` | keyword | it is on the keyword list |
| `string` | identifier | case matters, and `string` is not on the list |
| `Let` | identifier | only `let` is a keyword |
| `print` | identifier | `print` is a built-in function, not a keyword |
| `main` | identifier | `main` is special to later stages, not to the lexer |
| `u64` | none: a lexical error | reserved for a future version |
| `true` | keyword or boolean literal | a keyword that denotes a `bool` value; either kind conforms |

The last row has two correct answers. The specification makes `true` and
`false` keywords that denote the two boolean values, and
[decision 18](../../decisions/lexical.md#d18) lets the lexer give them either
a keyword kind or a boolean-literal kind. Pick one for your token printout.
What matters is that they can never be identifiers and that the parser can
treat them as values.

The type names `i32`, `f32`, `usize` and the rest are keywords too. The lexer
does not need to know they are types. It only needs to recognize their
spelling.

A second, shorter list holds words reserved for a future version: `i8`, `i16`,
`i64`, `u8`, `u16`, `u64`, `f16`, `bf16` and `const`
([decision 29](../../decisions/lexical.md#d29)). They have no meaning in v0.1.
When a word's whole spelling is on this list, the lexer reports a lexical
error, "reserved for a future version", instead of producing an identifier.
`u64_count` is still an identifier.

### Numbers

An integer literal is either `0`, or a digit from 1 to 9 followed by more
digits, or `0b` followed by one or more binary digits. So `42` and `0b101010`
are both integer literals, and `0b101010` has the value 42. Hexadecimal, octal,
digit separators such as `1_000`, and type suffixes such as `42u32` are not
part of v0.1. A leading zero is not allowed: `010` is an error, not ten, and
not the octal eight that C would read. The prefix must be a lowercase `0b`
([decision 17](../../decisions/lexical.md#d17)).

A floating-point literal needs digits on both sides of the dot, and may end with
an exponent: `e` or `E`, an optional sign, and digits. So `3.14`, `1.0e-4` and
`6.02E+23` are valid. `1e5` and `5.` are not: an exponent needs a decimal point
before it, and a dot needs digits after it. `.5` is a different case: it is the
token `.` followed by `5`, and the parser rejects it.

The lexer reads a number as a run of characters before it checks it. The run
starts at a digit and takes every following letter, digit and `_`, every `.`
that is not followed by a second `.`, and a `+` or `-` right after `e` or `E`.
If the whole run is not exactly one literal, it is one malformed numeric
literal. So `2values`, `0xff`, `0b102` and `1.0e` each give one error covering
the whole run, not a number followed by leftovers.

A leading minus sign is never part of a number. In `-42` the lexer produces an
operator token `-` and then an integer literal `42`, and the parser later
builds a negation from them. The
[grammar](../../specification/grammar.md#literals) says so directly.

The lexer also does not check whether a number is too big. A literal like
`3000000000` is a perfectly good token. Whether it fits the type it ends up
with is a question for [stage 5](stage-5-types-and-rules.md), because the
lexer cannot know that type. That holds for any length: even a literal larger
than the largest 64-bit integer is one token, and stage 5 reports it as a type
error ([decision](../../decisions/numbers.md#d32)).

### Characters and strings

A character literal is a single quote, exactly one character or one escape
sequence, and a closing single quote. `'λ'` is valid: one character, even
though in UTF-8 it takes two bytes, as [stage 1](stage-1-source-and-diagnostics.md#characters-bytes-and-encoding)
explained. `'ab'` and `''` are lexical errors. Here one character means one
Unicode scalar value ([decision 15](../../decisions/lexical.md#d15)), one
decoded code point. An accented letter typed as a plain letter followed by a
separate combining accent is two scalar values, so it does not fit in a
character literal even though it looks like one letter.

A string literal is a double quote, any number of characters or escape
sequences, and a closing double quote. Neither kind of literal may contain a
line break, so a string that is not closed by the end of its line is
unterminated.

There are five escapes: `\n`, `\t`, `\\`, `\"` and `\'`. Anything else after a
backslash, such as `\q` or `\r`, is an unsupported escape and a lexical error.
The tour's examples show them in use:

```vortex
// statements: valid
print("first line\nsecond line");
let quote: char = '\'';
let path = "left\\right";
```

A token for a string keeps its spelling exactly as written, backslashes and
all. The value the program will actually use, with `\n` turned into a real
line feed, is called the decoded value. The
[grammar](../../specification/grammar.md#literals) asks the syntax tree to
preserve the "decoded value" of each literal, so it must be worked out
somewhere before stage 3 finishes. The lexer is the stage that checks the
escapes are valid.

### Whitespace and comments

Spaces, tabs and line breaks separate tokens and are otherwise ignored. A line
break does not end a statement; only `;` does. This is valid:

```vortex
// statements: valid
let width = 128;
let area =
    width * width;
```

A line break is a line feed, or a carriage return followed by a line feed; the
pair is one line break. A carriage return on its own is a lexical error
([Lexical structure 2.1](../../specification/lexical-structure.md#21-whitespace-and-line-boundaries),
[decision 16](../../decisions/diagnostics.md#d16)). Test one file saved with
Windows line endings and one with a stray carriage return.

A comment starts with `//` and continues to the end of the line. The only
subtlety is that `//` inside a string is ordinary text. In the line below, the
lexer must produce one string token containing `//`, and the comment starts
only at the second `//`:

```vortex
// statements: valid
let url = "a//b"; // not a comment inside the quotes
```

Block comments, written `/* ... */` in many languages, are not part of v0.1.
The lexer reports `/*` and `*/` outside a comment or literal as lexical errors
rather than reading them as `/` and `*`. One case to test: in `a *// note` the
lexer meets `*/` before `//`, so that line is an error, while `a * // note` is
fine.

## The longest match

Some operator spellings begin with other operator spellings. `=` is the start
of `==`. `.` is the start of `..`, which is the start of `..=`. When the lexer
sees `..=`, it could in principle produce `..` and then `=`. The specification
settles this: "the lexer uses the longest valid token", so `..=` is one
inclusive-range token. The same rule makes `<=` one token rather than `<`
followed by `=`, and `->` one token rather than `-` followed by `>`.

The rule has one case that surprises people. The range in a `for` loop is
written like this:

```vortex
// statements: valid
for index in 0..=4 {
    print(index);
}
```

The characters after `in` are `0..=4`. A careless lexer might start a
floating-point number at `0.` and then get confused. The number rule prevents
this: a `.` ends a number when the character after it is another `.`. In
`0..=4` it is, so the number is `0`, and the longest-match rule then reads
`..=`. Numbers are the one place where the lexer does not look for the longest
valid token: it takes the whole run of number characters and then checks it,
as the section on numbers explains.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-labelledby="munch-title munch-desc">
<title id="munch-title">The longest valid token wins</title>
<desc id="munch-desc">The text 0..=4 from a for loop can be cut into tokens in several ways. The chosen reading is 0, then ..=, then 4. Reading .. followed by = is rejected because a longer token, ..=, fits. Reading 0. as a floating-point number is rejected because a floating-point literal needs digits on both sides of the dot.</desc>
<text class="vx-mono" style="font-variant-ligatures: none" x="20" y="34">for index in 0..=4 {</text>
<text class="vx-text-muted" x="210" y="34">the characters in question: 0 . . = 4</text>
<rect class="vx-box-accent vx-pulse" x="40" y="60" width="34" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="57" y="85" text-anchor="middle">0</text>
<rect class="vx-box-accent vx-pulse" x="80" y="60" width="114" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="137" y="85" text-anchor="middle">..=</text>
<rect class="vx-box-accent vx-pulse" x="200" y="60" width="34" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="217" y="85" text-anchor="middle">4</text>
<text class="vx-text" x="300" y="77">Chosen: three tokens</text>
<text class="vx-text-muted" x="300" y="95">..= is the longest operator that fits here</text>
<rect class="vx-box-bad" x="40" y="140" width="34" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="57" y="165" text-anchor="middle">0</text>
<rect class="vx-box-bad" x="80" y="140" width="74" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="117" y="165" text-anchor="middle">..</text>
<rect class="vx-box-bad" x="160" y="140" width="34" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="177" y="165" text-anchor="middle">=</text>
<rect class="vx-box-bad" x="200" y="140" width="34" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="217" y="165" text-anchor="middle">4</text>
<text class="vx-text" x="300" y="157">Rejected: .. then =</text>
<text class="vx-text-muted" x="300" y="175">a longer valid token, ..=, starts at the same place</text>
<rect class="vx-box-bad" x="40" y="220" width="74" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="77" y="245" text-anchor="middle">0.</text>
<rect class="vx-box-bad" x="120" y="220" width="34" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="137" y="245" text-anchor="middle">.</text>
<rect class="vx-box-bad" x="160" y="220" width="34" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="177" y="245" text-anchor="middle">=</text>
<rect class="vx-box-bad" x="200" y="220" width="34" height="40"/>
<text class="vx-mono" style="font-variant-ligatures: none" x="217" y="245" text-anchor="middle">4</text>
<text class="vx-text" x="300" y="237">Rejected: 0. as a number</text>
<text class="vx-text-muted" x="300" y="255">a floating-point literal needs a digit after the dot</text>
</svg>
<figcaption>Figure 2. Three ways to cut <code>0..=4</code> into tokens. Only the first follows the rules. The second takes a shorter operator when a longer one fits. The third tries to read a number that the grammar does not allow.</figcaption>
</figure>

Test this case explicitly, along with `0..4`. Ranges are how every Vortex `for`
loop is written, including the loops in the matrix multiplication program at
the end of the roadmap, so a mistake here breaks a lot.

## Errors the lexer reports

The [specification](../../specification/lexical-structure.md#27-lexical-errors)
gives the complete list of lexical errors
([decision 19](../../decisions/lexical.md#d19)); anything not on it belongs to
a later stage. The lexer must reject:

- bytes that are not valid UTF-8;
- an invalid character, meaning one that cannot start any token (such as `@`
  or `#`), a non-ASCII character outside a comment or literal, a control
  character other than tab, line feed and carriage return, a carriage return
  with no line feed after it, or a byte order mark anywhere but the start of
  the file;
- `/*` or `*/` outside a comment or literal;
- a word reserved for a future version, such as `i64` or `const`;
- a malformed numeric literal, such as `0xff`, `010`, `1e5` or `1.0e`;
- an unterminated string or character literal;
- an unsupported escape, such as `\q`;
- a character literal with the wrong number of characters, such as `'ab'` or
  `''`.

Each one is reported with the category "lexical error" and a span covering the
offending characters, using the format from stage 1. The
[diagnostics chapter](../../specification/diagnostics.md#104-examples) gives
the canonical example:

```vortex
// statements: lexical error
let value = "unterminated;
```

The required result is a lexical error at the unterminated string.

The same section adds two rules about what happens next. The lexer "must always
make progress after reporting a lexical error", which means it may not report
the same character forever or stop dead in the middle of the file. And
the end of the file is "one stable end token" that does not consume
characters again and again. Together these mean that one bad character costs
one error message, and the lexer then carries on.

### Where these errors belong {#open-decisions-about-where-errors-belong}

Some invalid spellings look as if the lexer or the parser could catch them.
[Decision 17](../../decisions/lexical.md#d17) settles each case, and the
[lexical structure chapter](../../specification/lexical-structure.md#how-far-a-number-extends)
states the rules:

- a number followed directly by letters or digits it cannot contain (`2values`,
  `0xff`, `42u32`, `0b102`, `1_000`) is one malformed numeric literal, a
  lexical error covering the whole run;
- `/*` and `*/` are lexical errors ("block comments are not supported");
- `5.` followed by anything but a digit or a second `.` is a malformed numeric
  literal, while `.5` lexes as `.` and `5`, and the parser reports a syntax
  error.

Write a test for each, next to a similar valid case.

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- A token for every kind in the specification's lexical chapter, and the
  end-of-file token.
- Kind, original spelling and span on every token.
- Keywords recognized by exact, case-sensitive spelling, and reserved words
  rejected.
- Decimal and binary integers, floats with optional exponent, booleans,
  characters and strings, with the five escapes.
- The longest-match rule for operators and punctuation.
- Whitespace and `//` comments skipped, with `//` inside strings kept as text.
- Every lexical error from the specification, reported in the stage 1 format
  with an accurate span.
- Progress after every error, and exactly one end token.
- The driver's `--tokens` option, which prints the token stream.
- Tests for every token kind and every lexical error in the specification's
  list, including malformed numeric runs and the block-comment markers.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Checking that a name was declared: that is
  [stage 4](stage-4-names-and-scopes.md).
- Checking that a number fits its type: that needs types, from
  [stage 5](stage-5-types-and-rules.md).
- Knowing that `i32` names a type: the lexer only sees a keyword.
- Hexadecimal, octal, separators, suffixes, raw or multiline strings, block
  comments: none are in v0.1.
- Unicode identifiers: v0.1 identifiers are ASCII only.
- Speed tricks: a Vortex file is small, and clarity matters more here.
- Colored token printouts or editor highlighting.

</div>
</div>

## What you do not need yet

The right-hand column is short on purpose. The lexer's job is narrow, and most
of what it is tempting to add belongs to a later stage. Anything that needs to
know what a name refers to, or what type a value has, is out of bounds here by
the [architecture page's contract](../architecture.md#pass-contracts).

## How you know it is finished

The roadmap says Milestone 2 is complete "when a Vortex file can be printed as
a correct token stream with accurate source locations". In detail:

- every example of valid token text in the lexical chapter and the tour
  produces the tokens you expect, with the right kinds, spellings and
  positions;
- every lexical error in the specification's list has a test that expects a
  lexical error at a specific span, and a nearby valid test next to it (for
  instance `'a'` next to `'ab'`), as the
  [diagnostics chapter](../../specification/diagnostics.md#107-verification-requirements)
  asks;
- `String` and `string`, `..=` and `..`, `0..4` and `1.0e-4` all come out as the
  specification says;
- a file with several bad characters produces one error for each, and the
  lexer reaches the end;
- each case in "Where these errors belong" has a test;
- every earlier test still passes.

## Traps

**Case-insensitive keywords.** `String` is a keyword and `string` is not. A
lexer that folds case before comparing will get this wrong, and so will one
that copies its keyword list from another language.

**Swallowing the minus sign.** It is tempting to read `-42` as one negative
number. The grammar says the minus is a separate operator. Getting this wrong
makes `x-1` come out as `x` followed by `-1`, which the parser cannot use.

**Reading a range as a number.** `0..4` must not start a floating-point literal.
Test it.

**Checking number sizes in the lexer.** The lexer does not know whether `300`
will become an `i32` or a `u32` or something else. Range checks belong to type
checking.

**Losing the spelling.** If a token keeps only its value, the parser cannot
quote `0b101` in an error message, and the specification's rule that later
stages must never rebuild spelling from the kind is broken.

**Stopping at the first error.** One stray `@` should produce one message, not
end the whole run. It also must not produce the same message over and over.

**Doing the parser's job.** The lexer does not know that `let` must be
followed by a name, or that braces must match. Those are syntax errors, found
in stage 3. A lexer that tries to check them duplicates work and gets
confusing.

## How others teach this stage

**Kaleidoscope, chapter 1.** The LLVM tutorial's first chapter builds its lexer
before anything else and describes the job as breaking the input up into
tokens.[^kal1] Its lexer hands back one token each time it is asked, reads
identifiers and keywords with the same loop and then compares the spelling
with its two keywords, and skips comments to the end of the line, much as
Vortex does. It differs from what Vortex needs in two plain ways. It keeps no
source positions, and it admits that its number handling does not do enough
error checking, so text like `1.23.45.67` is read as if it were `1.23`. Vortex needs
positions on every token and exact number rules from the first day.

**Crafting Interpreters, "Scanning".** Nystrom's chapter is the most readable
account of this stage anywhere. It defines lexemes and tokens, explains why the
scanner keeps going after an error so it can report as many as possible in one
run, and names the longest-match rule, maximal munch, in plain
terms.[^ci-scan] It also handles reserved words the same way this page
describes: read a word as an identifier, then check it against the list. The
differences are in the details of the language. Lox allows strings to run over
several lines; Vortex does not. Lox's tokens store only a line number; Vortex
needs full spans.

**Crafting Interpreters, "Scanning on Demand".** In the book's second
implementation, Nystrom writes a second scanner that produces a token only when the
compiler asks for one, instead of producing the whole list up front, and
reports errors by handing the compiler a special error token.[^ci-ondemand]
Tokens there refer back to their text in the source instead of copying it,
which only works while the source stays in memory. The Vortex specification
states the same condition. Whether your lexer produces all tokens at once or
one at a time is your choice. Vortex requires neither. Both must print the same
token stream.

[^kal1]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 1, "Kaleidoscope Introduction and the Lexer". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl01.html>
[^ci-scan]: Robert Nystrom, *Crafting Interpreters*, chapter "Scanning". <https://craftinginterpreters.com/scanning.html>
[^ci-ondemand]: Robert Nystrom, *Crafting Interpreters*, chapter "Scanning on Demand". <https://craftinginterpreters.com/scanning-on-demand.html>
