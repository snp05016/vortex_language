# Basic source rules

--8<-- "includes/remember/language-tour__02-basic-source-rules.md"

## Learning goals

After this chapter, you should be able to write valid identifiers, place
semicolons and braces, add comments, and use the supported escapes.

## Comments

### Syntax

```vortex
// statements: valid
// comment text continues to the end of this line
```

### Allowed

- `//` comments on their own line.
- `//` comments after code.
- Comments between [tokens](../specification/glossary.md) where whitespace is allowed.

```vortex
// statements: valid
let width = 128; // image width
```

### Not allowed

Block comments are not part of v0.1:

```vortex
// statements: lexical error
/* not valid v0.1 syntax */
```

The lexer (the [compiler stage](../compiler/guide/index.md) that splits source text into tokens) rejects `/*` and `*/` outside comments and literals, with a message
that block comments are not supported
([decision 17](../decisions/lexical.md#d17)). It skips a `//` comment but does
not treat `//` inside a string as a comment.

??? check "Does the lexer start a comment at the `//` inside `let url = \"https://example.com\";`?"

    No. The `//` is inside a string literal, so it is ordinary string content,
    not the start of a comment. The lexer only looks for `//` outside a string
    or character literal.

## Statements, blocks, and semicolons

Simple statements end in `;`. Blocks use `{}` and do not take a semicolon after
the closing brace.

```vortex
// statements: valid
let value = 10;
print(value);

if value > 0 {
    print("positive");
}
```

These forms are invalid:

```vortex
// statements: syntax error
let value = 10       // invalid: missing semicolon

if value > 0 {
    print(value);
};                    // invalid: block statement followed by semicolon
```

The parser (the compiler stage that checks how tokens fit together into
statements and blocks) enforces statement termination and matching braces.

??? check "Does the `if` block above need a `;` after its closing `}`?"

    No. Only simple statements end in `;`. A block statement, delimited by
    `{}`, never takes a trailing semicolon.

## Identifiers

An identifier is a programmer-defined name.

### Syntax

```text
first character: ASCII letter or _
later characters: ASCII letters, digits, or _
```

### Allowed

```vortex
// statements: valid
let value = 1;
let item_count = 2;
let _temporary = 3;
let Value2 = 4;
```

Names are case-sensitive, so `value` and `Value` are different names.

Source files are UTF-8 text; characters outside ASCII, such as `λ` or `é`, may
appear in comments, strings and character literals, but not in names or
anywhere else in the code
([Lexical structure](../specification/lexical-structure.md#source-encoding),
[decision 15](../decisions/lexical.md#d15)).

### Not allowed

```vortex
// statements: lexical error
let 2values = 2; // lexical error: read as one malformed number
```

```vortex
// statements: syntax error
let item-count = 2; // invalid: syntax error at "-"; a name cannot contain a hyphen
let fn = 2;         // invalid: fn is a keyword
```

The lexer determines whether text is an identifier or keyword. Name resolution
later checks whether an identifier was declared and is visible.

A few more words are reserved for future versions and cannot be names either:
`i8`, `i16`, `i64`, `u8`, `u16`, `u64`, `f16`, `bf16` and `const`
([decision 29](../decisions/lexical.md#d29)).

??? check "v0.1 has no `u64` type. Can a program still use `u64` as the name of a struct or variable?"

    No. `u64` is reserved for a future version, so it is a lexical error
    wherever it appears outside a comment or literal, even though the type
    itself does not exist yet.

## String and character escapes

Strings use double quotes. Characters use single quotes.

| Escape | Meaning |
| --- | --- |
| `\n` | new line |
| `\t` | tab |
| `\\` | backslash |
| `\"` | double quote |
| `\'` | single quote |

Valid examples:

```vortex
// statements: valid
print("first line\nsecond line");
let quote: char = '\'';
let path = "left\\right";
```

Invalid examples:

```vortex
// statements: lexical error
let bad = "unknown: \q"; // invalid: unsupported escape
let two: char = 'ab'; // invalid: more than one character
let open = "missing end; // invalid: unterminated string
```

The lexer validates delimiters and escape spellings. Type checking later
distinguishes `char` from `String`.

??? check "`'\\q'` and `\"\\q\"` both contain the escape `\q`. Does putting it in a string instead of a character literal make it valid?"

    No. `\q` is not one of the five supported escapes in either kind of
    literal, so both are lexical errors.

## Practice and self-check

Correct this source:

```vortex
// fragment
let 1st_value = "line one\qline two"
```

One valid answer is:

```vortex
// statements: valid
let first_value = "line one\nline two";
```

## Key ideas

!!! recap

    - **What is not part of v0.1 comments?** Block comments (`/* ... */`);
      only `//` line comments are accepted.
    - **Does `//` inside a string literal start a comment?** No. The lexer
      only looks for `//` outside a string or character literal.
    - **Which statements take a trailing `;`?** Simple statements. A block
      statement, delimited by `{}`, never takes one.
    - **What may an identifier start with?** An ASCII letter or `_`, never a
      digit.
    - **Are `value` and `Value` the same name?** No. Identifiers are
      case-sensitive.
    - **Can `u64` or `const` be used as a name in v0.1?** No. Both are
      reserved for a future version and are lexical errors wherever they
      appear outside a comment or literal.
    - **Which five escapes does v0.1 support?** `\n`, `\t`, `\\`, `\"` and
      `\'`; any other backslash escape is a lexical error.

## Where this comes back

--8<-- "includes/next/language-tour__02-basic-source-rules.md"
