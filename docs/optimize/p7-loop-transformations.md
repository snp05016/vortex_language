# P7. Loop transformations

<p class="page-intro">A loop transformation changes the order in which a loop nest does its work, never the work itself. This chapter builds the classic transformations, shows how to prove each one legal, and holds each to Vortex's promise that a faster matrix multiplication prints the same bits as the naive one.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [P6. Dependence analysis](p6-dependence-analysis.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a dependence's direction vector record, and when may loops be reordered?"

        For two accesses to one location, loop by loop from the outermost: whether the second runs in a later iteration (`<`), the same one (`=`) or an earlier one (`>`). A reordering of the loops is legal when no dependence, rewritten for the new order, has `>` as its first entry that is not `=`.

        Introduced in [P6. Dependence analysis](p6-dependence-analysis.md).

    ??? question "Which loop carries a dependence?"

        The outermost loop whose entry is not `=`. The two accesses run in different iterations of that loop and in the same iteration of every loop outside it. A dependence whose entries are all `=` is carried by no loop: both accesses run in one iteration.

        Introduced in [P6. Dependence analysis](p6-dependence-analysis.md).

    ??? question "In what order are the elements of a `[f32; 64, 64]` array stored?"

        Row after row, the last index varying fastest: `m[i, j + 1]` sits next to `m[i, j]`, and `m[i + 1, j]` is a row, 256 bytes, away.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "May a Vortex compiler regroup the additions in `sum += a[row, k] * b[k, column]`?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered. Addition is not associative, so a new grouping can change the last bits.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

    ??? question "What does passing `&mut c` promise about the other arguments of the same call?"

        That none of them is `c`. A variable lent as `&mut` may appear in no other argument of the call, so inside the callee `c` shares storage with no other parameter.

        Introduced in [References and mutability, decision 25](../decisions/references.md#d25).

!!! goals "In this chapter"

    - Explain what interchange, fusion, fission, unrolling, unroll-and-jam, tiling and skewing change in a loop nest, and what they leave alone.
    - Decide whether a transformation is legal by rewriting every dependence for the new loop order.
    - Recognize which transformations keep Vortex's floating-point results bit for bit.
    - Transform the stage 10 kernel by hand into an ikj nest and a register-blocked nest, justifying each step.

## The same work in a different order

Start with the kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for), at a size where speed begins to matter:

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            let mut sum: f32 = 0.0;
            for k in 0..64 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

The three loops ask for 64 × 64 × 64 = 262,144 multiply-adds, one for each point `(row, column, k)` of a cube. The set of points is the nest's **iteration space**, and the order in which the loops visit them is its **schedule**. A loop order is named by its variables from the outside in, with `i` for `row` and `j` for `column`: the kernel as written is ijk.

