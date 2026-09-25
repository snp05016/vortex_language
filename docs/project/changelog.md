# What is new in the docs

<p class="page-intro">Changes to this site, newest first, so a returning reader can see what is new since the last visit. Each entry links to the commit that made it once that commit is on GitHub.</p>

## 2026-09-24

- **Four books, finished.** [Back end](../backend/index.md) (A1 to E4),
  [Optimize](../optimize/index.md) (O1 to O12 and P1 to P16, plus
  [the CPU matmul ladder](../optimize/ladder.md)), [GPU](../gpu/index.md)
  (G1 to G15, with [the GPU matmul ladder](../gpu/g10-matmul-ladder.md) in
  G10) and [MLIR](../mlir/index.md) (M1 to M12): every chapter is now
  written
  ([eaf0c0b](https://github.com/snp05016/vortex_language/commit/eaf0c0bf9c67c04bfa0cd399cfa4a6584a1efd5e),
  [1982184](https://github.com/snp05016/vortex_language/commit/19821844f5710c91a92c514f4be152972359b123)).
- **Build v0.1, finished.** Every stage now has a "Before you start,
  remember" box, goals, check questions, C++ examples that follow the cited
  documentation, a "Key ideas" recap and a "You will use this again in" box.
  Four review pages cover stages 0 to 11, five
  [recipes](../compiler/recipes/add-a-token.md) walk through common changes,
  and a [workbook](../compiler/workbook.md) tracks every stage, with ticks
  kept in the reader's browser only
  ([2f8caff](https://github.com/snp05016/vortex_language/commit/2f8caff2074c5736533d416e4ad2d52c97ed3603),
  [eaf0c0b](https://github.com/snp05016/vortex_language/commit/eaf0c0bf9c67c04bfa0cd399cfa4a6584a1efd5e)).
- **Language tour, finished.** The same remember, next and check-question
  boxes and a "Key ideas" recap in every chapter, plus three review pages
  covering chapters 1 to 10
  ([a5ee191](https://github.com/snp05016/vortex_language/commit/a5ee1911aac48b4d1f124e84cbfee814a31f553f),
  [eaf0c0b](https://github.com/snp05016/vortex_language/commit/eaf0c0bf9c67c04bfa0cd399cfa4a6584a1efd5e)).
- **Specification: rule identifiers.** Every normative rule now carries a
  stable identifier, such as `arrays.dims.const`: 382 in total, one on each
  rule, explained on the
  [specification's front page](../specification/index.md#rule-identifiers)
  ([eaf0c0b](https://github.com/snp05016/vortex_language/commit/eaf0c0bf9c67c04bfa0cd399cfa4a6584a1efd5e)).
- **Case studies reviewed.** The ten [case studies](case-studies/index.md)
  were checked against the finished books; their status has not changed,
  since none has started and none has results
  ([eaf0c0b](https://github.com/snp05016/vortex_language/commit/eaf0c0bf9c67c04bfa0cd399cfa4a6584a1efd5e)).
- **Two new pages.** [Your progress](../progress.md) records which
  chapters you have opened and says when a review page is due, and the
  [concept map](../concept-map.md) draws which ideas each idea needs first,
  one tab per book, with a smaller map at the end of every page that
  introduces a concept. The progress record stays in your browser, with no
  account and nothing sent anywhere.
- **Language decisions.** The v0.1 documents had left 56 questions open or
  answered them in two different ways. Each is now settled by a
  **decision record**: the question, what the documents said, what other
  languages do, the decision and what it changed
  ([6d16a6a](https://github.com/snp05016/vortex_language/commit/6d16a6a827ca1309caca6c36671857db98edbf33)).
  The decisions are applied to the specification, the language tour and the
  compiler guide. A separate page lists the
  [implementation choices](../decisions/implementation.md) that the compiler
  guide leaves to the implementer. Start at
  [Language decisions](../decisions/index.md).
- **Four books, in progress.** [Optimize](../optimize/index.md),
  [Back end](../backend/index.md), [GPU](../gpu/index.md) and
  [MLIR](../mlir/index.md) teach the ideas behind the work planned after v0.1.
  Each book has an overview page and a page for every planned chapter. A
  chapter shows a placeholder until it is written, and the overview gives
  every chapter's status.
- **New tabs.** The top tabs are now Home, Learn, Reference, Build v0.1, the
  four books and Project. Project is new: it holds
  [Vortex for reviewers](index.md), [How Vortex is tested](testing.md),
  [How Vortex performance is measured](measuring.md),
  [Case studies](case-studies/index.md) (none started yet) and this page. The
  roadmap, the compiler internals pages and the authorship statement moved
  into it; their web addresses did not change.
- **Zensical.** The site is now built with Zensical instead of MkDocs. Both
  are **static site generators**, programs that turn these Markdown files into
  web pages.[^zensical] The build installs exactly Zensical 0.0.64, because
  Zensical has not reached version 1.0 and may still change
  ([6d16a6a](https://github.com/snp05016/vortex_language/commit/6d16a6a827ca1309caca6c36671857db98edbf33)).
- **Checked examples.** A new way to show code: each example is a file in
  `examples/`, and a page includes the file itself, so the code a reader sees
  is the code that was checked
  ([6d16a6a](https://github.com/snp05016/vortex_language/commit/6d16a6a827ca1309caca6c36671857db98edbf33)).
  A **continuous integration (CI)** workflow, a job GitHub runs whenever the
  examples change, checks every example on macOS (Apple silicon) and on Linux
  (Arm and x86-64), as [How Vortex is tested](testing.md#what-is-in-place-today)
  describes:
    - C++ examples are compiled and usually run, and any expected output is
      compared byte for byte;
    - assembly examples are assembled;
    - LLVM IR and MLIR examples (programs in the intermediate forms of the LLVM
      and MLIR compiler frameworks) go through the LLVM 18 tools where the
      machine has them;
    - an example that targets another platform, or needs a tool the machine
      lacks, is skipped with the reason printed. CUDA kernels need NVIDIA's
      compiler, and Metal kernels are always skipped for now.
- **Railroad diagrams.** A **railroad diagram** draws a grammar rule as
  tracks: every path from the left end to the right end spells one valid form
  of the rule. The [formal grammar](../specification/grammar.md) shows one for
  each of its rules. A script draws them from the grammar with the
  railroad-diagrams package,[^railroad] and the publishing workflow stops if a
  diagram no longer matches the grammar
  ([6d16a6a](https://github.com/snp05016/vortex_language/commit/6d16a6a827ca1309caca6c36671857db98edbf33)).
- **Definitions on hover.** Key terms show a short definition when the pointer
  rests on them. The definitions come from one file,
  `tools/docs/concepts/concepts.yml`, so a term reads the same on every page
  ([6d16a6a](https://github.com/snp05016/vortex_language/commit/6d16a6a827ca1309caca6c36671857db98edbf33)).
- **Docs checks in CI.** Every pull request or change to `main` that touches
  the docs is checked for prose style (Vale), spelling (codespell) and broken
  internal links and anchors (lychee). External links are checked once a week
  ([6d16a6a](https://github.com/snp05016/vortex_language/commit/6d16a6a827ca1309caca6c36671857db98edbf33)).

## 2026-09-23

- **Compiler guide.** An [overview](../compiler/guide/index.md), one stage page
  per roadmap milestone (stages 0 to 11), a [word list](../compiler/guide/words.md)
  and a [reading list](../compiler/guide/reading-list.md), plus readability
  edits to the tour and the specification
  ([dbc15d8](https://github.com/snp05016/vortex_language/commit/dbc15d8a76c36d5ad96426a6ec0caf36dc550a70)).

## 2026-09-22

- **Reference readability.** Stylesheet changes for the readability of the
  reference pages
  ([dcad969](https://github.com/snp05016/vortex_language/commit/dcad9690639d2d5d22f917bd772cce75624f6aa8)).
- **Monochrome design and authorship.** A monochrome design, the Fira Code
  font for code, and the [authorship and methodology](../authorship.md)
  statement
  ([ebb6049](https://github.com/snp05016/vortex_language/commit/ebb604938d4c1ae13c05b04f0534d08af846b7be)).
- **Specification chapters.** Nine [specification](../specification/index.md)
  chapters: declarations, types and values, expressions, statements, arrays,
  structs, references, diagnostics, and the glossary
  ([4fdf0f3](https://github.com/snp05016/vortex_language/commit/4fdf0f351a86c116b06f384bbc914f357835bc4b)).
- **Website.** The documentation became a website, built with MkDocs, with a
  workflow that publishes it to GitHub Pages
  ([cb8f5cd](https://github.com/snp05016/vortex_language/commit/cb8f5cddfe6411b24420a3e716807b9a58d0e8e9)).
- **New pages.** The [home page](../index.md), the
  [specification status](../specification/index.md),
  [conformance](../specification/conformance.md),
  [lexical structure](../specification/lexical-structure.md), the
  [compiler architecture](../compiler/architecture.md), the
  [parser design](../compiler/parser-design.md) and the
  [syntax-tree (AST) learning guide](../compiler/ast-guide.md). The grammar,
  the cheat sheet, the language tour, the philosophy and the roadmap grew
  ([cb8f5cd](https://github.com/snp05016/vortex_language/commit/cb8f5cddfe6411b24420a3e716807b9a58d0e8e9)).

## 2026-09-21

- **Tour: declarations.** The language tour's chapter on
  [declarations](../language-tour/10-declarations.md)
  ([0ee34a6](https://github.com/snp05016/vortex_language/commit/0ee34a6ce7ce9df6fff810069459c9a4d87432c7)).
- **Cheat sheet.** Additions to the
  [language and compiler cheat sheet](../language-and-compiler-cheatsheet.md)
  ([855c1f7](https://github.com/snp05016/vortex_language/commit/855c1f70ec3bd49108c8fd46cc2a97eb90f0a2d3)).

## 2026-09-19

- **Grammar, roadmap and cheat sheet.** The
  [formal grammar](../specification/grammar.md)
  ([87f6074](https://github.com/snp05016/vortex_language/commit/87f607415806bd1a5874574eab76ba53c63562f4)),
  the v0.1 compiler [roadmap](../roadmap.md)
  ([923392b](https://github.com/snp05016/vortex_language/commit/923392b0aa1217d6d6a094b6fcb3ff23c6047167))
  and the language and compiler cheat sheet
  ([ce7bd92](https://github.com/snp05016/vortex_language/commit/ce7bd928b1ec8d3664898105afa1351b275916d3)).

## 2026-09-18

- **Philosophy and tour.** The [language philosophy](../philosophy.md) and
  nine chapters of the [language tour](../language-tour/README.md)
  ([81c87bd](https://github.com/snp05016/vortex_language/commit/81c87bd93d6654af783cf55ca82b7de5f99d412f)).

## Sources

[^zensical]: Zensical, source repository `zensical/zensical`, GitHub. <https://github.com/zensical/zensical>
[^railroad]: Tab Atkins, railroad-diagrams, source repository `tabatkins/railroad-diagrams`, GitHub. <https://github.com/tabatkins/railroad-diagrams>
