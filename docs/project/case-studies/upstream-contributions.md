# A5. Upstream LLVM and MLIR contributions

<p class="page-intro">This case study records reviewed, merged changes to LLVM and MLIR in the parts of those projects that Vortex depends on. Unlike the other case studies it measures nothing: its evidence is public pull requests that other engineers reviewed and accepted.</p>

<p class="vx-meta">Status: Not started · Planning size: S for the first patch, then ongoing</p>

## The question

Can I land reviewed changes in LLVM or MLIR, in the areas Vortex uses, and
what did each review teach?

Working **upstream** means contributing to the original project that everyone
shares (here the `llvm-project` repository) instead of changing a private
copy. A **pull request** is a proposed change that the project's reviewers
discuss, ask to revise, and then accept or decline.

When the work is done, this section will hold one sentence of this form:

> ___ merged pull requests in ___, each with a test, are listed below with
> links; ___ more are open and marked as open.

## Why employers care

Merged work in a production compiler is the most verifiable evidence a
compiler engineer can show, because experts have already reviewed it in
public. Some roles require a public history of commits and reviews in a real
compiler, and for some roles centred on LLVM, upstream work is the job
itself.

It is also the one kind of evidence that cannot be inflated by a well-written
page. Every claim here is a link a reviewer can open, with the review thread
attached.

## What to build

The contributions should land where Vortex already works, so that each one
teaches something the other case studies use:

| Area | Why it matters to Vortex | Related work |
| --- | --- | --- |
| MLIR Linalg and Vector dialects | The MLIR lowering path uses them for tiling, fusion and vectorization. | [A7](mlir-lowering-path.md), the [MLIR book](../../mlir/index.md) |
| The AArch64 back end | It is the target of every CPU case study. | [A1](cpu-matmul-ladder.md), [A6](native-aarch64-backend.md), the [back-end book](../../backend/index.md) |
| LLVM IR and the passes a front end relies on | The LLVM IR back end exercises them directly. | [A4](llvm-backend.md), [O1](../../optimize/o1-optimizer-contract.md) |
| Testing tools such as lit and FileCheck | Every Vortex test on the LLVM side uses them. | [A3](differential-testing.md), [E4](../../backend/e4-testing-backends.md) |
| Documentation | Places where Vortex's own work found the docs unclear or out of date. | The books on this site |

Three sources of work, in order of how quickly they lead to a merged change:

1. **Issues labelled "good first issue".** LLVM's contribution guide points
   newcomers to them as a way to learn the code base, and asks you to comment
   on an issue before starting so others know it is taken.[^contributing]
2. **Bugs that Vortex work finds in LLVM or MLIR.** A crash or a
   miscompile met in [A4](llvm-backend.md) or [A7](mlir-lowering-path.md),
   reduced to a small test and reported, is a contribution even before it is
   fixed.
3. **Office hours and sync-ups.** Experienced contributors hold regular
   office hours for anyone who wants guidance, and area groups hold regular
   online meetings.[^getting-involved] They are the fastest way to find out
   which small changes a component's maintainers would welcome.

### How a change gets in

LLVM's own guides set out the process. In short:

- Build LLVM from source and reproduce the problem on the current main
  branch before changing anything.[^contributing]
- Every patch carries a test. The developer policy requires test cases for
  bug fixes and new features, and prefers functional tests written with
  FileCheck.[^dev-policy]
- Keep each pull request to one isolated change, formatted with the project's
  clang-format tools, with no unrelated edits.[^contributing]
- Ask for review from the maintainers listed in the `Maintainers.md` file of
  the area you changed. A new contributor who cannot yet request reviewers
  directly can mention them by name in a comment.[^contributing]
- If a week passes with no comments, ping the pull request; once a week is the
  accepted rate.[^contributing]
- A change is approved when a reviewer accepts it, usually with the message
  "LGTM" ("looks good to me").[^code-review]
- After three or more merged pull requests, a contributor may ask for commit
  access (the developer policy as read on 2026-09-23).[^dev-policy]

### Using AI tools upstream

This site's [authorship page](../../authorship.md) says that language models
helped write these docs. Upstream work follows LLVM's rules, not this site's.
LLVM's AI tool use policy, as read on 2026-09-23, says:[^ai-policy]

- a human must stay in the loop: the contributor reads and reviews all
  generated code or text before asking anyone else to review it, is its
  author, and must be able to answer questions about it during review;
- contributions containing a substantial amount of tool-generated content
  are labelled, for example with an `Assisted-by:` trailer in the commit
  message;
- AI tools must not be used to fix issues labelled "good first issue", which
  exist as learning opportunities for new contributors.