A **loop transformation** rewrites a nest to do the same work in a different order or grouping. It changes when each piece of work happens, and so which data is near the processor at that moment: the [philosophy's](../philosophy.md#2-say-what-to-calculate-then-decide-how-to-run-it) second principle made concrete, say what to calculate, then decide how to run it.

Order matters because memory is fetched in cache lines ([P2](p2-memory-hierarchy.md)). Row-major storage ([decision 43](../decisions/arrays.md#d43)) puts the consecutive reads of `b[k, column]` in the innermost `k` loop a row, 256 bytes, apart. The owner's Apple M4 Pro reports a 128-byte line (`sysctl hw.cachelinesize`, checked on 2026-09-24), room for 32 `f32` values: a walk along a row uses all of them, and a walk down a column uses one value per line it fetches.

<figure class="vx-figure">
<svg viewBox="0 0 760 322" role="img" aria-label="Sixteen reads of b in two loop orders" aria-describedby="p7-f1-desc">
<title id="p7-f1-title">Sixteen reads of b in two loop orders</title>
<desc id="p7-f1-desc">Two panels. Each shows a 4 by 4 array b, numbered in the order a loop nest reads it, and the same sixteen elements laid out in memory row after row, with bars marking groups of four that stand for cache lines. In the top panel the column loop is innermost: memory is read in order, one line after another. In the bottom panel the k loop is innermost: consecutive reads are a whole row apart, so reads 1 to 4 land in four different lines. A highlight replays each order in the grid and in memory at the same time.</desc>
<text class="vx-text" x="20" y="22">Column loop innermost (ikj): b[k, column] is read along a row</text>
<rect class="vx-box" x="20" y="36" width="34" height="26"/>
<rect class="vx-box" x="54" y="36" width="34" height="26"/>
<rect class="vx-box" x="88" y="36" width="34" height="26"/>
<rect class="vx-box" x="122" y="36" width="34" height="26"/>
<rect class="vx-box" x="20" y="62" width="34" height="26"/>
<rect class="vx-box" x="54" y="62" width="34" height="26"/>
<rect class="vx-box" x="88" y="62" width="34" height="26"/>
<rect class="vx-box" x="122" y="62" width="34" height="26"/>
<rect class="vx-box" x="20" y="88" width="34" height="26"/>
<rect class="vx-box" x="54" y="88" width="34" height="26"/>
<rect class="vx-box" x="88" y="88" width="34" height="26"/>
<rect class="vx-box" x="122" y="88" width="34" height="26"/>
<rect class="vx-box" x="20" y="114" width="34" height="26"/>
<rect class="vx-box" x="54" y="114" width="34" height="26"/>
<rect class="vx-box" x="88" y="114" width="34" height="26"/>
<rect class="vx-box" x="122" y="114" width="34" height="26"/>
<rect class="vx-box" x="200" y="62" width="34" height="30"/>
<rect class="vx-box" x="234" y="62" width="34" height="30"/>
<rect class="vx-box" x="268" y="62" width="34" height="30"/>
<rect class="vx-box" x="302" y="62" width="34" height="30"/>
<rect class="vx-box" x="336" y="62" width="34" height="30"/>
<rect class="vx-box" x="370" y="62" width="34" height="30"/>
<rect class="vx-box" x="404" y="62" width="34" height="30"/>
<rect class="vx-box" x="438" y="62" width="34" height="30"/>
<rect class="vx-box" x="472" y="62" width="34" height="30"/>
<rect class="vx-box" x="506" y="62" width="34" height="30"/>
<rect class="vx-box" x="540" y="62" width="34" height="30"/>
<rect class="vx-box" x="574" y="62" width="34" height="30"/>
<rect class="vx-box" x="608" y="62" width="34" height="30"/>
<rect class="vx-box" x="642" y="62" width="34" height="30"/>
<rect class="vx-box" x="676" y="62" width="34" height="30"/>
<rect class="vx-box" x="710" y="62" width="34" height="30"/>
<rect class="vx-box-accent" x="20" y="36" width="34" height="26"><animateMotion dur="9.6s" repeatCount="indefinite" calcMode="discrete" values="0,0;34,0;68,0;102,0;0,26;34,26;68,26;102,26;0,52;34,52;68,52;102,52;0,78;34,78;68,78;102,78"/></rect>
<rect class="vx-box-accent" x="200" y="62" width="34" height="30"><animateMotion dur="9.6s" repeatCount="indefinite" calcMode="discrete" values="0,0;34,0;68,0;102,0;136,0;170,0;204,0;238,0;272,0;306,0;340,0;374,0;408,0;442,0;476,0;510,0"/></rect>
<text class="vx-mono" x="37" y="54" text-anchor="middle">1</text>
<text class="vx-mono" x="71" y="54" text-anchor="middle">2</text>
<text class="vx-mono" x="105" y="54" text-anchor="middle">3</text>
<text class="vx-mono" x="139" y="54" text-anchor="middle">4</text>
<text class="vx-mono" x="37" y="80" text-anchor="middle">5</text>
<text class="vx-mono" x="71" y="80" text-anchor="middle">6</text>
<text class="vx-mono" x="105" y="80" text-anchor="middle">7</text>
<text class="vx-mono" x="139" y="80" text-anchor="middle">8</text>
<text class="vx-mono" x="37" y="106" text-anchor="middle">9</text>
<text class="vx-mono" x="71" y="106" text-anchor="middle">10</text>
<text class="vx-mono" x="105" y="106" text-anchor="middle">11</text>
<text class="vx-mono" x="139" y="106" text-anchor="middle">12</text>
<text class="vx-mono" x="37" y="132" text-anchor="middle">13</text>
<text class="vx-mono" x="71" y="132" text-anchor="middle">14</text>
<text class="vx-mono" x="105" y="132" text-anchor="middle">15</text>
<text class="vx-mono" x="139" y="132" text-anchor="middle">16</text>
<text class="vx-mono" x="217" y="82" text-anchor="middle">1</text>
<text class="vx-mono" x="251" y="82" text-anchor="middle">2</text>
<text class="vx-mono" x="285" y="82" text-anchor="middle">3</text>
<text class="vx-mono" x="319" y="82" text-anchor="middle">4</text>
<text class="vx-mono" x="353" y="82" text-anchor="middle">5</text>
<text class="vx-mono" x="387" y="82" text-anchor="middle">6</text>
<text class="vx-mono" x="421" y="82" text-anchor="middle">7</text>
<text class="vx-mono" x="455" y="82" text-anchor="middle">8</text>
<text class="vx-mono" x="489" y="82" text-anchor="middle">9</text>
<text class="vx-mono" x="523" y="82" text-anchor="middle">10</text>
<text class="vx-mono" x="557" y="82" text-anchor="middle">11</text>
<text class="vx-mono" x="591" y="82" text-anchor="middle">12</text>
<text class="vx-mono" x="625" y="82" text-anchor="middle">13</text>
<text class="vx-mono" x="659" y="82" text-anchor="middle">14</text>
<text class="vx-mono" x="693" y="82" text-anchor="middle">15</text>
<text class="vx-mono" x="727" y="82" text-anchor="middle">16</text>
<line class="vx-line" x1="203" y1="100" x2="333" y2="100"/>
<line class="vx-line" x1="339" y1="100" x2="469" y2="100"/>
<line class="vx-line" x1="475" y1="100" x2="605" y2="100"/>
<line class="vx-line" x1="611" y1="100" x2="741" y2="100"/>
<text class="vx-text-muted" x="20" y="152">b, in reading order</text>
<text class="vx-text-muted" x="200" y="118">memory, row after row; each bar marks one cache line (4 values here)</text>
<text class="vx-text" x="20" y="182">k loop innermost (ijk): b[k, column] is read down a column</text>
<rect class="vx-box" x="20" y="196" width="34" height="26"/>
<rect class="vx-box" x="54" y="196" width="34" height="26"/>
<rect class="vx-box" x="88" y="196" width="34" height="26"/>
<rect class="vx-box" x="122" y="196" width="34" height="26"/>
<rect class="vx-box" x="20" y="222" width="34" height="26"/>
<rect class="vx-box" x="54" y="222" width="34" height="26"/>
<rect class="vx-box" x="88" y="222" width="34" height="26"/>
<rect class="vx-box" x="122" y="222" width="34" height="26"/>
<rect class="vx-box" x="20" y="248" width="34" height="26"/>
<rect class="vx-box" x="54" y="248" width="34" height="26"/>
<rect class="vx-box" x="88" y="248" width="34" height="26"/>
<rect class="vx-box" x="122" y="248" width="34" height="26"/>
<rect class="vx-box" x="20" y="274" width="34" height="26"/>
<rect class="vx-box" x="54" y="274" width="34" height="26"/>
<rect class="vx-box" x="88" y="274" width="34" height="26"/>
<rect class="vx-box" x="122" y="274" width="34" height="26"/>
<rect class="vx-box" x="200" y="222" width="34" height="30"/>
<rect class="vx-box" x="234" y="222" width="34" height="30"/>
<rect class="vx-box" x="268" y="222" width="34" height="30"/>
<rect class="vx-box" x="302" y="222" width="34" height="30"/>
<rect class="vx-box" x="336" y="222" width="34" height="30"/>
<rect class="vx-box" x="370" y="222" width="34" height="30"/>
<rect class="vx-box" x="404" y="222" width="34" height="30"/>
<rect class="vx-box" x="438" y="222" width="34" height="30"/>
<rect class="vx-box" x="472" y="222" width="34" height="30"/>
<rect class="vx-box" x="506" y="222" width="34" height="30"/>
<rect class="vx-box" x="540" y="222" width="34" height="30"/>
<rect class="vx-box" x="574" y="222" width="34" height="30"/>
<rect class="vx-box" x="608" y="222" width="34" height="30"/>
<rect class="vx-box" x="642" y="222" width="34" height="30"/>
<rect class="vx-box" x="676" y="222" width="34" height="30"/>
<rect class="vx-box" x="710" y="222" width="34" height="30"/>
<rect class="vx-box-accent" x="20" y="196" width="34" height="26"><animateMotion dur="9.6s" repeatCount="indefinite" calcMode="discrete" values="0,0;0,26;0,52;0,78;34,0;34,26;34,52;34,78;68,0;68,26;68,52;68,78;102,0;102,26;102,52;102,78"/></rect>
<rect class="vx-box-accent" x="200" y="222" width="34" height="30"><animateMotion dur="9.6s" repeatCount="indefinite" calcMode="discrete" values="0,0;136,0;272,0;408,0;34,0;170,0;306,0;442,0;68,0;204,0;340,0;476,0;102,0;238,0;374,0;510,0"/></rect>
<text class="vx-mono" x="37" y="214" text-anchor="middle">1</text>
<text class="vx-mono" x="71" y="214" text-anchor="middle">5</text>
<text class="vx-mono" x="105" y="214" text-anchor="middle">9</text>
<text class="vx-mono" x="139" y="214" text-anchor="middle">13</text>
<text class="vx-mono" x="37" y="240" text-anchor="middle">2</text>
<text class="vx-mono" x="71" y="240" text-anchor="middle">6</text>
<text class="vx-mono" x="105" y="240" text-anchor="middle">10</text>
<text class="vx-mono" x="139" y="240" text-anchor="middle">14</text>
<text class="vx-mono" x="37" y="266" text-anchor="middle">3</text>
<text class="vx-mono" x="71" y="266" text-anchor="middle">7</text>
<text class="vx-mono" x="105" y="266" text-anchor="middle">11</text>
<text class="vx-mono" x="139" y="266" text-anchor="middle">15</text>
<text class="vx-mono" x="37" y="292" text-anchor="middle">4</text>
<text class="vx-mono" x="71" y="292" text-anchor="middle">8</text>
<text class="vx-mono" x="105" y="292" text-anchor="middle">12</text>
<text class="vx-mono" x="139" y="292" text-anchor="middle">16</text>
<text class="vx-mono" x="217" y="242" text-anchor="middle">1</text>
<text class="vx-mono" x="251" y="242" text-anchor="middle">5</text>
<text class="vx-mono" x="285" y="242" text-anchor="middle">9</text>
<text class="vx-mono" x="319" y="242" text-anchor="middle">13</text>
<text class="vx-mono" x="353" y="242" text-anchor="middle">2</text>
<text class="vx-mono" x="387" y="242" text-anchor="middle">6</text>
<text class="vx-mono" x="421" y="242" text-anchor="middle">10</text>
<text class="vx-mono" x="455" y="242" text-anchor="middle">14</text>
<text class="vx-mono" x="489" y="242" text-anchor="middle">3</text>
<text class="vx-mono" x="523" y="242" text-anchor="middle">7</text>
<text class="vx-mono" x="557" y="242" text-anchor="middle">11</text>
<text class="vx-mono" x="591" y="242" text-anchor="middle">15</text>
<text class="vx-mono" x="625" y="242" text-anchor="middle">4</text>
<text class="vx-mono" x="659" y="242" text-anchor="middle">8</text>
<text class="vx-mono" x="693" y="242" text-anchor="middle">12</text>
<text class="vx-mono" x="727" y="242" text-anchor="middle">16</text>
<line class="vx-line" x1="203" y1="260" x2="333" y2="260"/>
<line class="vx-line" x1="339" y1="260" x2="469" y2="260"/>
<line class="vx-line" x1="475" y1="260" x2="605" y2="260"/>
<line class="vx-line" x1="611" y1="260" x2="741" y2="260"/>
<text class="vx-text-muted" x="20" y="312">b, in reading order</text>
<text class="vx-text-muted" x="200" y="278">memory, row after row; each bar marks one cache line (4 values here)</text>
</svg>
<figcaption>Figure 1. Sixteen reads of a 4 × 4 array <code>b</code>, numbered in the grid and in row-major memory. With <code>column</code> innermost, the reads sweep memory line by line; with <code>k</code> innermost, each read lands in a different line. Real lines hold 32 <code>f32</code> values on the M4 Pro, not four.</figcaption>
</figure>

Wolf and Lam name the two ideas at work.[^wl91] **Reuse** belongs to the computation: the same data used by several iterations, as `a[row, k]` is used once per `column`. Reusing one element is **temporal reuse**; reusing one cache line is **spatial reuse**. **Locality** is reuse that the cache turns into a saved trip to memory, because the second use arrives before the data is evicted. A transformation cannot create or destroy reuse. It moves uses closer together or further apart in time, and so decides how much reuse becomes locality.

Ulrich Drepper, multiplying 1000 × 1000 double-precision matrices on a 2666 MHz Intel Core 2, read the second matrix along rows by first copying it transposed, and needed 23.4% of the original cycles, copy included.[^drepper] Simon Boehm swapped the two innermost loops of a 1024 × 1024 `f32` multiplication on an Intel i7-6700 (clang 14, `-O3 -march=native -ffast-math`), and the time fell from 1512 ms, his best in the original order, to 89 ms, partly because the compiler could now vectorize.[^boehm]

Neither result carries over to Vortex unchanged. Boehm's flags let the compiler reassociate additions and fuse each multiply with its add, and his compiled code did use fused multiply-adds; Vortex forbids both. Drepper's copy keeps each element's additions in order, but the blocked version he builds next rests on the claim that their order does not matter, with rounding set aside in a footnote.[^drepper]

## Interchange

Boehm's change is a **loop interchange**: swapping two loops, so that the inner one runs outside. A swap is allowed only if every dependence keeps its direction: each value is still written before it is read and read before it is overwritten, and writes to one element keep their order.

### When a swap is legal

Take a smaller nest:

```vortex
// items: valid
fn shift(g: &mut [i32; 5, 5]) {
    for i in 1..5 {
        for j in 0..4 {
            g[i, j] = g[i - 1, j + 1] + 1;
        }
    }
}
```

Iteration `(i, j)` reads what iteration `(i − 1, j + 1)` wrote, so the reader runs at distance `(1, −1)` from the writer: one row later, one column earlier. Swap the loops and column `j` finishes before column `j + 1` starts: the reader now runs before its writer and reads the old value. In the new order the distance is `(−1, 1)`, whose first nonzero entry is negative. The swap is illegal.

<figure class="vx-figure">
<svg viewBox="0 0 760 350" role="img" aria-label="A dependence that forbids swapping two loops" aria-describedby="p7-f2-desc">
<title id="p7-f2-title">A dependence that forbids swapping two loops</title>
<desc id="p7-f2-desc">Two copies of the 4 by 4 grid of iterations of g[i, j] = g[i - 1, j + 1] + 1, with i from 1 to 4 top to bottom and j from 0 to 3 left to right. Arrows show each value flowing from the iteration that writes it, one row up and one column right, to the iteration that reads it. The numbers give the order of the visits. On the left, rows are outside, and every arrow runs from an earlier visit to a later one: the distance (1, -1) is legal. On the right, columns are outside, and every arrow runs from a later visit to an earlier one: the read happens before the write, so the swap is illegal.</desc>
<defs><marker id="p7-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="40" y="24">Rows outside, as written</text>
<text class="vx-text-muted" x="40" y="44">i = 1 to 4 downward, j = 0 to 3 rightward</text>
<line class="vx-flow" x1="132.0" y1="100.0" x2="94.8" y2="137.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-line" x1="196.0" y1="100.0" x2="158.8" y2="137.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-line" x1="260.0" y1="100.0" x2="222.8" y2="137.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-line" x1="132.0" y1="164.0" x2="94.8" y2="201.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-line" x1="196.0" y1="164.0" x2="158.8" y2="201.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-line" x1="260.0" y1="164.0" x2="222.8" y2="201.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-line" x1="132.0" y1="228.0" x2="94.8" y2="265.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-line" x1="196.0" y1="228.0" x2="158.8" y2="265.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-line" x1="260.0" y1="228.0" x2="222.8" y2="265.2" marker-end="url(#p7-f2-head)"/>
<circle class="vx-box" cx="80" cy="88" r="16"/>
<text class="vx-mono" x="80" y="93" text-anchor="middle">1</text>
<circle class="vx-box" cx="144" cy="88" r="16"/>
<text class="vx-mono" x="144" y="93" text-anchor="middle">2</text>
<circle class="vx-box" cx="208" cy="88" r="16"/>
<text class="vx-mono" x="208" y="93" text-anchor="middle">3</text>
<circle class="vx-box" cx="272" cy="88" r="16"/>
<text class="vx-mono" x="272" y="93" text-anchor="middle">4</text>
<circle class="vx-box" cx="80" cy="152" r="16"/>
<text class="vx-mono" x="80" y="157" text-anchor="middle">5</text>
<circle class="vx-box" cx="144" cy="152" r="16"/>
<text class="vx-mono" x="144" y="157" text-anchor="middle">6</text>
<circle class="vx-box" cx="208" cy="152" r="16"/>
<text class="vx-mono" x="208" y="157" text-anchor="middle">7</text>
<circle class="vx-box" cx="272" cy="152" r="16"/>
<text class="vx-mono" x="272" y="157" text-anchor="middle">8</text>
<circle class="vx-box" cx="80" cy="216" r="16"/>
<text class="vx-mono" x="80" y="221" text-anchor="middle">9</text>
<circle class="vx-box" cx="144" cy="216" r="16"/>
<text class="vx-mono" x="144" y="221" text-anchor="middle">10</text>
<circle class="vx-box" cx="208" cy="216" r="16"/>
<text class="vx-mono" x="208" y="221" text-anchor="middle">11</text>
<circle class="vx-box" cx="272" cy="216" r="16"/>
<text class="vx-mono" x="272" y="221" text-anchor="middle">12</text>
<circle class="vx-box" cx="80" cy="280" r="16"/>
<text class="vx-mono" x="80" y="285" text-anchor="middle">13</text>
<circle class="vx-box" cx="144" cy="280" r="16"/>
<text class="vx-mono" x="144" y="285" text-anchor="middle">14</text>
<circle class="vx-box" cx="208" cy="280" r="16"/>
<text class="vx-mono" x="208" y="285" text-anchor="middle">15</text>
<circle class="vx-box" cx="272" cy="280" r="16"/>
<text class="vx-mono" x="272" y="285" text-anchor="middle">16</text>
<text class="vx-text" x="440" y="24">Columns outside, swapped</text>
<text class="vx-text-muted" x="440" y="44">i = 1 to 4 downward, j = 0 to 3 rightward</text>
<line class="vx-box-bad vx-pulse" x1="532.0" y1="100.0" x2="494.8" y2="137.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-box-bad" x1="596.0" y1="100.0" x2="558.8" y2="137.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-box-bad" x1="660.0" y1="100.0" x2="622.8" y2="137.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-box-bad" x1="532.0" y1="164.0" x2="494.8" y2="201.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-box-bad" x1="596.0" y1="164.0" x2="558.8" y2="201.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-box-bad" x1="660.0" y1="164.0" x2="622.8" y2="201.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-box-bad" x1="532.0" y1="228.0" x2="494.8" y2="265.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-box-bad" x1="596.0" y1="228.0" x2="558.8" y2="265.2" marker-end="url(#p7-f2-head)"/>
<line class="vx-box-bad" x1="660.0" y1="228.0" x2="622.8" y2="265.2" marker-end="url(#p7-f2-head)"/>
<circle class="vx-box" cx="480" cy="88" r="16"/>
<text class="vx-mono" x="480" y="93" text-anchor="middle">1</text>
<circle class="vx-box" cx="544" cy="88" r="16"/>
<text class="vx-mono" x="544" y="93" text-anchor="middle">5</text>
<circle class="vx-box" cx="608" cy="88" r="16"/>
<text class="vx-mono" x="608" y="93" text-anchor="middle">9</text>
<circle class="vx-box" cx="672" cy="88" r="16"/>
<text class="vx-mono" x="672" y="93" text-anchor="middle">13</text>
<circle class="vx-box" cx="480" cy="152" r="16"/>
<text class="vx-mono" x="480" y="157" text-anchor="middle">2</text>
<circle class="vx-box" cx="544" cy="152" r="16"/>
<text class="vx-mono" x="544" y="157" text-anchor="middle">6</text>
<circle class="vx-box" cx="608" cy="152" r="16"/>
<text class="vx-mono" x="608" y="157" text-anchor="middle">10</text>
<circle class="vx-box" cx="672" cy="152" r="16"/>
<text class="vx-mono" x="672" y="157" text-anchor="middle">14</text>
<circle class="vx-box" cx="480" cy="216" r="16"/>
<text class="vx-mono" x="480" y="221" text-anchor="middle">3</text>
<circle class="vx-box" cx="544" cy="216" r="16"/>
<text class="vx-mono" x="544" y="221" text-anchor="middle">7</text>
<circle class="vx-box" cx="608" cy="216" r="16"/>
<text class="vx-mono" x="608" y="221" text-anchor="middle">11</text>
<circle class="vx-box" cx="672" cy="216" r="16"/>
<text class="vx-mono" x="672" y="221" text-anchor="middle">15</text>
<circle class="vx-box" cx="480" cy="280" r="16"/>
<text class="vx-mono" x="480" y="285" text-anchor="middle">4</text>
<circle class="vx-box" cx="544" cy="280" r="16"/>
<text class="vx-mono" x="544" y="285" text-anchor="middle">8</text>
<circle class="vx-box" cx="608" cy="280" r="16"/>
<text class="vx-mono" x="608" y="285" text-anchor="middle">12</text>
<circle class="vx-box" cx="672" cy="280" r="16"/>
<text class="vx-mono" x="672" y="285" text-anchor="middle">16</text>
<text class="vx-text-muted" x="40" y="322">every arrow runs forward in time</text>
<text class="vx-text-accent" x="40" y="342">(1, &#8722;1) as written: legal</text>
<text class="vx-text-muted" x="440" y="322">every arrow now runs backward in time</text>
<text class="vx-text-accent" x="440" y="342">(&#8722;1, 1) after the swap: illegal</text>
</svg>
<figcaption>Figure 2. The sixteen iterations of <code>shift</code>, numbered in visit order. Each arrow carries a value from its writer to its reader. As written, every arrow runs forward, such as 2 to 5; swapped, the same arrow runs from visit 5 to visit 2, so the read comes first.</figcaption>
</figure>

A comment in LLVM's interchange pass states the rule for a **perfect loop nest**, one whose statements all sit in the innermost loop: a permutation of its loops is legal exactly when, after the same permutation of every direction vector, no vector has `>` as its first entry that is not `=`.[^llvm-interchange] Wolf and Lam give the same test for a wider family of transformations, which returns [with skewing](#skewing-and-the-unimodular-view).[^wl91]

For matrix multiplication the test is cheap. Treat the kernel as updating `c[row, column]` directly, as the next section justifies. Nothing writes `a` or `b`, and two iterations touch the same element of `c` only when they differ in `k` alone, so every dependence is `(=, =, <)`. Any permutation leaves `<` as the first entry that is not `=`: all six loop orders are legal.

??? check "The body of `shift` becomes `g[i, j] = g[i - 1, j - 1] + 1`, with `j` now in `1..5`. May its loops be swapped now?"

    Yes. The value comes from one row up and one column left, a distance of `(1, 1)`. Swapped, it is still `(1, 1)`, which starts with a positive entry, so the reader still runs after the writer.

## Making the stage 10 nest perfect

Legal is not the same as ready. `let mut sum` and `c[row, column] = sum` sit between the `column` and `k` loops, so the nest is not perfect, and compilers notice. On a C version of the kernel, cleaned up by `mem2reg` and similar passes, LLVM 18.1.8's `loop-interchange` did nothing, remarking that it handles only inner loops whose PHI nodes ([O3](o3-ssa.md)) are inductions or reductions (checked on the owner's M4 Pro on 2026-09-24). Its source adds that an inner PHI node must continue a reduction of the outer loop,[^llvm-interchange] and `sum` restarts for every `column`.

`sum` is itself a transformation, applied by hand. Callahan, Carr and Kennedy observed that most compilers of their time kept no array element in a register, because their data-flow analysis treated a whole array like one scalar. Their **scalar replacement** rewrites references to a reused element as references to a scalar temporary, which the register allocators of most compilers are then likely to keep in a register.[^cck90] To interchange, undo it first, in four steps.

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. The kernel as written**

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            let mut sum: f32 = 0.0;
            for k in 0..64 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

`sum` makes the nest imperfect.

</div>
<div class="vx-step" markdown="1">

**Step 2. Put the sum back in `c`**

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            c[row, column] = 0.0;
            for k in 0..64 {
                c[row, column] += a[row, k] * b[k, column];
            }
        }
    }
}
```

Legal, because `sum` lived for one `(row, column)` pair and nothing else read it. Same bits: `c[row, column]` starts at `0.0` as `sum` did, and each `+=` rounds the product, then the sum ([decision 56](../decisions/numbers.md#d56)). One condition is new: a write to `c` must not change `a` or `b`. [Decision 25](../decisions/references.md#d25) guarantees it: the variable a caller lends as `&mut` for `c` cannot also appear in the call's other arguments.

</div>
<div class="vx-step" markdown="1">

**Step 3. Split the zeroing into its own loop**

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            c[row, column] = 0.0;
        }
        for column in 0..64 {
            for k in 0..64 {
                c[row, column] += a[row, k] * b[k, column];
            }
        }
    }
}
```

This is **loop fission** ([below](#fusion-and-fission)), legal because each element is still zeroed before its updates and the updates of one element never touch another. The second `column` loop and the `k` loop now form a perfect nest.

</div>
<div class="vx-step" markdown="1">

**Step 4. Swap `column` and `k`**

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            c[row, column] = 0.0;
        }
        for k in 0..64 {
            for column in 0..64 {
                c[row, column] += a[row, k] * b[k, column];
            }
        }
    }
}
```

Legal: the one dependence, on `c[row, column]` along `k`, is `(=, <)` in `(column, k)` order and `(<, =)` after the swap. Same bits: each element still adds its products in increasing `k`, starting from `0.0`. The inner loop walks `b` and `c` along rows, and `a[row, k]` can be loaded once per `k` (loop-invariant code motion, [O6](o6-redundancy.md)).

</div>
</div>

LLVM's pass asks for still more. It considers a nest only when every loop but the innermost holds exactly one inner loop,[^llvm-interchange] so it skipped a C version of step 3 without a remark: the zeroing loop sits beside the second `column` loop. With the zeroing moved in front of the `row` loop, one more fission, it swapped `column` and `k` by itself (same machine and date).

Step 2 is where Vortex has an advantage over C, whose default aliasing rules, Drepper points out, give the compiler no help: unless `restrict` is used, any pointer access may touch any array.[^drepper] A Vortex compiler can cite the guarantee when it explains itself, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks: "interchanged `column` and `k`: `c` is a `&mut` argument, so it cannot overlap `a` or `b`".

### Your turn: column sums

This kernel adds up each column of a matrix, and its inner loop reads `m` 1 KiB apart, one cache line per read:

```vortex
// items: valid
fn column_sums(m: &[f32; 256, 256], totals: &mut [f32; 256]) {
    for column in 0..256 {
        let mut total: f32 = 0.0;
        for row in 0..256 {
            total += m[row, column];
        }
        totals[column] = total;
    }
}
```

Step 2 gives:

```vortex
// items: valid
fn column_sums(m: &[f32; 256, 256], totals: &mut [f32; 256]) {
    for column in 0..256 {
        totals[column] = 0.0;
        for row in 0..256 {
            totals[column] += m[row, column];
        }
    }
}
```

??? check "What do steps 3 and 4 produce for `column_sums`, and can the bits change?"

    Step 3 moves `totals[column] = 0.0;` into its own loop before the nest; step 4 puts `row` outside. The one dependence, on `totals[column]` along `row`, goes from `(=, <)` to `(<, =)`, so the swap is legal. Each total still adds its column in increasing `row` from `0.0`, so no bit changes, and `m` is read along its rows.

## What a Vortex transformation must also keep

Dependences between array elements are the textbook condition, and the textbook assumes a forgiving language: Carr and Kennedy state their safety rule for unroll-and-jam for "a semantics that is only defined on correct programs (as in Fortran)".[^ck94] Vortex defines what happens after an error, and a conforming compiler must preserve what a program prints, its runtime error line and its exit status ([Conformance 1.3](../specification/conformance.md#13-implementation-conformance)). Add decision 56's ban on changing floating-point results, and a Vortex compiler must account for three more things.

**Every floating-point accumulation order.** Interchange kept [decision 56](../decisions/numbers.md#d56) above because each element of `c` kept its own order: operations on different elements never combine, so running them in another order changes no printed digit. A nest that pours everything into one accumulator, `total += x[i, j]` over both loops, is different: swapped, it adds the numbers in another order, and the rounded total can change. `total` ties every iteration to every other, and a compiler may look past that only if the order cannot matter.

LLVM draws the same line. Its interchange pass accepts a floating-point sum that runs through both loops only when the sum may be reordered:[^llvm-interchange] on a C version of the whole-grid sum it refused the swap, and made it once `-ffast-math` allowed reassociation (checked on the owner's M4 Pro on 2026-09-24). Its vectorizer, on most targets, vectorizes a floating-point reduction only when reassociation is permitted; on some, AArch64 among them, its ordered reductions keep the exact result but are typically slower.[^llvm-vec]

Integers are no escape: a Vortex integer `+` that overflows stops the program ([decision 34](../decisions/diagnostics.md#d34)), and another order can overflow where the original did not.

**Which check fails first, and what was printed before it.** A failed check writes one line naming its position ([decision 14](../decisions/program.md#d14)), so another order can report another failure, and a body that calls `print` would print in another order. The safe policy is to transform only nests whose checks are proven unable to fail, which fixed shapes and constant bounds make routine ([O8](o8-loops.md)), and to treat `print` as a dependence no transformation may reorder. LLVM's interchange pass likewise refuses a nest containing a call that may read memory.[^llvm-interchange]

**No overlap between arrays**, which `&mut` settled in step 2.

The first example runs three nests as written and swapped: the matrix multiplication (same bits), one floating-point sum over a whole grid (different bits) and the `shift` nest (different values).

--8<-- "includes/examples/optimize/p7-loop-transformations/interchange.cpp.md"

## Fusion and fission

**Loop fusion** merges adjacent loops with the same iteration range into one loop whose body holds both bodies. Loop fission, also called **loop distribution**, splits one loop into several, each with some of the statements. Fusion can join these two loops:

```vortex
// items: valid
fn scale_then_shift(x: &[f32; 1024], scaled: &mut [f32; 1024], out: &mut [f32; 1024]) {
    for i in 0..1024 {
        scaled[i] = 2.0 * x[i];
    }
    for i in 0..1024 {
        out[i] = scaled[i] + 1.0;
    }
}
```

Fused, each `scaled[i]` is used while still in a register instead of being read back in a second pass over 4 KiB. The stores to `scaled` stay, because the caller can see that array.

LLVM's fusion pass lists four conditions: the loops are adjacent, they run the same number of iterations, one runs exactly when the other does, and no dependence between them has a negative distance.[^llvm-fuse] With fixed shapes, the second compares two constants. When the counts differ by a few iterations, **loop peeling**, running those iterations of one loop as straight-line code, can make them equal; LLVM 18's fusion pass peels only when a hidden option allows it, and by default it allows none.[^llvm-fuse] If the second loop read `scaled[i + 1]`, fused iteration `i` would read an element that iteration `i + 1` has not yet written: a distance of −1, pointing backwards.

Fission is legal unless it breaks a recurrence, a cycle of data or control dependences.[^ck94] Statements that feed each other across iterations, such as `x[i] = y[i - 1] + 1` and `y[i] = 2 * x[i]`, must stay in one loop; the others may go to separate loops in dependence order.

Fission makes an imperfect nest perfect, as in step 3, and it can isolate a statement that blocks vectorization, the stated purpose of LLVM's distribution pass. That pass can also guard a loop with a run-time check that two arrays do not overlap,[^llvm-distribute] a check Vortex never needs between a `&mut` parameter and another parameter. Clang leaves distribution off by default because it can hurt, for example by leaving the processor fewer independent instructions to overlap.[^clang-le]

The second example applies each transformation once legally and once illegally, and compares the results.

--8<-- "includes/examples/optimize/p7-loop-transformations/fusion.cpp.md"

??? check "Can `for i in 1..64 { b[i] = a[i] + 1.0; }` and `for i in 1..64 { c[i] = b[i - 1] * 2.0; }` be fused?"

    Yes. Iteration `i` of the second loop reads what iteration `i − 1` of the first wrote, and in the fused loop that write happens one iteration earlier: a distance of +1, forward in time. `b[0]` is never written, so both versions read its old value.

## Unrolling

**Loop unrolling** copies a loop's body several times and steps the loop by that many: unrolled by 4, the `k` loop runs 16 times, each doing the work of `k` to `k + 3`. The increment, compare and branch run a quarter as often, and the instruction scheduler ([C6](../backend/c6-scheduling.md)) sees four bodies at once. When the factor does not divide the **trip count**, the number of iterations, the leftover iterations need a **remainder loop**, or a few straight-line copies when the trip count is a known constant. A Vortex compiler usually knows it: `a..b` runs `b − a` times when `a < b` ([decision 13](../decisions/statements.md#d13)), and the stage 10 bounds are constants.

Unrolling alone never regroups floating-point work: the copies still add into one `sum`, in order. That is also its limit. Each addition to `sum` waits for the one before it, and unrolling does not shorten that chain. Partial sums would, but they regroup the additions, which decision 56 forbids.

## Unroll-and-jam and register blocks

Independent chains can come from somewhere else: several elements of `c` at once. **Unroll-and-jam** unrolls an outer loop and then fuses, or jams, the copies of the inner loop into one.[^llvm-passes] Unroll `row` and `column` by 2, jam the four `k` loops, and give each its own running sum:

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row_pair in 0..32 {
        let row = 2 * row_pair;
        for column_pair in 0..32 {
            let column = 2 * column_pair;
            let mut sum00: f32 = 0.0;
            let mut sum01: f32 = 0.0;
            let mut sum10: f32 = 0.0;
            let mut sum11: f32 = 0.0;
            for k in 0..64 {
                let a0 = a[row, k];
                let a1 = a[row + 1, k];
                let b0 = b[k, column];
                let b1 = b[k, column + 1];
                sum00 += a0 * b0;
                sum01 += a0 * b1;
                sum10 += a1 * b0;
                sum11 += a1 * b1;
            }
            c[row, column] = sum00;
            c[row, column + 1] = sum01;
            c[row + 1, column] = sum10;
            c[row + 1, column + 1] = sum11;
        }
    }
}
```

