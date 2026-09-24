# Lexical structure and source text

These records cover how Vortex reads a source file: its encoding, the shape of
numeric literals and comment markers, how `true` and `false` are classified,
the complete list of lexical errors, and the words kept back for later
versions. The rules themselves are stated in the
[Lexical structure](../specification/lexical-structure.md) chapter and the
pages it links to; each record explains the question, the options and why the
rule was chosen.

## 15. Source encoding {#d15}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 1, 2.

**Question.** Which bytes may a source file contain, and what is one
character?

**Before this decision.** [Lexical structure](../specification/lexical-structure.md#25-literals)
allowed `'λ'` in a character literal, but no page named the file's encoding or
said what happens to invalid bytes, a byte order mark (an invisible marker
some editors put first in a file) or control characters (such as NUL).
[Stage 1](../compiler/guide/stage-1-source-and-diagnostics.md#characters-bytes-and-encoding)
left this open.

**Options.**

- UTF-8 only, or several encodings.
- Ignore a leading byte order mark, or reject it.
- A character is one Unicode scalar value (one code point) or one grapheme
  cluster (what a reader sees as one letter).

**Elsewhere.** Go, Rust and Zig all require UTF-8. Go may ignore a leading
byte order mark ([Go specification][go-src]), Rust removes it
([Rust Reference][rs-input]) and Zig ignores it
([Zig documentation][zig-encoding]).

**Decision.** A source file must be valid UTF-8; invalid UTF-8 is a lexical
error. A U+FEFF as the first character is ignored and takes no column;
anywhere else it is a lexical error. The only control characters allowed are
tab, line feed (LF) and carriage return (CR), and a CR must be followed by LF
([decision 16](diagnostics.md#d16)); any other is a lexical error, even in a
comment or literal. Non-ASCII characters may appear only in comments, string
literals and character literals. A source character, and a `char` value, is
one Unicode scalar value: any code point except the surrogates U+D800 to
U+DFFF.

**Why.** UTF-8 already encodes `String`. Scalar values come straight from
decoding, while grapheme boundaries depend on Unicode tables that change
between versions. Banning invisible control characters keeps every character
visible in review.

**Consequences.** ASCII files are unaffected. A letter typed as a base letter
plus a combining accent is two scalar values, so it does not fit in a
character literal; the precomposed letter does. Stage 1 reports invalid UTF-8
while decoding; stage 2 reports the rest. Changed:
[Lexical structure](../specification/lexical-structure.md#21-whitespace-and-line-boundaries)
2.1 and 2.5, Types and values 4.2 and 4.6, Conformance 1.2, and the pages
that describe characters.

## 17. Malformed numeric literals and comment markers {#d17}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N29,
N30. **Guide stage:** 2.

**Question.** Is number-like text that is not a valid literal, such as
`2values` or `1.0e`, one error or several tokens? And what are `010`,
`0B1`, `1e5`, `/*` and `*/`?

**Before this decision.** [Lexical structure](../specification/lexical-structure.md#25-literals)
2.5 and the grammar rejected `0xff`, `1_000`, `42u32`, `.5` and `5.` without
saying how, and accepted `010` as ten. Nothing covered `0B1`, `1e5` or `*/`;
[stage 2](../compiler/guide/stage-2-lexer.md#open-decisions-about-where-errors-belong)
left it to implementers.

**Options.**

- Longest valid prefix: `0b102` becomes `0b10` then `2`.
- Whole run: one piece from the first digit, one error if invalid.
- Read `010` as ten, or reject it.

**Elsewhere.** C++ reads a whole "preprocessing number" before checking it
([cppreference][cpp-phases]) and reads a leading `0` as octal
([cppreference][cpp-intlit]); Swift ignores leading zeros
([Swift book][sw-intlit]). Rust block comments nest
([Rust Reference][rs-comments]), and Zig has no multiline comments
([Zig documentation][zig-comments]).

**Decision.** A number starts at a decimal digit and takes the longest run of
ASCII letters, digits, `_`, any `.` not followed by another `.`, and a `+` or
`-` right after `e` or `E`. The run must be `0`, a nonzero digit followed by
digits, `0b` and binary digits, or digits, `.`, digits and an optional
exponent. Any other run is one malformed numeric literal, a lexical error
spanning the run. `.5` is `.` then `5`: a syntax error. Outside comments and
literals, `/*` and `*/` are lexical errors.

**Why.** One typo gives one error at the right place. Rejecting `010` avoids
the octal trap for C readers; rejecting `0B1` and `1e5` keeps the grammar's
forms. A `.` before another `.` ends the run, so `0..10` stays a
range.

**Consequences.**

```vortex
// statements: lexical error
let mask = 010; // malformed numeric literal: leading zero
```

`0B1`, `1e5`, `1.0e`, `5.` and `1_000` fail the same way, and so does
`a *// note`, where the lexer meets `*/` before `//`. The parser rejects `.5`
and may suggest `0.5`. Changed:
[Lexical structure](../specification/lexical-structure.md) 2.2, 2.3, 2.5 and
2.6, the [grammar](../specification/grammar.md#literals), tour chapters 2 and
8, and stage 2.

## 18. `true` and `false` token kind {#d18}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 2.

**Question.** Are `true` and `false` keywords or literals, and must a lexer
give them a particular token kind?

**Before this decision.** [Lexical structure](../specification/lexical-structure.md#24-keywords)
2.4 lists `true` and `false` among the reserved spellings, and 2.5 calls them
the two boolean literals. [Stage 2](../compiler/guide/stage-2-lexer.md#keywords-and-identifiers)
called the token kind "a small open decision". The two statements agree; the
open point was only whether the token kind is fixed.

**Options.**

- Keyword tokens that the parser turns into literal values.
- Boolean-literal tokens.
- Predeclared names, looked up like any other name.

**Elsewhere.** C++ makes its boolean literals keywords
([cppreference][cpp-bool]), and Rust lists `true` and `false` among its strict
keywords ([Rust Reference][rs-kw-strict]). Go makes them predeclared
identifiers instead ([Go specification][go-predecl]).

**Decision.** `true` and `false` are keywords that denote the two `bool`
values. They must never be treated as identifiers. An implementation may give
them a keyword token kind or a boolean-literal token kind; both conform.

**Why.** The choice is invisible to programs: both token kinds accept and
reject exactly the same source, so fixing one would constrain lexers without
changing any rule. Keeping the words in the keyword list, as C++ and Rust do,
keeps them out of name lookup.

**Consequences.** No program changes. Tests must not depend on the token kind,
and a token printout ([I4](implementation.md#i4)) may show either. The parser
must accept whichever kind its lexer produces wherever a literal may appear.
Changed: [Lexical structure](../specification/lexical-structure.md) 2.4 and
2.5, and the keyword table in stage 2.

## 19. Lexical error list {#d19}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 2.

**Question.** Which inputs must the lexer reject, and where is the complete
list?

**Before this decision.** [Lexical structure](../specification/lexical-structure.md#27-lexical-errors)
2.7 listed six errors. [Diagnostics](../specification/diagnostics.md#lexical-error)
10.2 gave four "examples", one of them a "malformed number" that the lexical
chapter never defined. [Stage 2](../compiler/guide/stage-2-lexer.md#errors-the-lexer-reports)
repeated the six and left three more cases open.

**Options.**

- Keep example lists on several pages.
- Make one list complete and normative (binding on every implementation), and
  link to it from everywhere else.

**Elsewhere.** The Rust Reference ([Rust Reference][rs-intlit]) and the Go
specification ([Go specification][go-tokens]) each define their tokens
normatively in one part of the document.

**Decision.** Lexical structure 2.7 is the complete list of lexical errors. A
conforming lexer must reject exactly these, each with the category "lexical
error":

1. invalid UTF-8 ([decision 15](#d15));
2. an invalid character: one that cannot begin any token, a non-ASCII
   character outside comments and literals, a forbidden control character, a
   CR without LF, or U+FEFF after the start ([decision 15](#d15));
3. a block-comment marker, `/*` or `*/` ([decision 17](#d17));
4. a word reserved for a future version ([decision 29](#d29));
5. a malformed numeric literal ([decision 17](#d17));
6. an unterminated string or character literal;
7. an unsupported escape;
8. a character literal that holds no character or more than one.

Every other error belongs to a later phase. Diagnostics 10.2 links to this
list instead of giving examples.

**Why.** Tests can enumerate one list, and every invalid input has exactly one
expected category, which the example labels of
[decision 28](documentation.md#d28) rely on.

**Consequences.** No valid program changes. The old entries "`0b` with no
following binary digit" and "malformed floating-point exponent text" become
cases of a malformed numeric literal. Stage 2 needs one failing test per
entry, each next to a similar valid test. Changed: Lexical structure 2.7,
Diagnostics 10.2, stage 2 and the roadmap's Milestone 2.

## 29. Reserved words and `as` {#d29}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 2.

**Question.** May a program use the names of planned types, such as `i64` or
`bf16`, as its own names? And is `as` a keyword?

**Before this decision.** [Lexical structure](../specification/lexical-structure.md#24-keywords)
2.4 reserved only the v0.1 keywords, so `i64` was an ordinary identifier. The
[grammar](../specification/grammar.md#types) said `u64` "parses as a named
type, then fails name resolution", and
[Types planned for later](../language-tour/05-types-planned-for-later.md#additional-primitive-types)
allowed a struct named `i64`. No page mentioned `as`.

**Options.**

- Leave the planned names free, and break programs that use them when the
  types arrive.
- Reserve them now, rejected by the lexer or by the parser.
- Also reserve `as` for a cast operator.

**Elsewhere.** Rust reserves keywords that have no meaning yet, for future use
([Rust Reference][rs-kw]), and Zig publishes one fixed keyword list
([Zig documentation][zig-keywords]). Go's keywords leave out type names, which
are predeclared identifiers instead ([Go specification][go-keywords]).

**Decision.** `i8`, `i16`, `i64`, `u8`, `u16`, `u64`, `f16`, `bf16` and
`const` are reserved for a future version. They have no meaning in v0.1, and
each one outside a comment or literal is a lexical error, "reserved for a
future version". Matching is by whole word and case-sensitive. `as` is not
reserved: casts are written `f32(x)` ([decision 1](numbers.md#d1)). Whether
`kernel` is reserved is decided when kernels are specified.

**Why.** Reserving costs nothing now, since no example uses these names, and a
later version can add the types and constant declarations without breaking a
v0.1 program. The check needs only the word's spelling, so the lexer makes it,
in one place.

**Consequences.**

```vortex
// items: lexical error
struct u64 { // u64 is reserved for a future version
    high: u32,
    low: u32,
}
```

`u64_count`, `constant` and `I64` remain identifiers. Stage 2 checks the list
whenever it reads a word, so `u64` no longer reaches name resolution. Changed:
[Lexical structure](../specification/lexical-structure.md#24-keywords) 2.3,
2.4 and 2.7, the grammar, Types and values 4.13, the glossary, the cheat
sheet, tour chapters 2, 4 and 5, and stages 2, 3 and 4.

[go-src]: https://go.dev/ref/spec#Source_code_representation
[rs-input]: https://doc.rust-lang.org/reference/input-format.html
[zig-encoding]: https://ziglang.org/documentation/0.16.0/#Source-Encoding
[cpp-phases]: https://en.cppreference.com/cpp/language/translation_phases
[cpp-intlit]: https://en.cppreference.com/cpp/language/integer_literal
[sw-intlit]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/lexicalstructure#Integer-Literals
[rs-comments]: https://doc.rust-lang.org/reference/comments.html
[zig-comments]: https://ziglang.org/documentation/0.16.0/#Comments
[cpp-bool]: https://en.cppreference.com/cpp/language/bool_literal
[rs-kw-strict]: https://doc.rust-lang.org/reference/keywords.html#strict-keywords
[go-predecl]: https://go.dev/ref/spec#Predeclared_identifiers
[rs-intlit]: https://doc.rust-lang.org/reference/tokens.html#integer-literals
[go-tokens]: https://go.dev/ref/spec#Tokens
[rs-kw]: https://doc.rust-lang.org/reference/keywords.html#reserved-keywords
[zig-keywords]: https://ziglang.org/documentation/0.16.0/#Keyword-Reference
[go-keywords]: https://go.dev/ref/spec#Keywords
