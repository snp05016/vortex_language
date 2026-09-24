# Basic source rules

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