Each `k` step loads two values of `a` and two of `b` for four multiply-adds, so every loaded value is used twice: one load per multiply-add instead of two. A block of `mr` rows by `nr` columns loads `mr + nr` values per step for `mr × nr` multiply-adds:

$$
\frac{\text{loads}}{\text{multiply-add}} = \frac{m_r + n_r}{m_r \, n_r}
$$

<figure class="vx-figure">
<svg viewBox="0 0 760 346" role="img" aria-label="A 2 by 2 register block after unroll-and-jam" aria-describedby="p7-f3-desc">
<title id="p7-f3-title">A 2 by 2 register block after unroll-and-jam</title>
<desc id="p7-f3-desc">Matrix a on the left, matrix b above matrix c. A 2 by 2 block of c is marked; its four running sums, s00 to s11, stay in registers. At each step of k, one column slice of two values of a and one row slice of two values of b are highlighted; the a slice moves right and the b slice moves down as k advances. Beside the matrices, the loads per multiply-add are counted: 2 for the plain loop, 1 for the 2 by 2 block, and m r plus n r over m r times n r for a general block.</desc>
<rect class="vx-box" x="230" y="16" width="36" height="36"/>
<rect class="vx-box" x="40" y="176" width="36" height="36"/>
<rect class="vx-box" x="230" y="176" width="36" height="36"/>
<rect class="vx-box" x="266" y="16" width="36" height="36"/>
<rect class="vx-box" x="76" y="176" width="36" height="36"/>
<rect class="vx-box" x="266" y="176" width="36" height="36"/>
<rect class="vx-box" x="302" y="16" width="36" height="36"/>
<rect class="vx-box" x="112" y="176" width="36" height="36"/>
<rect class="vx-box" x="302" y="176" width="36" height="36"/>
<rect class="vx-box" x="338" y="16" width="36" height="36"/>
<rect class="vx-box" x="148" y="176" width="36" height="36"/>
<rect class="vx-box" x="338" y="176" width="36" height="36"/>
<rect class="vx-box" x="230" y="52" width="36" height="36"/>
<rect class="vx-box" x="40" y="212" width="36" height="36"/>
<rect class="vx-box" x="230" y="212" width="36" height="36"/>
<rect class="vx-box" x="266" y="52" width="36" height="36"/>
<rect class="vx-box" x="76" y="212" width="36" height="36"/>
<rect class="vx-box" x="266" y="212" width="36" height="36"/>
<rect class="vx-box" x="302" y="52" width="36" height="36"/>
<rect class="vx-box" x="112" y="212" width="36" height="36"/>
<rect class="vx-box" x="302" y="212" width="36" height="36"/>
<rect class="vx-box" x="338" y="52" width="36" height="36"/>
<rect class="vx-box" x="148" y="212" width="36" height="36"/>
<rect class="vx-box" x="338" y="212" width="36" height="36"/>
<rect class="vx-box" x="230" y="88" width="36" height="36"/>
<rect class="vx-box" x="40" y="248" width="36" height="36"/>
<rect class="vx-box" x="230" y="248" width="36" height="36"/>
<rect class="vx-box" x="266" y="88" width="36" height="36"/>
<rect class="vx-box" x="76" y="248" width="36" height="36"/>
<rect class="vx-box" x="266" y="248" width="36" height="36"/>
<rect class="vx-box" x="302" y="88" width="36" height="36"/>
<rect class="vx-box" x="112" y="248" width="36" height="36"/>
<rect class="vx-box" x="302" y="248" width="36" height="36"/>
<rect class="vx-box" x="338" y="88" width="36" height="36"/>
<rect class="vx-box" x="148" y="248" width="36" height="36"/>
<rect class="vx-box" x="338" y="248" width="36" height="36"/>
<rect class="vx-box" x="230" y="124" width="36" height="36"/>
<rect class="vx-box" x="40" y="284" width="36" height="36"/>
<rect class="vx-box" x="230" y="284" width="36" height="36"/>
<rect class="vx-box" x="266" y="124" width="36" height="36"/>
<rect class="vx-box" x="76" y="284" width="36" height="36"/>
<rect class="vx-box" x="266" y="284" width="36" height="36"/>
<rect class="vx-box" x="302" y="124" width="36" height="36"/>
<rect class="vx-box" x="112" y="284" width="36" height="36"/>
<rect class="vx-box" x="302" y="284" width="36" height="36"/>
<rect class="vx-box" x="338" y="124" width="36" height="36"/>
<rect class="vx-box" x="148" y="284" width="36" height="36"/>
<rect class="vx-box" x="338" y="284" width="36" height="36"/>
<rect class="vx-box-accent" x="230" y="176" width="72" height="72"/>
<rect class="vx-box-accent" x="40" y="176" width="36" height="72"><animateTransform attributeName="transform" type="translate" dur="4.8s" repeatCount="indefinite" calcMode="discrete" values="0 0;36 0;72 0;108 0"/></rect>
<rect class="vx-box-accent" x="230" y="16" width="72" height="36"><animateTransform attributeName="transform" type="translate" dur="4.8s" repeatCount="indefinite" calcMode="discrete" values="0 0;0 36;0 72;0 108"/></rect>
<text class="vx-mono" x="248" y="199" text-anchor="middle">s00</text>
<text class="vx-mono" x="284" y="199" text-anchor="middle">s01</text>
<text class="vx-mono" x="248" y="235" text-anchor="middle">s10</text>
<text class="vx-mono" x="284" y="235" text-anchor="middle">s11</text>
<text class="vx-text" x="18" y="250">a</text>
<text class="vx-text" x="386" y="92">b</text>
<text class="vx-text" x="386" y="252">c</text>
<text class="vx-text-muted" x="58" y="170" text-anchor="middle">0</text>
<text class="vx-text-muted" x="216" y="38" text-anchor="middle">0</text>
<text class="vx-text-muted" x="94" y="170" text-anchor="middle">1</text>
<text class="vx-text-muted" x="216" y="74" text-anchor="middle">1</text>
<text class="vx-text-muted" x="130" y="170" text-anchor="middle">2</text>
<text class="vx-text-muted" x="216" y="110" text-anchor="middle">2</text>
<text class="vx-text-muted" x="166" y="170" text-anchor="middle">3</text>
<text class="vx-text-muted" x="216" y="146" text-anchor="middle">3</text>
<text class="vx-text-muted" x="40" y="340">k: the highlighted column of a and row of b move together</text>
<text class="vx-text" x="430" y="40">One k step of the 2 &#215; 2 block</text>
<text class="vx-text-muted" x="430" y="64">load a[row, k] and a[row + 1, k]</text>
<text class="vx-text-muted" x="430" y="84">load b[k, column] and b[k, column + 1]</text>
<text class="vx-text-muted" x="430" y="104">4 multiply-adds into s00, s01, s10, s11</text>
<text class="vx-text-muted" x="430" y="124">each loaded value is used twice</text>
<text class="vx-text" x="430" y="172">Loads per multiply-add</text>
<text class="vx-mono" x="430" y="198">plain loop</text>
<text class="vx-mono" x="560" y="198">2 / 1 = 2</text>
<text class="vx-mono" x="430" y="222">2 &#215; 2 block</text>
<text class="vx-mono" x="560" y="222">4 / 4 = 1</text>
<text class="vx-mono" x="430" y="246">mr &#215; nr block</text>
<text class="vx-mono" x="560" y="246">(mr + nr) / (mr &#183; nr)</text>
<text class="vx-text-muted" x="430" y="284">s00 to s11 stay in registers</text>
<text class="vx-text-muted" x="430" y="302">for the whole k loop</text>
</svg>
<figcaption>Figure 3. One <code>k</code> step of the 2 × 2 block. The four sums of the marked block of <code>c</code>, s00 to s11, stay in registers for the whole <code>k</code> loop. Each step loads two values of <code>a</code> and two of <code>b</code>, and each value meets both values of the other pair.</figcaption>
</figure>

Carr and Kennedy measure this with **balance**: a loop's memory references per floating-point operation, compared with the words per operation the machine can fetch at peak (a multiply-add counts as one operation). A loop whose balance exceeds the machine's waits for memory, and unroll-and-jam with scalar replacement can lower it. They argue that the compiler should do this, not programmers writing hard-to-read code by hand,[^ck94] as Vortex's fourth principle, [make the simple version work well](../philosophy.md#4-make-the-simple-version-work-well), also asks. McKinley, Carr and Tseng give its other name, **register tiling**.[^mct96]

The register file sets the limit. A 2 × 2 block keeps 4 sums and 4 loaded values live at once; a block too large for the registers makes the allocator store values and reload them ([C5](../backend/c5-spilling.md)), spending the loads it was meant to save. [P12](p12-fast-gemm.md) sizes blocks for real registers and SIMD widths.

Carr and Kennedy state the legality rule, taken from Callahan and colleagues: a dependence carried by the unrolled loop with distance `d` there, followed by zeros and then a negative entry, allows at most `d − 1` extra copies of the body.[^ck94] Their example has the `shift` nest's `(1, −1)`, which allows none. Matrix multiplication has no negative entries, so any factor is legal.

The bits are the same: each sum starts at `0.0` and adds its products in increasing `k`. Restarting the sums for each group of `k` and adding them into `c` afterwards would split each element's chain of additions, which under decision 56 needs the same permission as reassociation. Index arithmetic such as `row + 1` is checked in source; a compiler that transforms the nest itself knows these values are in range.

The third example applies unroll-and-jam to a related kernel, the dot product of every row of one matrix with every row of another, counts the loads, and compares the bits.

--8<-- "includes/examples/optimize/p7-loop-transformations/unroll_and_jam.cpp.md"

??? check "How many loads per multiply-add does a 4 × 4 block need, and what does it cost?"

    Eight loads for sixteen multiply-adds, 0.5 per multiply-add, half the 2 × 2 figure. The price is 16 sums and 8 loaded values live at once, which must all fit in registers.

## Strip-mining and tiling

**Strip-mining** splits one loop in two: an outer loop over strips of, say, 16 iterations, and an inner loop within a strip. It reorders nothing, so it is always legal, with a remainder when the strip length does not divide the trip count.

**Tiling**, also called blocking, strip-mines several loops and moves all the strip loops outside: strip-mining combined with loop permutation, as McKinley, Carr and Tseng describe it.[^mct96] An n-deep nest becomes 2n deep, the outer loops stepping from tile to tile,[^wl91] so the data a tile reuses can stay in cache while the tile runs: reuse becomes locality.

Wolf and Lam's condition for tiling is that the **band**, the consecutive loops being tiled, be **fully permutable**: every dependence lexicographically positive (its first nonzero entry positive) and, within the band, either already carried by an outer loop or free of negative entries.[^wl91] Matrix multiplication's `(=, =, <)` qualifies, so all three loops can be tiled, and the bits stay the same as long as the blocks of `k` run in increasing order and each element of `c` accumulates in place. [P8](p8-cache-blocking.md) chooses the block sizes.

## Skewing and the unimodular view

The last transformation reshapes the iteration space. Each value here depends on its upper and left neighbours:

```vortex
// items: valid
fn wave(h: &mut [f32; 5, 7]) {
    for i in 1..5 {
        for j in 1..7 {
            h[i, j] = 0.5 * h[i - 1, j] + 0.25 * h[i, j - 1];
        }
    }
}
```

The distances are `(1, 0)`, from above, and `(0, 1)`, from the left. Each loop carries one, so neither loop's iterations can run at the same time, with or without interchange. But the cells of one anti-diagonal, with the same `i + j`, depend only on the previous anti-diagonal. Computing one anti-diagonal after another is a **wavefront**.

**Loop skewing** turns the wavefront into a loop. It replaces `j` with `w = i + j`, which makes the distances `(1, 1)` and `(0, 1)` in `(i, w)` coordinates; interchanging so that `w` runs outside makes them `(1, 1)` and `(1, 0)`. The `w` loop now carries every dependence, and the inner loop, over one anti-diagonal, carries none: its iterations may run in any order, on several cores ([P13](p13-multithreading.md)) or in SIMD lanes ([P10](p10-vectorization.md)).

<figure class="vx-figure">
<svg viewBox="0 0 760 244" role="img" aria-label="Skewing turns anti-diagonal waves into columns" aria-describedby="p7-f4-desc">
<title id="p7-f4-title">Skewing turns anti-diagonal waves into columns</title>
<desc id="p7-f4-desc">Left: a 4 by 6 grid of cells, each numbered with w, the sum of its row i and column j. Each cell needs the cell above it and the cell to its left, so cells with the same number, an anti-diagonal, form a wave of independent work. Right: the same cells redrawn at column w, which shears the grid into a parallelogram; every wave is now one vertical column, and both inputs of every cell lie in the previous column. One cell, i = 3 and j = 4, is outlined in both drawings, with arrows from its two inputs. The waves light up in turn in both drawings.</desc>
<rect class="vx-box" x="40" y="70" width="30" height="30"/>
<rect class="vx-box" x="430" y="70" width="30" height="30"/>
<rect class="vx-box" x="70" y="70" width="30" height="30"/>
<rect class="vx-box" x="460" y="70" width="30" height="30"/>
<rect class="vx-box" x="100" y="70" width="30" height="30"/>
<rect class="vx-box" x="490" y="70" width="30" height="30"/>
<rect class="vx-box" x="130" y="70" width="30" height="30"/>
<rect class="vx-box" x="520" y="70" width="30" height="30"/>
<rect class="vx-box" x="160" y="70" width="30" height="30"/>
<rect class="vx-box" x="550" y="70" width="30" height="30"/>
<rect class="vx-box" x="190" y="70" width="30" height="30"/>
<rect class="vx-box" x="580" y="70" width="30" height="30"/>
<rect class="vx-box" x="40" y="100" width="30" height="30"/>
<rect class="vx-box" x="460" y="100" width="30" height="30"/>
<rect class="vx-box" x="70" y="100" width="30" height="30"/>
<rect class="vx-box" x="490" y="100" width="30" height="30"/>
<rect class="vx-box" x="100" y="100" width="30" height="30"/>
<rect class="vx-box" x="520" y="100" width="30" height="30"/>
<rect class="vx-box" x="130" y="100" width="30" height="30"/>
<rect class="vx-box" x="550" y="100" width="30" height="30"/>
<rect class="vx-box" x="160" y="100" width="30" height="30"/>
<rect class="vx-box" x="580" y="100" width="30" height="30"/>
<rect class="vx-box" x="190" y="100" width="30" height="30"/>
<rect class="vx-box" x="610" y="100" width="30" height="30"/>
<rect class="vx-box" x="40" y="130" width="30" height="30"/>
<rect class="vx-box" x="490" y="130" width="30" height="30"/>
<rect class="vx-box" x="70" y="130" width="30" height="30"/>
<rect class="vx-box" x="520" y="130" width="30" height="30"/>
<rect class="vx-box" x="100" y="130" width="30" height="30"/>
<rect class="vx-box" x="550" y="130" width="30" height="30"/>
<rect class="vx-box" x="130" y="130" width="30" height="30"/>
<rect class="vx-box" x="580" y="130" width="30" height="30"/>
<rect class="vx-box" x="160" y="130" width="30" height="30"/>
<rect class="vx-box" x="610" y="130" width="30" height="30"/>
<rect class="vx-box" x="190" y="130" width="30" height="30"/>
<rect class="vx-box" x="640" y="130" width="30" height="30"/>
<rect class="vx-box" x="40" y="160" width="30" height="30"/>
<rect class="vx-box" x="520" y="160" width="30" height="30"/>
<rect class="vx-box" x="70" y="160" width="30" height="30"/>
<rect class="vx-box" x="550" y="160" width="30" height="30"/>
<rect class="vx-box" x="100" y="160" width="30" height="30"/>
<rect class="vx-box" x="580" y="160" width="30" height="30"/>
<rect class="vx-box" x="130" y="160" width="30" height="30"/>
<rect class="vx-box" x="610" y="160" width="30" height="30"/>
<rect class="vx-box" x="160" y="160" width="30" height="30"/>
<rect class="vx-box" x="640" y="160" width="30" height="30"/>
<rect class="vx-box" x="190" y="160" width="30" height="30"/>
<rect class="vx-box" x="670" y="160" width="30" height="30"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 9">
<rect class="vx-box-accent" x="40" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="430" y="70" width="30" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 9">
<rect class="vx-box-accent" x="70" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="460" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="40" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="460" y="100" width="30" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 9">
<rect class="vx-box-accent" x="100" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="490" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="70" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="490" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="40" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="490" y="130" width="30" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 9">
<rect class="vx-box-accent" x="130" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="520" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="100" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="520" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="70" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="520" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="40" y="160" width="30" height="30"/>
<rect class="vx-box-accent" x="520" y="160" width="30" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 9">
<rect class="vx-box-accent" x="160" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="550" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="130" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="550" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="100" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="550" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="70" y="160" width="30" height="30"/>
<rect class="vx-box-accent" x="550" y="160" width="30" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 9">
<rect class="vx-box-accent" x="190" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="580" y="70" width="30" height="30"/>
<rect class="vx-box-accent" x="160" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="580" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="130" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="580" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="100" y="160" width="30" height="30"/>
<rect class="vx-box-accent" x="580" y="160" width="30" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 9">
<rect class="vx-box-accent" x="190" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="610" y="100" width="30" height="30"/>
<rect class="vx-box-accent" x="160" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="610" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="130" y="160" width="30" height="30"/>
<rect class="vx-box-accent" x="610" y="160" width="30" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 9">
<rect class="vx-box-accent" x="190" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="640" y="130" width="30" height="30"/>
<rect class="vx-box-accent" x="160" y="160" width="30" height="30"/>
<rect class="vx-box-accent" x="640" y="160" width="30" height="30"/>
</g>
<g class="vx-seq" style="--vx-i: 8; --vx-n: 9">
<rect class="vx-box-accent" x="190" y="160" width="30" height="30"/>
<rect class="vx-box-accent" x="670" y="160" width="30" height="30"/>
</g>
<defs><marker id="p7-f4-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="5" markerHeight="5" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="130" y="130" width="30" height="30"/>
<rect class="vx-box-strong" x="580" y="130" width="30" height="30"/>
<text class="vx-mono" x="55" y="90" text-anchor="middle">2</text>
<text class="vx-mono" x="445" y="90" text-anchor="middle">2</text>
<text class="vx-mono" x="85" y="90" text-anchor="middle">3</text>
<text class="vx-mono" x="475" y="90" text-anchor="middle">3</text>
<text class="vx-mono" x="115" y="90" text-anchor="middle">4</text>
<text class="vx-mono" x="505" y="90" text-anchor="middle">4</text>
<text class="vx-mono" x="145" y="90" text-anchor="middle">5</text>
<text class="vx-mono" x="535" y="90" text-anchor="middle">5</text>
<text class="vx-mono" x="175" y="90" text-anchor="middle">6</text>
<text class="vx-mono" x="565" y="90" text-anchor="middle">6</text>
<text class="vx-mono" x="205" y="90" text-anchor="middle">7</text>
<text class="vx-mono" x="595" y="90" text-anchor="middle">7</text>
<text class="vx-mono" x="55" y="120" text-anchor="middle">3</text>
<text class="vx-mono" x="475" y="120" text-anchor="middle">3</text>
<text class="vx-mono" x="85" y="120" text-anchor="middle">4</text>
<text class="vx-mono" x="505" y="120" text-anchor="middle">4</text>
<text class="vx-mono" x="115" y="120" text-anchor="middle">5</text>
<text class="vx-mono" x="535" y="120" text-anchor="middle">5</text>
<text class="vx-mono" x="145" y="120" text-anchor="middle">6</text>
<text class="vx-mono" x="565" y="120" text-anchor="middle">6</text>
<text class="vx-mono" x="175" y="120" text-anchor="middle">7</text>
<text class="vx-mono" x="595" y="120" text-anchor="middle">7</text>
<text class="vx-mono" x="205" y="120" text-anchor="middle">8</text>
<text class="vx-mono" x="625" y="120" text-anchor="middle">8</text>
<text class="vx-mono" x="55" y="150" text-anchor="middle">4</text>
<text class="vx-mono" x="505" y="150" text-anchor="middle">4</text>
<text class="vx-mono" x="85" y="150" text-anchor="middle">5</text>
<text class="vx-mono" x="535" y="150" text-anchor="middle">5</text>
<text class="vx-mono" x="115" y="150" text-anchor="middle">6</text>
<text class="vx-mono" x="565" y="150" text-anchor="middle">6</text>
<text class="vx-mono" x="145" y="150" text-anchor="middle">7</text>
<text class="vx-mono" x="595" y="150" text-anchor="middle">7</text>
<text class="vx-mono" x="175" y="150" text-anchor="middle">8</text>
<text class="vx-mono" x="625" y="150" text-anchor="middle">8</text>
<text class="vx-mono" x="205" y="150" text-anchor="middle">9</text>
<text class="vx-mono" x="655" y="150" text-anchor="middle">9</text>
<text class="vx-mono" x="55" y="180" text-anchor="middle">5</text>
<text class="vx-mono" x="535" y="180" text-anchor="middle">5</text>
<text class="vx-mono" x="85" y="180" text-anchor="middle">6</text>
<text class="vx-mono" x="565" y="180" text-anchor="middle">6</text>
<text class="vx-mono" x="115" y="180" text-anchor="middle">7</text>
<text class="vx-mono" x="595" y="180" text-anchor="middle">7</text>
<text class="vx-mono" x="145" y="180" text-anchor="middle">8</text>
<text class="vx-mono" x="625" y="180" text-anchor="middle">8</text>
<text class="vx-mono" x="175" y="180" text-anchor="middle">9</text>
<text class="vx-mono" x="655" y="180" text-anchor="middle">9</text>
<text class="vx-mono" x="205" y="180" text-anchor="middle">10</text>
<text class="vx-mono" x="685" y="180" text-anchor="middle">10</text>
<line class="vx-line" x1="145.0" y1="125" x2="145.0" y2="138" marker-end="url(#p7-f4-head)"/>
<line class="vx-line" x1="125" y1="145" x2="138" y2="145" marker-end="url(#p7-f4-head)"/>
<line class="vx-line" x1="559.0" y1="125" x2="587.5" y2="138" marker-end="url(#p7-f4-head)"/>
<line class="vx-line" x1="575" y1="145" x2="588" y2="145" marker-end="url(#p7-f4-head)"/>
<text class="vx-text" x="40" y="24">Before skewing: rows i, columns j</text>
<text class="vx-text-muted" x="40" y="46">each cell needs the cell above and the cell to its left</text>
<text class="vx-text" x="430" y="24">After skewing: columns w = i + j</text>
<text class="vx-text-muted" x="430" y="46">both inputs of a cell sit in the previous column</text>
<text class="vx-text-muted" x="40" y="214">a wave is every cell with the same number</text>
<text class="vx-text-muted" x="430" y="214">one column, one wave of independent cells</text>
</svg>
<figcaption>Figure 4. The <code>wave</code> nest before and after skewing, each cell labelled with <code>w = i + j</code>. On the left a wave is an anti-diagonal; on the right the same cells sit at column <code>w</code>, so each wave is a column. The outlined cell, <code>i = 3</code> and <code>j = 4</code>, gets its inputs along the arrows, both from the previous column.</figcaption>
</figure>

Wolf and Lam's framework writes interchange, skewing and **loop reversal** (running a loop backwards) as one operation: multiplying the iteration vector by a **unimodular matrix**, a square integer matrix whose determinant is 1 or −1.[^wl91] Products and inverses of unimodular matrices are unimodular, so any sequence of these transformations is one matrix $T$, and one test decides its legality: $T$ is legal exactly when $T\,d$ is lexicographically positive for every dependence distance $d$.[^wl91] For the wavefront:

$$
T_{\text{skew}} = \begin{pmatrix} 1 & 0 \\ 1 & 1 \end{pmatrix},\qquad
T_{\text{swap}} = \begin{pmatrix} 0 & 1 \\ 1 & 0 \end{pmatrix},\qquad
T = T_{\text{swap}}\,T_{\text{skew}} = \begin{pmatrix} 1 & 1 \\ 1 & 0 \end{pmatrix}
$$

$$
T \begin{pmatrix} 1 \\ 0 \end{pmatrix} = \begin{pmatrix} 1 \\ 1 \end{pmatrix},\qquad
T \begin{pmatrix} 0 \\ 1 \end{pmatrix} = \begin{pmatrix} 1 \\ 0 \end{pmatrix}
$$

Both results start with a positive entry, so the new nest is legal, and because that entry belongs to the outer `w` loop, the inner loop carries nothing. Skewing also enables other transformations: Wolf and Lam, citing Wolfe, add that it can make a pair of loops tilable,[^wl91] and Carr and Kennedy use it to clear the negative entries that block unroll-and-jam.[^ck94] [P9](p9-polyhedral-model.md) generalizes these matrices to affine schedules.

Skewing reorders only independent cells, each computed from the same operands by the same operations, so no bit can change. The fourth example runs both orders, prints the size of each wave, and compares the bits.

--8<-- "includes/examples/optimize/p7-loop-transformations/skewing.cpp.md"

## Putting them in order

In Vortex every floating-point accumulation counts as a dependence, so a transformation that passes its legality test keeps every bit. The last column names the shortcut that would break that.

| Transformation | Legal when | What would change Vortex's bits |
| --- | --- | --- |
| Interchange | every dependence stays lexicographically positive | swapping the loops around one shared accumulator |
| Fusion | no dependence between the loops gets a negative distance | only an illegal version |
| Fission | no cycle of dependences is split | only an illegal version |
| Unrolling | always, with a remainder when needed | partial sums for one accumulator |
| Unroll-and-jam | at most d − 1 extra copies for each dependence like `shift`'s | sums restarted for each group of `k` |
| Strip-mining | always | nothing |
| Tiling | the band is fully permutable | accumulation blocks run out of order |
| Skewing | always, for an inner loop skewed by an outer one; with interchange, every $T\,d$ positive | only an illegal version |
| Reversal | the loop carries no dependence | reversing an accumulation |

No single transformation makes the kernel fast, and their order matters. McKinley, Carr and Tseng propose an order for locality work that this chapter follows:[^mct96]

1. Fix the order of memory accesses with permutation, fusion, distribution, skewing and reversal, which needs little machine knowledge beyond the cache line size.
2. Tile, so that blocks of data fit in the caches, which needs the cache sizes, the line size and the data sizes.
3. Unroll-and-jam and scalar-replace, so that values are reused from registers, which needs the number and kind of registers.

Carr and Kennedy agree from the other side: they unroll-and-jam only inside tiles, because unrolling a loop that steps between tiles would enlarge the data each tile keeps in cache.[^ck94] [The CPU matmul ladder](ladder.md) climbs in the same order. It is a guide, not a law: skewing gains nothing by itself and matters only for what it allows, and as Wolf and Lam put it, "the desirability of a transformation cannot be evaluated locally".[^wl91] [O10](o10-pass-pipelines.md) covers choosing and ordering passes.

### What LLVM does with these

On the owner's machine, LLVM 18.1.8 has passes named `loop-interchange`, `loop-fusion`, `loop-distribute`, `loop-unroll` and `loop-unroll-and-jam`, and none for tiling or skewing, yet the default `-O3` pipelines of `opt` 18.1.8 and Apple clang 21.0.0 never interchange, fuse or unroll-and-jam (checked with `--print-passes` and `-print-pipeline-passes` on 2026-09-24). LLVM 18's pipeline source adds interchange and unroll-and-jam only under hidden options that default to off,[^llvm-pipelines] and its distribution pass acts only on loops marked with a pragma.[^llvm-distribute]

Run by name, a pass keeps two questions apart: a legality check decides whether it may transform, a cost model decides whether it should, and its remarks say which one refused. The fifth example gives the interchange pass a nest that walks a row-major array column by column. The pass finds the swap legal and profitable and makes it; with `-pass-remarks=loop-interchange` it also says so on standard error.

For profitability the interchange pass asks three models in turn and takes the first answer: loop cache analysis, a count of accesses whose subscripts list the inner loop's variable before the outer loop's, and a test of whether the swap helps vectorization.[^llvm-interchange] Here the first abstains, because nothing in the file gives it a cache line size (`opt -passes='print<loop-cache-cost>'` rates both loops alike), and the second decides.

--8<-- "includes/examples/optimize/p7-loop-transformations/column_walk.ll.md"

A general-purpose compiler works hard for each of these facts; Vortex starts with them: fixed array shapes, subscripts that keep their dimensions ([P6](p6-dependence-analysis.md)), `&mut` for overlap and decision 56 for floating point.

## Measuring the effect

No speedup is claimed here: measure your own, following [P1](p1-measure-first.md).

1. Write the ijk kernel with a running sum, the ikj kernel and the 2 × 2 unroll-and-jam kernel in C++, built with `-O2 -ffp-contract=off` so that each operation rounds once, as in Vortex.
2. Check that all three produce identical bits before timing anything.
3. Read each kernel's vectorization remarks (`-Rpass=loop-vectorize`, `-Rpass-missed=loop-vectorize`): the C++ compiler may vectorize some kernels and not others.
4. Time many separate runs and report the median with a confidence interval, never the best run.
5. Compute GFLOP/s as $2N^3 / t$ for N × N matrices and a time $t$ in nanoseconds.
6. Use two sizes, one whose three matrices fit in your L2 cache and one whose matrices do not ([P2](p2-memory-hierarchy.md)).

| Kernel | N | Median time | 95% interval | GFLOP/s |
| --- | --- | --- | --- | --- |
| ijk, running sum | | | | |
| ikj | | | | |
| 2 × 2 unroll-and-jam | | | | |
| ijk, running sum | | | | |
| ikj | | | | |
| 2 × 2 unroll-and-jam | | | | |

Record the machine, compiler version, flags and date with the table. Tests compare bits; times go in a report.

## For Vortex

!!! vortex "Exercise"

    **Build** the first loop transformations in your compiler, for nests with constant loop bounds and affine subscripts ([P6](p6-dependence-analysis.md)): each index a sum of loop variables times constants, plus a constant.

    1. A loop-nest view of your IR showing, for each nest, whether it is perfect, every loop's trip count, and every array access with its subscript in each dimension.
    2. The two preparation steps from the step-through, returning a running scalar to its element and splitting off the element's zeroing, with their preconditions: the scalar lives for one iteration of the enclosing loops, nothing else in the nest reads the element, and the array is a `&mut` parameter or a local.
    3. Interchange of two loops of a perfect nest, gated by the dependence test from [P6](p6-dependence-analysis.md) and by this chapter's Vortex conditions: every floating-point accumulator keeps its order, no check in the nest can fail (use the range facts from [O8](o8-loops.md), or refuse), and the body calls no `print`.
    4. Unroll-and-jam of the outer loops of a nest by factors given on the command line, with leftover iterations as a remainder loop or straight-line code, gated the same way and by the rule for negative distance entries.
    5. A remark for every decision, applied or refused, naming the loops, the transformation and the reason, such as "not interchanged: `total` is a floating-point sum over both loops".

    Transform only when a flag asks; deciding when to transform comes later.

    **Not yet:** tiling ([P8](p8-cache-blocking.md)), vectorization ([P10](p10-vectorization.md)), threads ([P13](p13-multithreading.md)), anything that regroups floating-point operations (Vortex has no opt-in for it), and a cost model that decides for itself ([P15](p15-choosing-parameters.md)).

    **Proof that it works:**

    - Differential tests: byte-identical output with the transformations on and off, for the stage 10 square and rectangular kernels, and for shapes the factors do not divide, such as a 3 × 5 matrix times a 5 × 7 one.
    - Golden tests of refusals: the `shift` nest, one floating-point sum over a whole matrix, and a nest that calls `print` are left alone, each with a remark that says why.
    - A program whose loop bound overruns an array reports the same runtime error at the same position with the transformations on and off.
    - A measurement of the naive, interchanged and unrolled-and-jammed kernels at two sizes, following [P1](p1-measure-first.md), with the machine, compiler version and date.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a loop transformation change?** The order and grouping of a nest's work, never the work itself.
    - **When is a reordering legal?** When no dependence, rewritten for the new order, has `>` as its first entry that is not `=`.
    - **Why does interchange keep the matrix product's bits but not a whole-grid sum's?** Each element of `c` keeps its additions in order; the single sum's additions are reordered.
    - **What must a Vortex compiler keep besides dependences?** Every floating-point accumulation order, and the first failing check with everything printed before it.
    - **What does unroll-and-jam buy, and what limits it?** Independent sums and reuse of each loaded value, `(mr + nr) / (mr × nr)` loads per multiply-add, until the registers run out.
    - **What is skewing for?** Turning a wavefront into a loop with independent iterations, and making loops tilable.
    - **In what order should they run?** Access order first (line size), then tiling (cache sizes), then register blocking (registers).

## Where this comes back

!!! next "You will use this again in"

    - [O10. Pass managers and pipelines](o10-pass-pipelines.md): *pass order*, *legality and profitability*
    - [P8. Cache blocking](p8-cache-blocking.md): *tiling*, *reuse and locality*, *fully permutable band*
    - [P9. The polyhedral model](p9-polyhedral-model.md): *unimodular matrix*, *skewing*, *schedule*
    - [P10. Vectorization](p10-vectorization.md): *interchange*, *loop fission*
    - [P11. Floating point under optimization](p11-floating-point.md): *accumulation order*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *unroll-and-jam*, *register tiling*, *balance*
    - [P13. Multithreading](p13-multithreading.md): *wavefront*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *unroll factor*, *cost model*
    - [C5. Spilling, splitting and rematerialization](../backend/c5-spilling.md): *register blocking*
    - [M6. Loops: affine and scf](../mlir/m6-affine-and-scf.md): *loop fusion*, *tiling*, *unroll-and-jam*
    - [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): *register tiling*

## Sources and further reading

For depth, read Wolf and Lam on the unimodular framework and tiling, Carr and Kennedy on unroll-and-jam, and McKinley, Carr and Tseng on choosing transformations with a cost model; Bacon, Graham and Sharp survey many more, each with its legality test and an example.[^bgs94]

[^drepper]: Ulrich Drepper, "What Every Programmer Should Know About Memory", version 1.0, 2007, section 6.2.1 and its footnote 28. <https://www.akkadia.org/drepper/cpumemory.pdf>
[^boehm]: Simon Boehm, "Fast Multidimensional Matrix Multiplication on CPU from Scratch", August 2022. <https://siboehm.com/articles/22/Fast-MMM-on-CPU>
[^wl91]: Michael E. Wolf and Monica S. Lam, "A Data Locality Optimizing Algorithm", *Proceedings of the ACM SIGPLAN 1991 Conference on Programming Language Design and Implementation (PLDI)*, 1991, sections 1 to 4. <https://doi.org/10.1145/113445.113449> (free copy: <https://suif.stanford.edu/papers/wolf91a.pdf>)
[^llvm-interchange]: LLVM Project, `LoopInterchange.cpp`, release/18.x branch: the legality theorem in the comment before `isLexicographicallyPositive`, the check in `run(LoopNest &LN)` that the nest is one chain of loops, the comments in `findInductionAndReductions` and `findInnerReductionPhi`, the check for calls, the order of cost models in `isProfitable`, and the pass's optimization remarks. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/LoopInterchange.cpp>
[^cck90]: David Callahan, Steve Carr and Ken Kennedy, "Improving Register Allocation for Subscripted Variables", *Proceedings of the ACM SIGPLAN 1990 Conference on Programming Language Design and Implementation (PLDI)*, 1990, abstract. <https://doi.org/10.1145/93542.93553>
[^ck94]: Steve Carr and Ken Kennedy, "Improving the Ratio of Memory Operations to Floating-Point Operations in Loops", *ACM Transactions on Programming Languages and Systems* 16(6), 1994, sections 1, 2.1, 3.1, 3.2.1 and 3.2.4.1. <https://doi.org/10.1145/197320.197366>
[^mct96]: Kathryn S. McKinley, Steve Carr and Chau-Wen Tseng, "Improving Data Locality with Loop Transformations", *ACM Transactions on Programming Languages and Systems* 18(4), 1996, section 1.1. <https://doi.org/10.1145/233561.233564> (author's copy: <https://www.cs.utexas.edu/~mckinley/papers/toplas-1996.pdf>)
[^llvm-fuse]: LLVM Project, `LoopFuse.cpp`, release/18.x branch: the file header and the `loop-fusion-peel-max-count` option. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/LoopFuse.cpp>
[^llvm-distribute]: LLVM Project, `LoopDistribute.cpp`, release/18.x branch: the file header and the `enable-loop-distribute` option. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/LoopDistribute.cpp>
[^llvm-pipelines]: LLVM Project, `PassBuilderPipelines.cpp`, release/18.x branch: the `enable-loopinterchange` and `enable-unroll-and-jam` options and where the pipelines use them. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Passes/PassBuilderPipelines.cpp>
[^llvm-passes]: LLVM Project, "LLVM's Analysis and Transform Passes", entry `loop-unroll-and-jam`. <https://llvm.org/docs/Passes.html#loop-unroll-and-jam-unroll-and-jam-loops>
[^clang-le]: Clang Project, "Clang Language Extensions", section "Extensions for loop hint optimizations", subsection "Loop Distribution". <https://clang.llvm.org/docs/LanguageExtensions.html#loop-distribution>
[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", section "Reductions". <https://llvm.org/docs/Vectorizers.html#reductions>
[^bgs94]: David F. Bacon, Susan L. Graham and Oliver J. Sharp, "Compiler Transformations for High-Performance Computing", *ACM Computing Surveys* 26(4), 1994. <https://doi.org/10.1145/197405.197406>
