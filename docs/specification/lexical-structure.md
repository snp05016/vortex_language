# 2. Lexical structure

The lexer converts source characters into tokens. It removes whitespace and
comments that separate tokens, preserves token spelling and source spans, and
reports malformed token text.

## 2.1 Whitespace and line boundaries

<a class="vx-rule" id="lex.whitespace.separators" href="#lex.whitespace.separators">lex.whitespace.separators</a> Spaces, horizontal tabs, and line breaks separate tokens. A line break has no
statement-terminating meaning. Statements that require a semicolon must contain
`;` even when followed by a line break.

<a class="vx-rule" id="lex.whitespace.line-break" href="#lex.whitespace.line-break">lex.whitespace.line-break</a> A line break is a line feed (LF, U+000A), or a carriage return (CR, U+000D)
immediately followed by a line feed; the CR LF pair counts as one line break.
A carriage return that is not immediately followed by a line feed is a lexical
error. [Conformance 1.7](conformance.md#17-source-locations) defines how lines
and columns are counted ([decision 16](../decisions/diagnostics.md#d16)).

```vortex
// statements: valid
let width = 128;
let area =
    width * width;
```

Both declarations are valid.

### Source encoding

<a class="vx-rule" id="lex.encoding.utf8" href="#lex.encoding.utf8">lex.encoding.utf8</a> A source file must be encoded in UTF-8. The lexer reads it as a sequence of
source characters, and each source character is one Unicode scalar value: a
Unicode code point other than a surrogate (U+D800 to U+DFFF).

- <a class="vx-rule" id="lex.encoding.invalid-utf8" href="#lex.encoding.invalid-utf8">lex.encoding.invalid-utf8</a> A byte sequence that is not valid UTF-8 is a lexical error.
- <a class="vx-rule" id="lex.encoding.bom" href="#lex.encoding.bom">lex.encoding.bom</a> If the first character of the file is U+FEFF (a byte order mark), the lexer
  ignores it: it is not part of any token and occupies no column. U+FEFF
  anywhere else is a lexical error.
- <a class="vx-rule" id="lex.encoding.control-chars" href="#lex.encoding.control-chars">lex.encoding.control-chars</a> Horizontal tab (U+0009), line feed and carriage return are the only control
  characters allowed. Any other control character (U+0000 to U+001F, U+007F,
  or U+0080 to U+009F) is a lexical error, including inside a comment or a
  literal.
- <a class="vx-rule" id="lex.encoding.non-ascii" href="#lex.encoding.non-ascii">lex.encoding.non-ascii</a> A non-ASCII character (U+0080 or above) may appear only inside a comment, a
  string literal or a character literal. Anywhere else it is a lexical error.

See [decision 15](../decisions/lexical.md#d15).

## 2.2 Comments

<a class="vx-rule" id="lex.comments.line" href="#lex.comments.line">lex.comments.line</a> A line comment begins with `//` outside a string or character literal and
continues to the end of the line.

```vortex
// statements: valid
// Allocate one row.
let row: [f32; 4] = [0.0; 4]; // four zero values
```

Block comments are not part of v0.1:

```vortex
// statements: lexical error
/* invalid v0.1 syntax */
```

<a class="vx-rule" id="lex.comments.block-marker" href="#lex.comments.block-marker">lex.comments.block-marker</a> Outside a comment, a string literal or a character literal, the character
pairs `/*` and `*/` are lexical errors ("block comments are not supported").
The lexer must report them rather than read them as `/` and `*`. This includes
`*/` directly before a line comment: in `a *// note` the lexer meets `*/`
first, so write `a * // note`. See [decision 17](../decisions/lexical.md#d17).

<a class="vx-rule" id="lex.comments.string-content" href="#lex.comments.string-content">lex.comments.string-content</a> The character sequence `//` inside a string literal is ordinary string
content.

## 2.3 Identifiers

<a class="vx-rule" id="lex.identifiers.grammar" href="#lex.identifiers.grammar">lex.identifiers.grammar</a> An identifier begins with an ASCII letter or `_`. Each later character is an
ASCII letter, decimal digit, or `_`.

```ebnf
identifier ::=
    identifier_start, { identifier_continue } ;

identifier_start ::=
      letter
    | "_" ;

identifier_continue ::=
      letter
    | digit
    | "_" ;
```

<a class="vx-rule" id="lex.identifiers.case-sensitive" href="#lex.identifiers.case-sensitive">lex.identifiers.case-sensitive</a> Identifiers are case-sensitive. `value`, `Value`, and `VALUE` are distinct
names.

Valid spellings include:

```text
value
item_count
_temporary
Matrix2
```

<a class="vx-rule" id="lex.identifiers.reserved-words" href="#lex.identifiers.reserved-words">lex.identifiers.reserved-words</a> Invalid identifier spellings include `2values`, `item-count`, and non-ASCII
letters. Because `2values` begins with a digit, the lexer reads it as one
malformed numeric literal ([How far a number extends](#how-far-a-number-extends)).
A keyword, or a word reserved for a future version ([2.4](#24-keywords)),
cannot be used as an identifier.

## 2.4 Keywords

<a class="vx-rule" id="lex.keywords.list" href="#lex.keywords.list">lex.keywords.list</a> The following spellings are keywords. Each has a fixed meaning in v0.1 and
cannot be used as an identifier:

```text
fn      struct  let     mut     return
if      else    while   for     in
break   continue
void    bool    char    i32     u32
usize   f32     f64     String
true    false
```

<a class="vx-rule" id="lex.keywords.case-sensitive" href="#lex.keywords.case-sensitive">lex.keywords.case-sensitive</a> Keyword matching is case-sensitive. `String` is a keyword, while `string` is
an identifier.

<a class="vx-rule" id="lex.keywords.true-false" href="#lex.keywords.true-false">lex.keywords.true-false</a> `true` and `false` are keywords that denote the two `bool` values
([Boolean literals](#boolean-literals)). An implementation may classify them
as keyword tokens or as boolean-literal tokens; both conform, and neither may
be treated as an identifier. See [decision 18](../decisions/lexical.md#d18).

<a class="vx-rule" id="lex.keywords.reserved-list" href="#lex.keywords.reserved-list">lex.keywords.reserved-list</a> The following spellings are reserved for a future version:

```text
i8      i16     i64     u8      u16
u64     f16     bf16    const
```

<a class="vx-rule" id="lex.keywords.reserved-error" href="#lex.keywords.reserved-error">lex.keywords.reserved-error</a> They have no meaning in v0.1. Each occurrence of one of these words outside a
comment, string literal or character literal is a lexical error ("reserved for
a future version"), whether it is written as a type, a variable, a field or a
struct name. A match must be the whole word with the same case: `u64_count`,
`constant` and `I64` are identifiers. See
[decision 29](../decisions/lexical.md#d29).

## 2.5 Literals

### Integer literals

<a class="vx-rule" id="lex.literals.integer" href="#lex.literals.integer">lex.literals.integer</a> Decimal integer literals contain one or more decimal digits and must not begin
with `0` unless the literal is `0` itself, so `010` is invalid. Binary integer
literals begin with a lowercase `0b` followed by one or more binary digits;
`0B101` is invalid.

```vortex
// fragment
0
42
0b101010
```

<a class="vx-rule" id="lex.literals.integer-restrictions" href="#lex.literals.integer-restrictions">lex.literals.integer-restrictions</a> Hexadecimal, octal, digit separators, and suffixes are not accepted in v0.1.
A leading minus sign is a unary operator, not part of the literal. The lexer
does not check whether an integer literal fits any type: a literal of any
length is one token, and type checking reports a value that does not fit
([Types and values 4.3](types-and-values.md#43-integers)).

### Floating-point literals

<a class="vx-rule" id="lex.literals.float" href="#lex.literals.float">lex.literals.float</a> A floating-point literal requires digits on both sides of `.` and may end with
an exponent: `e` or `E`, an optional `+` or `-`, and one or more digits. An
exponent needs the decimal point, so `1e5` is invalid and `1.0e5` is valid.

```vortex
// fragment
3.14
1.0e-4
6.02E+23
```

<a class="vx-rule" id="lex.literals.float-spelling" href="#lex.literals.float-spelling">lex.literals.float-spelling</a> `5.` is a malformed numeric literal. `.5` is not a numeric literal: it lexes
as `.` followed by `5`, and the parser rejects it (syntax error). Write `5.0`
and `0.5`. The lexer keeps a floating literal's spelling; type checking rounds
it once, to its final type
([Types and values 4.4](types-and-values.md#44-floating-point-values)).
[Decision record](../decisions/numbers.md#d32).

### How far a number extends

<a class="vx-rule" id="lex.literals.number-extent" href="#lex.literals.number-extent">lex.literals.number-extent</a> A numeric literal begins with a decimal digit. From there the lexer takes the
longest run of characters in which each character is:

- an ASCII letter, a decimal digit, or `_`;
- a `.` that is not immediately followed by another `.`; or
- a `+` or `-` immediately after an `e` or `E` in the run.

<a class="vx-rule" id="lex.literals.number-run" href="#lex.literals.number-run">lex.literals.number-run</a> The whole run must be one integer or floating-point literal as defined above.
Otherwise the run is one malformed numeric literal: one lexical error whose
span covers the whole run. For example, `2values`, `0xff`, `0B101`, `0b102`,
`1_000`, `42u32`, `010`, `1e5`, `1.0e`, `5.` and `1.0.5` are each one
malformed numeric literal. Because a `.` followed by another `.` ends the run,
`0..10` is the integer `0`, the operator `..` and the integer `10`. See
[decision 17](../decisions/lexical.md#d17).

### Boolean literals

<a class="vx-rule" id="lex.literals.boolean" href="#lex.literals.boolean">lex.literals.boolean</a> The two boolean literals are the keywords `true` and `false`
([2.4](#24-keywords)).

### Character and string literals

<a class="vx-rule" id="lex.literals.quote-style" href="#lex.literals.quote-style">lex.literals.quote-style</a> Characters use single quotes. Strings use double quotes.

```vortex
// fragment
'A'
'λ'
"Vortex"
"first line\nsecond line"
```

<a class="vx-rule" id="lex.literals.char-string-content" href="#lex.literals.char-string-content">lex.literals.char-string-content</a> A character literal contains exactly one source character other than `'`,
`\`, a line feed or a carriage return, or one supported escape. A source
character is one Unicode scalar value ([Source encoding](#source-encoding)),
so `'λ'` is valid, while a letter typed as a base letter followed by a
separate combining accent is two source characters and is not a valid
character literal. A string literal contains any number of source characters
other than `"`, `\`, a line feed or a carriage return, and supported escapes.
A string or character literal must close on the line where it starts. String
and character literals may use these escapes:

| Escape | Decoded value |
| --- | --- |
| `\n` | Line feed |
| `\t` | Horizontal tab |
| `\\` | Backslash |
| `\"` | Double quote |
| `\'` | Single quote |

<a class="vx-rule" id="lex.literals.string-restrictions" href="#lex.literals.string-restrictions">lex.literals.string-restrictions</a> Raw strings, byte strings, multiline strings, and other escapes are outside
v0.1.

## 2.6 Operators and punctuation

<a class="vx-rule" id="lex.operators.list" href="#lex.operators.list">lex.operators.list</a> The lexer recognizes the following operator spellings:

```text
+   -   *   /   %
=   ==  !=  <   >   <=  >=
&&  ||  !
&   |   ^   ~   <<  >>
+=  -=  *=  /=  %=
..  ..=
```

<a class="vx-rule" id="lex.operators.punctuation" href="#lex.operators.punctuation">lex.operators.punctuation</a> It also recognizes:

```text
( ) { } [ ] ; . : , ->
```

<a class="vx-rule" id="lex.operators.maximal-munch" href="#lex.operators.maximal-munch">lex.operators.maximal-munch</a> When one token spelling is a prefix of another, the lexer uses the longest
valid token. For example, `..=` is one inclusive-range token rather than `..`
followed by `=`.

<a class="vx-rule" id="lex.operators.maximal-munch-scope" href="#lex.operators.maximal-munch-scope">lex.operators.maximal-munch-scope</a> This rule applies to operators and punctuation. A number follows
[How far a number extends](#how-far-a-number-extends) instead: the lexer takes
the whole run and then checks it, so `0b102` is one malformed numeric literal,
not `0b10` followed by `2`.

## 2.7 Lexical errors

<a class="vx-rule" id="lex.errors.complete-list" href="#lex.errors.complete-list">lex.errors.complete-list</a> This list is complete. The lexer must report each of the following as a
lexical error, and every other error belongs to a later phase
([decision 19](../decisions/lexical.md#d19)):

- <a class="vx-rule" id="lex.errors.invalid-utf8" href="#lex.errors.invalid-utf8">lex.errors.invalid-utf8</a> **Invalid UTF-8:** bytes that are not valid UTF-8
  ([Source encoding](#source-encoding)).
- <a class="vx-rule" id="lex.errors.invalid-character" href="#lex.errors.invalid-character">lex.errors.invalid-character</a> **Invalid character:** a character that cannot begin any token and is not
  whitespace, such as `@`, `#` or `$`; a non-ASCII character outside a
  comment, string literal or character literal; a control character other
  than horizontal tab, line feed and carriage return; a carriage return not
  followed by a line feed; or U+FEFF anywhere except as the first character of
  the file ([2.1](#21-whitespace-and-line-boundaries)).
- <a class="vx-rule" id="lex.errors.block-comment-marker" href="#lex.errors.block-comment-marker">lex.errors.block-comment-marker</a> **Block-comment marker:** `/*` or `*/` outside a comment, string literal or
  character literal ([2.2](#22-comments)).
- <a class="vx-rule" id="lex.errors.reserved-word" href="#lex.errors.reserved-word">lex.errors.reserved-word</a> **Reserved word:** a word reserved for a future version
  ([2.4](#24-keywords)).
- <a class="vx-rule" id="lex.errors.malformed-numeric" href="#lex.errors.malformed-numeric">lex.errors.malformed-numeric</a> **Malformed numeric literal:** a numeric run that is not a valid literal
  ([How far a number extends](#how-far-a-number-extends)).
- <a class="vx-rule" id="lex.errors.unterminated-literal" href="#lex.errors.unterminated-literal">lex.errors.unterminated-literal</a> **Unterminated string or character literal:** the closing quote is missing
  before the end of the line or of the file.
- <a class="vx-rule" id="lex.errors.unsupported-escape" href="#lex.errors.unsupported-escape">lex.errors.unsupported-escape</a> **Unsupported escape:** a backslash in a string or character literal
  followed by anything other than `n`, `t`, `\`, `"` or `'`.
- <a class="vx-rule" id="lex.errors.wrong-character-count" href="#lex.errors.wrong-character-count">lex.errors.wrong-character-count</a> **Wrong character count:** a character literal that holds no character, or
  more than one character or escape, such as `''` or `'ab'`.

<a class="vx-rule" id="lex.errors.span" href="#lex.errors.span">lex.errors.span</a> The span of each error covers the offending text: the whole run for a
malformed numeric literal, and both characters of a block-comment marker.

<a class="vx-rule" id="lex.errors.progress" href="#lex.errors.progress">lex.errors.progress</a> The lexer must always make progress after reporting a lexical error. End of
file is represented by one stable end token and does not consume source
characters repeatedly.

## 2.8 Token source data

<a class="vx-rule" id="lex.tokens.source-data" href="#lex.tokens.source-data">lex.tokens.source-data</a> Each token preserves its kind, original spelling, and source span. Later stages
must not need to reconstruct spelling from the token kind. If token text points
into the original source buffer, that buffer must remain alive for the complete
lifetime of the tokens.
