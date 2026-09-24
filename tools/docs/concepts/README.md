# Concepts: the docs memory system

`concepts.yml` is the single source of truth for every concept the docs teach.
`tools/docs/concepts.py` turns it into snippet files under `includes/`:

| Output | What it is |
| --- | --- |
| `includes/abbreviations.md` | Hover tooltips, appended to every page by `pymdownx.snippets` `auto_append` and rendered by `abbr`. |
| `includes/remember/<page-key>.md` | "Before you start, remember": 3 to 5 prerequisite questions with hidden answers. |
| `includes/next/<page-key>.md` | "You will use this again in": up to 6 later pages that use this page's concepts. |
| `includes/review/<group>.md` | Mixed recall questions for a group of pages, plus spaced questions from earlier groups. |
| `includes/concept-map.mmd`, `includes/concept-map-<book>.mmd` | Mermaid flowcharts of the `requires` edges (whole site, and per book: `tour`, `spec`, `guide`). |

`page-key` is the page path under `docs/` without `.md`, with `/` replaced by
`__` (`compiler/guide/stage-2-lexer.md` becomes `compiler__guide__stage-2-lexer`).
Every nav page gets a remember and a next file, empty when there is nothing to
show, so a page can include both without checking.

## Running it

```sh
python tools/docs/concepts.py              # validate concepts.yml and write includes/
python tools/docs/concepts.py --check      # CI: exit 1 if includes/ is stale, 2 if the data is invalid
python tools/docs/concepts.py --scan       # suggest used_in lists from the docs text (writes nothing)
python tools/docs/concepts.py --self-test  # link and tooltip-filter checks
```

It needs Python 3.12+, PyYAML and Markdown (both come with the docs toolchain).
Output is deterministic: the same inputs give byte-identical files.

## Using the outputs in a page

Snippet paths are relative to the repository root. Put the includes at column
0, the remember box under the page title and the next box at the end:

````markdown
# 3. The parser and the syntax tree

--8<-- "includes/remember/compiler__guide__stage-3-parser-and-tree.md"

...page...

--8<-- "includes/next/compiler__guide__stage-3-parser-and-tree.md"
````

Links inside the remember and next files are relative to the page named by
the file's key, so include each file only from that page. Links inside a
review bank are relative to the directory of its group's pages, so include it
from a page in that directory (for example `docs/compiler/guide/review.md`).
A concept map goes inside a Mermaid fence:

````markdown
```mermaid
--8<-- "includes/concept-map-guide.mmd"
```
````

## Concept fields

| Field | Required | Meaning |
| --- | --- | --- |
| `id` | yes | Stable kebab-case key. Other concepts refer to it in `requires`; never rename it casually. |
| `term` | yes | Display name, lower case unless it is a proper name (`Pratt parsing`, `UTF-8`). |
| `abbr` | no | Acronym or abbreviation, such as `AST`, `IR`, `SSA`. Setting it turns the tooltip on. |
| `aliases` | no | Other spellings (`maximal munch`, `lexeme`). Used for tooltips (multi-word ones only) and by `--scan`, which already matches plurals, so do not list them. |
| `tooltip` | no | `true` to mark every occurrence of a multi-word term site-wide. See the tooltip rule below. |
| `short` | yes | At most 110 characters, plain text with no Markdown. It is the tooltip text. |
| `definition` | yes | One to three plain sentences, consistent with the page that teaches the concept. |
| `spec` | no | The specification glossary's definition, copied word for word. Never reword it. |
| `spec_ref` | no | Docs path, optionally with `#anchor`, of the normative rule, such as `specification/lexical-structure.md#23-identifiers`. |
| `introduced_in` | yes | Docs path of the teaching page that first explains the concept (see below). |
| `requires` | no | Concept ids a reader needs first. Each must be introduced on the same page or earlier in the nav. |
| `used_in` | no | Later nav pages that rely on the concept. Seeded by `--scan`, then curated by hand. |
| `recall_question` | yes | A short question whose answer is the concept. Ends with `?`; no double quotes. |
| `recall_answer` | yes | At most two sentences, naming the concept first. No new claims beyond `definition` and the teaching page. |

### Where a concept is introduced

`introduced_in` names the teaching page, not the first mention:

1. A compiler concept is introduced by the guide stage whose "Words for this
   stage" list defines it (or the stage named after it, such as the lexer).
2. A language concept is introduced by the tour chapter that teaches it, when
   the recall question can be answered from that chapter. The tour comes first
   in the nav, so the guide's remember boxes point back to it.
3. Otherwise it is the first page that defines it: the guide overview for
   `compiler` or `machine code`, the specification for spec-only vocabulary such
   as `normative`, and the word list for terms only it defines.

The specification is the authority on wording; `spec_ref` links to it.

### The tooltip rule

`abbr` marks every exact, case-sensitive occurrence on every page, so tooltips
are restricted:

- A concept with an `abbr` has tooltips by default (set `tooltip: false` to
  stop that).
- A multi-word term gets tooltips only with `tooltip: true`. Opt in for
  jargon a newcomer cannot decode from the words (`basic block`,
  `recursive descent`, `source span`), and leave out phrases a reader already
  understands (`test case`, `local variable`) or that appear so often that
  underlining every one would clutter the page (`type checking`,
  `name resolution`, `constant evaluation`).
- A single word never gets a tooltip. Validation rejects `tooltip: true` on
  one.

Marked text is exactly the term, its multi-word aliases and the `abbr`, as
written. Plurals and capitalised forms are not marked: each extra form slows
the render of every page, and together they matched only 6% of occurrences.
Code spans and code blocks are never marked.

## Review groups

`review_groups` lists the banks to build. Each group has a `name` (the output
file name), a `title`, and its `pages`, which must share one directory. A bank
holds every concept introduced in its pages, plus a few from earlier groups
that its pages rely on most (at least five, or a third of the group's own
count), interleaved in a fixed pseudo-random order.

## Validation

The generator refuses to write anything, and exits 2, when:

- a field is unknown or missing, an `id` repeats or is not kebab-case;
- `short` is too long or not plain, or a question or answer breaks the rules above;
- a page is not in the `mkdocs.yml` nav, or a `spec_ref` anchor is not a heading on that page;
- a `requires` entry is unknown, forms a cycle, or is introduced later than the concept;
- a `used_in` page does not come after `introduced_in`;
- two concepts would mark the same tooltip text;
- a `spec` field no longer matches the glossary, or a glossary term is quoted by no concept or by more than one.

The last rule keeps this file in step with `docs/specification/glossary.md`:
when the glossary changes, copy the new wording into the matching `spec`
field, or add a concept for a new term.