Chapters: [E4](../../backend/e4-testing-backends.md) teaches lit and
FileCheck, which almost every LLVM patch touches; the rest of the
[back-end book](../../backend/index.md)'s part E reads LLVM's code generator;
[M1](../../mlir/m1-why-mlir.md) and the [MLIR book](../../mlir/index.md)
explain the dialects; O10 in the [optimization book](../../optimize/index.md)
explains LLVM's pass manager.

This study leaves out:

- changes to private forks or patches that were never reviewed;
- comments and reactions on other people's pull requests, which are listed
  separately below as community activity, not as contributions.

## Method

Nothing on this page is measured, so the method is a set of rules for the
ledger.

- **List merged pull requests, and open ones marked as open.** A pull request
  counts as a contribution only when it is merged.
- **Keep reverts visible.** A merged change that was later reverted stays in
  the table, marked reverted, with a link to the revert.
- **Count what was declined.** The number of pull requests closed without
  merging is given in one line below the table. Hiding them would make the
  table look better than the record.
- **Say what each change was.** One line per pull request describing the
  change, so that its size is visible without opening it.
- **Say what review taught.** One line per merged pull request, in your own
  words.
- **Credit honestly.** If a change was co-authored or finished by someone
  else, the row says which part was yours.

### What counts as success

1. At least one merged pull request, with a test, in one of the areas in the
   table above.
2. Every row links to the pull request and its review thread.
3. Every merged row says what review taught.
4. Later: enough merged pull requests to request commit access.

## Results

### Pull requests

| # | Area | What changed | Pull request | Status | Merged on | Test added | What review taught |
| --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |

Status
: Merged, Open (with the date of the last activity), or Reverted (with a link
  to the revert).

Test added
: The path of the test the pull request added or changed, or "none" with the
  reason (for example, a documentation fix).

Closed without merging: ___

### Issues reported

| Issue | Found through | Reduced test in the report? | Status |
| --- | --- | --- | --- |
|  |  |  |  |

Found through
: The Vortex work that exposed the problem, such as an A3 campaign or the A4
  lowering.

### Community activity

| Date | Where | What | Link |
| --- | --- | --- | --- |
|  |  |  |  |

Community activity covers reviews given on other people's pull requests,
office hours attended, and forum threads. It supports the ledger above; it is
not counted as a contribution.

## Analysis

Empty until the first merged pull request. This section will collect what the
reviews taught across pull requests: the recurring requests, the conventions
learned, and how the next patch changed because of them.

## What did not work

Empty until work begins. This section will record pull requests that stalled
or were declined, and why.

## Threats to validity

**Only successes shown.** A table of merged work alone overstates the record.
The count of closed pull requests and the visible reverts correct for that.

**Size inflation.** Ten one-line changes can look like more than one
substantial fix. The "What changed" column makes size visible, and splitting a
change into several pull requests to raise the count defeats the purpose.
LLVM's guide asks for independent changes in separate pull
requests;[^contributing] dependent pieces of one change do not qualify.

**Credit.** Shared work must say which part was yours.

**Tool use.** A contribution that breaks LLVM's AI tool policy damages the
trust this page is meant to show. The policy is followed as written,
including its labelling rule and its ban on AI tools for good first
issues.[^ai-policy]

**Links that change.** Pull request links are stable, but branch names are
not. The table links pull requests and merged commits, never branches.

## Reproduce

Nothing to reproduce locally: every row links to a public pull request, its
review thread and, once merged, its commit in `llvm-project`.

## What a reviewer should look at

| Evidence | Where it will be | Available |
| --- | --- | --- |
| Each pull request and its full review thread | The links in the table above | Not yet |
| The merged commit on `llvm-project`'s main branch | Linked from each pull request | Not yet |
| The test each pull request added | Linked in the table | Not yet |
| Any revert and the reason for it | Linked in the table | Not yet |
| Issues reported from Vortex work, with their reduced tests | The issues table | Not yet |

## Sources

[^contributing]: LLVM Project, "Contributing to LLVM". <https://llvm.org/docs/Contributing.html>
[^getting-involved]: LLVM Project, "Getting Involved", sections on online sync-ups and office hours. <https://llvm.org/docs/GettingInvolved.html>
[^dev-policy]: LLVM Project, "LLVM Developer Policy", sections "Test Cases" and "Obtaining Commit Access". <https://llvm.org/docs/DeveloperPolicy.html>
[^code-review]: LLVM Project, "LLVM Code-Review Policy and Practices". <https://llvm.org/docs/CodeReview.html>
[^ai-policy]: LLVM Project, "LLVM AI Tool Use Policy". <https://llvm.org/docs/AIToolPolicy.html>
