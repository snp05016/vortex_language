"""Pygments lexer for Vortex v0.1.

Keywords, types and literals follow docs/specification/lexical-structure.md
section 2.4. Update this file when that list changes.
"""

from pygments.lexer import RegexLexer, bygroups, words
from pygments.token import (
    Comment,
    Keyword,
    Name,
    Number,
    Operator,
    Punctuation,
    String,
    Text,
    Whitespace,
)

__all__ = ["VortexLexer"]


class VortexLexer(RegexLexer):
    name = "Vortex"
    aliases = ["vortex", "vx"]
    filenames = ["*.vx"]

    tokens = {
        "root": [
            (r"\s+", Whitespace),
            (r"//.*?$", Comment.Single),
            # Declarations: highlight the name that follows the keyword.
            (r"(fn)(\s+)([A-Za-z_][A-Za-z0-9_]*)",
             bygroups(Keyword.Declaration, Whitespace, Name.Function)),
            (r"(struct)(\s+)([A-Za-z_][A-Za-z0-9_]*)",
             bygroups(Keyword.Declaration, Whitespace, Name.Class)),
            (words(("let", "mut"), suffix=r"\b"), Keyword.Declaration),
            (words(("return", "if", "else", "while", "for", "in", "break",
                    "continue"), suffix=r"\b"), Keyword),
            (words(("void", "bool", "char", "i32", "u32", "usize", "f32",
                    "f64", "String"), suffix=r"\b"), Keyword.Type),
            (words(("true", "false"), suffix=r"\b"), Keyword.Constant),
            # Numbers: binary, decimal float (digits on both sides of '.',
            # optional exponent), decimal integer.
            (r"0b[01]+\b", Number.Bin),
            (r"[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?", Number.Float),
            (r"[0-9]+[eE][+-]?[0-9]+", Number.Float),
            (r"[0-9]+", Number.Integer),
            (r"'(\\.|[^\\'])'", String.Char),
            (r'"', String.Double, "string"),
            (r"[A-Za-z_][A-Za-z0-9_]*", Name),
            (r"\.\.=|\.\.|->|&&|\|\||<<|>>|[=!<>]=|[-+*/%&|^~!<>=]", Operator),
            (r"[()\[\]{},;:.]", Punctuation),
            (r".", Text),
        ],
        "string": [
            (r"\\.", String.Escape),
            (r'[^\\"]+', String.Double),
            (r'"', String.Double, "#pop"),
        ],
    }


if __name__ == "__main__":
    # Self-check: every token of a representative program is recognised
    # (no Error tokens) and key words get the expected token types.
    from pygments.token import Error

    sample = (
        'struct Point { x: f32, y: f32, }\n'
        'fn main() -> void {\n'
        '    let mut total: i32 = 0b101 + 2; // comment\n'
        '    let c = \'\\n\'; let s = "a\\tb";\n'
        '    for i in 0..=4 { total += i; }\n'
        '    let ok = total >= 3 && !false;\n'
        '    let f = 6.02E+23 * 1.5;\n'
        '}\n'
    )
    tokens = list(VortexLexer().get_tokens(sample))
    assert not [t for t in tokens if t[0] is Error], "unexpected Error token"
    kinds = {value: kind for kind, value in tokens}
    assert kinds["main"] is Name.Function
    assert kinds["Point"] is Name.Class
    assert kinds["f32"] is Keyword.Type
    assert kinds["0b101"] is Number.Bin
    assert kinds["6.02E+23"] is Number.Float
    assert kinds["..="] is Operator
    print("pygments_vortex self-check passed")
