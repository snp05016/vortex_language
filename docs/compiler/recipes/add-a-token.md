# How to add a token

<p class="page-intro">Use this recipe whenever the language needs a new
keyword, operator, punctuation mark or literal form that your lexer does not
yet recognize: for example, giving a reserved word its v0.1 meaning, or adding
a new operator spelling. It walks through the lexer change and the two
specification pages that must change with it, in the order that keeps them
from drifting apart.</p>

## Before you start

This recipe assumes:

- Your lexer already exists and passes its current tests
  ([2. The lexer](../guide/stage-2-lexer.md)). You are extending a working
  token stream, not building the first one.
- Your driver already has a `--tokens` option that prints the token stream
  ([2. The lexer](../guide/stage-2-lexer.md#the-kinds-of-token-vortex-has),
  [decision 20](../../decisions/program.md#d20)). The steps below assume you
  can run it on a small file and read the result.
- If the new token also changes what the parser accepts, read
  [3. The parser and the syntax tree](../guide/stage-3-parser-and-tree.md)
  first; step 5 below touches the same grammar chapter that stage leans on.

Read [Lexical structure](../../specification/lexical-structure.md) and the
[grammar reference](../../specification/grammar.md) before changing either.
Both are normative: an implementation that disagrees with them is wrong, not
the other way around
([document authority](../../specification/conformance.md#11-document-authority)).

## Steps

1. **Decide whether this needs a decision record first.** Not every new
   spelling is purely mechanical. Check
   [Language decisions](../../decisions/index.md), in particular the
   [lexical structure](../../decisions/lexical.md) records, before you touch
   any code:

     - If the spelling is already fully settled there, skip ahead to step 2.
       A reserved word getting its v0.1 meaning, for instance, is exactly the
       case [decision 29](../../decisions/lexical.md#d29) anticipated: the
       word is already off limits, and giving it a meaning is an
       implementation task, not a new language question.
     - If the spelling raises a question no record answers, such as which
       token kind it should get when more than one would conform (the way
       [decision 18](../../decisions/lexical.md#d18) let `true` and `false`
       be either a keyword or a boolean literal), or how it interacts with
       the longest-match rule against an existing operator, write the record
       first, following the shape of the existing ones in
       `decisions/lexical.md`: question, what the documents said before, the
       options, what other languages do, the decision, and what it changes.
       A specification edit that has no record behind it is a rule with no
       stated reason, and the next reader has to reconstruct your thinking
       from the diff.
     - If a new record replaces an old one instead of adding to it, say so
       and link forward from the old record, the way
       [decision index](../../decisions/index.md) describes.

    Only once the question is settled, in a merged decision record, move on.

2. **Give the spelling a token kind.** Decide which **token kind**
   ([2. The lexer](../guide/stage-2-lexer.md#words-for-this-stage)) the new
   spelling belongs to:

     - A new word that behaves like an existing keyword (fixed spelling,
       cannot be a name) is an addition to the keyword kind, not a new kind.
     - A new symbol that behaves like an existing operator or punctuation
       mark is an addition to that table, in the same way `..=` sits beside
       `..` and `.` in
       [Operators and punctuation](../../specification/lexical-structure.md#26-operators-and-punctuation).
     - A genuinely new shape of value, such as a literal form v0.1 does not
       have yet, needs its own kind, named the way the
       [token kind table](../guide/stage-2-lexer.md#the-kinds-of-token-vortex-has)
       names the existing ones: a plain noun phrase, singular, matching the
       specification's section heading for it.

    Write the name down before step 3. Everything after this point, the
    lexer's internal enum, the token dump, the grammar's terminal, and the
    specification prose, should use that same name so a reader can search for
    one string and find every place the token appears.

3. **Teach the lexer.** Say what changes, not how to write it:

     - **Where recognition happens.** A keyword-shaped spelling is recognized
       by the identifier path: read the whole word, then compare it, by
       exact spelling, against the keyword and reserved-word lists
       ([Keywords and identifiers](../guide/stage-2-lexer.md#keywords-and-identifiers)).
       An operator or punctuation spelling is recognized by the longest-match
       path
       ([The longest match](../guide/stage-2-lexer.md#the-longest-match)). A
       new literal shape is recognized by its own rule, most often triggered
       by the same first character as an existing literal, the way a
       number's run
       ([How far a number extends](../../specification/lexical-structure.md#how-far-a-number-extends))
       already claims every digit-led run before checking it.
     - **Where it must not collide.** If the new spelling shares a prefix
       with an existing operator or punctuation mark, place it so the
       longest valid token still wins, exactly as `..=` must be tried before
       `..`, which must be tried before `.`. If the new spelling is a word,
       add it to exactly one list: keywords, reserved words, or neither; a
       spelling on two lists is a bug this recipe cannot catch for you.
     - **What every token still owes its reader.** The token keeps its exact
       spelling and its source span, never only its decoded meaning
       ([Token source data](../../specification/lexical-structure.md#28-token-source-data)).
       If the new token is a literal, decide, in the same step, what its
       decoded value looks like and where that decoding happens; do not let
       it silently become a job for the parser.
     - **What it must not do.** The lexer only classifies characters. It must
       not ask whether the token is well placed, whether a name it resembles
       is declared, or whether a number fits a type
       ([architecture, pass contracts](../architecture.md#pass-contracts)). A
       token change that reaches into name resolution or type checking
       belongs to a different recipe.

4. **Write the golden token-dump tests.** A **golden token-dump test** pairs
   one small source file with the exact `--tokens` output your lexer must
   print for it: the token dump is the "golden", or accepted-correct, copy
   that later runs are checked against. Add, at minimum:

     - One test where the new spelling appears exactly once, checked against
       the full line your printout format uses for it (kind, spelling and
       position, in whatever order your format from
       [implementation choice I4](../../decisions/implementation.md#i4)
       settled on).
     - One test with a spelling one character shorter or longer than the new
       one, so a future change that breaks the boundary fails a test instead
       of passing silently. `stage-2-lexer.md` does exactly this for `..`
       beside `..=`
       ([The longest match](../guide/stage-2-lexer.md#the-longest-match)); do
       the same for your token.
     - If the new spelling can be written badly, one test for each way it
       fails, each expecting the right entry from
       [Lexical errors](../../specification/lexical-structure.md#27-lexical-errors),
       next to a nearby valid case, the pairing
       [decision 19](../../decisions/lexical.md#d19) and the
       [diagnostics chapter](../../specification/diagnostics.md#107-verification-requirements)
       both ask for.
     - One run over a file that also contains every other token kind, to
       confirm the new one does not change how anything around it is read.

    Keep the printout format itself unchanged. A token-dump recipe that also
    redesigns the format turns every existing golden file into a failing test
    for the wrong reason.

5. **Extend the grammar and its railroad diagram.** If the new token can
   appear in a program, not only inside the lexer's own tests, it needs a
   grammar production, or a change to an existing one, in
   [the grammar reference](../../specification/grammar.md):

     - Add the token's spelling, in quotes, to the EBNF production where it
       belongs, following
       [How to read the grammar](../../specification/grammar.md#how-to-read-the-grammar).
       A new keyword usually extends an existing alternative (another line in
       a `|` list); a new literal form usually needs a new production named
       after the token kind you chose in step 2.
     - If the production is new, give it a `<span id="rule-<name>"></span>`
       anchor next to the others at the top of its section, using the same
       `rule-<production_name>` pattern the existing anchors use (see
       [Literals](../../specification/grammar.md#literals) for nine of them
       declared together).
     - Update the section's **Allowed** and **Not allowed** example lists so
       the new spelling appears in the one it belongs to, with a correct
       label comment ([decision 28](../../decisions/documentation.md#d28)); a
       `fragment` label for a bare spelling, never a `valid` label unless the
       block is a complete, compiling program.
     - Add the include line for the new or changed production's diagram, in
       the same place the others sit under the `=== "Diagram"` tab:
       `--8<-- "includes/railroad/<production_name>.md"`. Do not draw the
       diagram by hand. The **railroad diagram**, the boxes-and-tracks
       picture of a production, is generated from the grammar text by
       `tools/docs/railroad.py`; writing the include line is your whole job
       here, and the coordinator's build regenerates the actual SVG from what
       you wrote in the EBNF block.

    Grammar changes belong with the token change because a token nothing in
    the grammar can produce is dead: the lexer would recognize it, and every
    later stage would never see it.

6. **Update the specification text.** Add the rule itself to
   [Lexical structure](../../specification/lexical-structure.md), in the
   numbered section the token kind belongs to: 2.3 for identifier-shaped
   rules, 2.4 for keywords and reserved words, 2.5 for literals, 2.6 for
   operators and punctuation, and a new bullet in 2.7 only if the token can
   fail to lex. State the rule as a plain, testable sentence, the way the
   existing rules read, and cite the decision record from step 1 the way the
   chapter already cites [decision 15](../../decisions/lexical.md#d15),
   [17](../../decisions/lexical.md#d17) and
   [29](../../decisions/lexical.md#d29) next to the rules they settled.

    If the repository's rule identifiers are already in place on the page you
    are editing (each normative rule starting with
    `<a class="vx-rule" id="lex.<topic>.<rule>">`), give your new rule its own
    id under the `lex` prefix: lower case, dotted, short, and not reused from
    any other rule anywhere in the specification. Add the id only to the
    sentence that states the requirement; do not add one to an example, a
    note, or the section heading, and do not change the wording of any rule
    you are not adding.

## Check that it worked

1. Run your driver's `--tokens` option on the file from each golden test in
   step 4 and diff the output against the expected file. Every kind,
   spelling and position must match, including the boundary cases.
2. Run your whole lexer test suite. A change that is only additive should not
   move a single existing token's kind, spelling or span.
3. Read the rendered grammar page once the site rebuilds. The new production
   text should appear under **Grammar**, and a diagram, generated from it,
   should appear under **Diagram** in the same section, with no manual
   drawing left over.
4. Re-read the new sentence in Lexical structure next to the decision record
   it cites. The two should say the same thing in two different registers:
   the specification states the rule, the record explains why it is that
   rule and not another.

## Related

- [2. The lexer](../guide/stage-2-lexer.md)
- [3. The parser and the syntax tree](../guide/stage-3-parser-and-tree.md)
- [Lexical structure](../../specification/lexical-structure.md)
- [Grammar reference](../../specification/grammar.md)
- [Language decisions](../../decisions/index.md)
- [How to add a diagnostic](add-a-diagnostic.md)
- [How to add an AST node](add-an-ast-node.md)
