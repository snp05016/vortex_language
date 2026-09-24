# O9. Memory: alias analysis and MemorySSA

<p class="page-intro">A compiler may keep a loaded value in a register, move a load out of a loop or run four iterations at once only when it knows which stores can change the bytes that load reads. This chapter builds that knowledge: alias queries and their four answers, points-to analysis, the promises a front end writes into the IR, and MemorySSA, which gives memory the SSA form that O3 gave to values. For Vortex it ends at a rule the language already has: storage reached through a <code>&amp;mut</code> parameter is reachable through no other parameter, and that rule removes the runtime checks a C compiler must place in front of the matrix kernel.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 50 minutes · Builds on: [O3. SSA form: construction and destruction](o3-ssa.md), [Build v0.1, stage 8](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying)</p>

???+ remember "Before you start, remember"

    ??? question "Which stack slots can mem2reg turn into SSA values, and which stay in memory?"

        Allocas in the entry block that hold a single value and are used only by direct loads and stores. A slot whose address is passed to a call stays in memory, because the call might read or write it.

        Introduced in [O3. SSA form: construction and destruction](o3-ssa.md#stack-slots-and-mem2reg).

    ??? question "Where does Cytron's method put phis, and how does renaming find the version a use sees?"

        At the iterated dominance frontier of the blocks that assign the variable. Renaming then walks the dominator tree with a stack per variable, so the top of the stack is always the nearest assignment that dominates the current point.

        Introduced in [O3. SSA form: construction and destruction](o3-ssa.md#how-many-phis).

    ??? question "In an analysis of available loads, what does the kernel's store to `c[row, column]` kill?"

        Any available value loaded from `c`, but not the values loaded from `a` or `b`: a variable lent as `&mut` appears in no other argument of the same call (decisions 25 and 41).

        Introduced in [O4. Dataflow analysis](o4-dataflow.md#the-general-recipe).

    ??? question "What does `noalias` on a pointer parameter promise, and when may a front end attach it?"

        That during the call, memory written and reached through that pointer is reached through no other pointer. A front end may attach it only when a rule of the language or a proof stands behind it.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#the-contract-travels-in-the-ir).

    ??? question "What does a Vortex reference become in compiled code, and what may the code rely on?"

        The address where the referenced storage starts; I6 suggests an 8-byte address. The code may rely on References 9.8: storage behind a `&mut` parameter is not reachable through any other parameter of the same call.

        Introduced in [Build v0.1, stage 8](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying).

!!! goals "In this chapter"

    - Explain what an alias query asks, and what the answers NoAlias, MayAlias, PartialAlias and MustAlias, and the mod/ref answers, mean in bytes.
    - Recognize which facts an alias analysis can prove on its own (distinct objects, offsets and sizes, addresses that never escape) and which only a front end can supply.
    - Compare inclusion-based and unification-based points-to analysis on a small program, for precision and for cost.
    - Build MemorySSA for a small function by hand, and find each load's clobber the way MemorySSA's walker does.
    - Trace how Vortex's `&mut` rule removes the runtime alias checks that the vectorizer would otherwise put in front of the matrix kernel.

## One store, one load

Here are three lines of LLVM IR from this chapter's first example:

```llvm
store i32 1, ptr %p
store i32 2, ptr %q
%v = load i32, ptr %p
```

May the compiler replace `%v` with 1? Only if the second store cannot write any of the four bytes the first store wrote. If `%p` and `%q` hold the same address, the load returns 2. If the two four-byte ranges share only some bytes, the load returns a value made partly of one store and partly of the other. The compiler has to decide before the program runs, for every pair of addresses that `%p` and `%q` might hold.

Two memory accesses **alias** when they may touch at least one byte in common. **Alias analysis**, also called pointer analysis, is the family of techniques that try to decide whether two pointers can ever point to the same object in memory.[^llvm-aa] Every optimization that moves, removes or merges a load or a store asks it a question first. [O4](o4-dataflow.md#the-general-recipe) met the question as a kill set: which available loads does a store destroy? [O3](o3-ssa.md#stack-slots-and-mem2reg) met it as a condition on mem2reg: a stack slot whose address is passed to a call cannot become an SSA value, because the call might read or write it.

The same question sits in the middle of the Vortex kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for), at the size O1 to O3 use:

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

The inner loop loads from `a` and `b`, and after it the function stores into `c`. As [stage 8](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying) explains, each reference arrives as an address. If the store to `c[row, column]` could change an element of `a` or `b`, then every load after it must go back to memory, no loaded value may be reused across it, and the four-lane loop of [P10](p10-vectorization.md) is wrong unless something checks the addresses first. In C, where the three arrays would be three `float *` parameters, the compiler has to assume the worst. A Vortex compiler does not, for a reason this chapter builds up to.

## Four answers

An alias query compares two **memory locations**. LLVM describes a location by its start address and its size, and the size matters. LLVM's documentation gives the example of two one-byte stores one byte apart, which do not alias, and shows that widening the first to two bytes makes them alias.[^llvm-aa] A query has four possible answers:[^llvm-aa]

- **NoAlias**: no access made through one pointer ever depends on an access made through the other. Usually the byte ranges never overlap. LLVM also allows the answer when both pointers are only ever used to read.
- **MayAlias**: the two might refer to the same object.
- **PartialAlias**: the two are known to overlap in some way, whether or not they start at the same address.
- **MustAlias**: the two are guaranteed to start at exactly the same address.

<figure class="vx-figure">
<svg viewBox="0 0 760 372" role="img" aria-label="The four answers to an alias query, drawn as the bytes that two accesses touch" aria-describedby="o9-f1-desc">
<title id="o9-f1-title">NoAlias, PartialAlias, MustAlias and MayAlias, in bytes</title>
<desc id="o9-f1-desc">A row of twelve cells across the top numbers bytes 0 to 11 of one object. Below it are four rows, each with two bars for the bytes that two accesses touch. NoAlias: 4 bytes at a cover bytes 0 to 3, and 4 bytes at a plus 4 cover bytes 4 to 7, so no byte is shared. PartialAlias: 4 bytes at a cover bytes 0 to 3, and 2 bytes at a plus 2 cover bytes 2 and 3, which are shared. MustAlias: 4 bytes at the address of a[1] and 4 bytes at a plus 4 both cover bytes 4 to 7. MayAlias: 4 bytes at p cover bytes 0 to 3, and 4 bytes at q are drawn dashed, sliding from bytes 0 to 3 across to bytes 8 to 11, because where q points is unknown.</desc>
<text class="vx-text-muted" x="210" y="20">bytes of one object</text>
<rect class="vx-box" x="210" y="28" width="40" height="22"/>
<rect class="vx-box" x="250" y="28" width="40" height="22"/>
<rect class="vx-box" x="290" y="28" width="40" height="22"/>
<rect class="vx-box" x="330" y="28" width="40" height="22"/>
<rect class="vx-box" x="370" y="28" width="40" height="22"/>
<rect class="vx-box" x="410" y="28" width="40" height="22"/>
<rect class="vx-box" x="450" y="28" width="40" height="22"/>
<rect class="vx-box" x="490" y="28" width="40" height="22"/>
<rect class="vx-box" x="530" y="28" width="40" height="22"/>
<rect class="vx-box" x="570" y="28" width="40" height="22"/>
<rect class="vx-box" x="610" y="28" width="40" height="22"/>
<rect class="vx-box" x="650" y="28" width="40" height="22"/>
<text class="vx-text-muted" x="230" y="44" text-anchor="middle">0</text>
<text class="vx-text-muted" x="270" y="44" text-anchor="middle">1</text>
<text class="vx-text-muted" x="310" y="44" text-anchor="middle">2</text>
<text class="vx-text-muted" x="350" y="44" text-anchor="middle">3</text>
<text class="vx-text-muted" x="390" y="44" text-anchor="middle">4</text>
<text class="vx-text-muted" x="430" y="44" text-anchor="middle">5</text>
<text class="vx-text-muted" x="470" y="44" text-anchor="middle">6</text>
<text class="vx-text-muted" x="510" y="44" text-anchor="middle">7</text>
<text class="vx-text-muted" x="550" y="44" text-anchor="middle">8</text>
<text class="vx-text-muted" x="590" y="44" text-anchor="middle">9</text>
<text class="vx-text-muted" x="630" y="44" text-anchor="middle">10</text>
<text class="vx-text-muted" x="670" y="44" text-anchor="middle">11</text>
<text class="vx-text" x="20" y="90">NoAlias</text>
<text class="vx-text-muted" x="20" y="108">a[0] and a[1]</text>
<rect class="vx-box-accent" x="210" y="74" width="160" height="24" rx="3"/>
<text class="vx-mono" x="290" y="91" text-anchor="middle">4 bytes at a</text>
<rect class="vx-box" x="370" y="104" width="160" height="24" rx="3"/>
<text class="vx-mono" x="450" y="121" text-anchor="middle">4 bytes at a + 4</text>
<text class="vx-text" x="20" y="166">PartialAlias</text>
<text class="vx-text-muted" x="20" y="184">bytes 2 and 3 shared</text>
<line class="vx-line" x1="290" y1="144" x2="290" y2="210" stroke-dasharray="3 3"/>
<line class="vx-line" x1="370" y1="144" x2="370" y2="210" stroke-dasharray="3 3"/>
<rect class="vx-box-accent" x="210" y="150" width="160" height="24" rx="3"/>
<text class="vx-mono" x="290" y="167" text-anchor="middle">4 bytes at a</text>
<rect class="vx-box" x="290" y="180" width="80" height="24" rx="3"/>
<text class="vx-mono" x="330" y="197" text-anchor="middle">2 bytes</text>
<text class="vx-text-muted" x="380" y="197">at a + 2</text>
<text class="vx-text" x="20" y="242">MustAlias</text>
<text class="vx-text-muted" x="20" y="260">same start, every time</text>
<rect class="vx-box-accent" x="370" y="226" width="160" height="24" rx="3"/>
<text class="vx-mono" x="450" y="243" text-anchor="middle">4 bytes at &amp;a[1]</text>
<rect class="vx-box" x="370" y="256" width="160" height="24" rx="3"/>
<text class="vx-mono" x="450" y="273" text-anchor="middle">4 bytes at a + 4</text>
<text class="vx-text" x="20" y="318">MayAlias</text>
<text class="vx-text-muted" x="20" y="336">where q points is unknown</text>
<rect class="vx-box-accent" x="210" y="302" width="160" height="24" rx="3"/>
<text class="vx-mono" x="290" y="319" text-anchor="middle">4 bytes at p</text>
<g class="vx-travel" style="--vx-distance: 320px">
<rect class="vx-box-bad" x="210" y="332" width="160" height="24" rx="3"/>
<text class="vx-mono" x="290" y="349" text-anchor="middle">4 bytes at q</text>
</g>
</svg>
<figcaption>Figure 1. The four answers to an alias query, drawn as the bytes two accesses touch. NoAlias: the accesses never share a byte. PartialAlias: they share some. MustAlias: they start at the same address, here with the same size. MayAlias: nothing is known, so the second access is drawn sliding over the places it might be.</figcaption>
</figure>

The answers are not symmetric in risk. An analysis may always say less than it knows, and MayAlias is its way of saying nothing, so MayAlias is never wrong. A NoAlias that is wrong is a miscompilation. An optimizer therefore treats MayAlias and PartialAlias as "assume the worst". NoAlias tells it that the two accesses are independent, so it may reorder them or keep a loaded value across the store. MustAlias tells it that the two touch the same place, so a value stored by one can be handed to a later load of the same size without going through memory.

Alias queries compare two locations. Many transformations ask a related question about one instruction and one location: may this instruction read the location, may it write it, both, or neither? The answer is **mod/ref information**: **Mod** if the instruction may modify the location, **Ref** if it may read it, **ModRef** for both and **NoModRef** for neither. It is conservative in the same way: an instruction that might read or write the location gets ModRef.[^llvm-aa] An ordinary load is at most Ref for any location. A call to a function the optimizer cannot see is ModRef for almost everything, unless something is known about the memory the callee can reach.

??? check "Classify each pair: (a) 4 bytes at `a` and 4 bytes at `a + 4`; (b) 4 bytes at `a` and 2 bytes at `a + 2`; (c) 4 bytes through a parameter `p` and 4 bytes through a parameter `q`, with nothing else known; (d) 4 bytes of a local variable whose address is never taken and 4 bytes through a parameter."

    (a) NoAlias, by offsets and sizes: bytes 0 to 3 and 4 to 7. (b) PartialAlias: bytes 2 and 3 are shared. (c) MayAlias: two parameters may hold any addresses the caller chooses. (d) NoAlias: a local whose address is never taken can be reached only by name, so no parameter can point into it. The first example below puts all four in front of LLVM, together with a promise that settles (c).

## What an analysis can prove on its own

Some answers need no help from the programmer. LLVM's basic alias analysis, `basic-aa`, is a local analysis that knows, among other facts, that distinct global variables, stack allocations and heap allocations never alias; that different fields of a structure do not alias; that indexes into an array with statically different subscripts cannot alias; and that a call cannot read or modify a stack allocation whose address never escapes the function.[^llvm-aa]

Two ideas carry most of that list. The first is the **identified object**, a pointer whose object the analysis can name. In LLVM 18 that means an `alloca`, a global variable, the result of a call whose return value is marked `noalias` (such as an allocation function), or a parameter marked `noalias` or `byval`.[^aa-src] Two different identified objects never alias, and a plain pointer parameter never aliases an identified object that the function itself created, such as one of its own stack slots.[^aa-src] The second idea is **escape**: an address escapes when the function stores it in memory or passes it to a call that might keep it. Until a local's address escapes, the local's bytes can be reached only through the local itself, which is why the call in O3's mem2reg example kept `%seen` in memory.

The first example gives five functions the question from the start of the chapter: store 1, store 2 somewhere, then load from where the 1 went. It runs EarlyCSE, the dominator-tree walk that [O2](o2-cfg-and-dominance.md#dominance-in-llvm-and-mlir) used to reuse computations, with the option that makes it consult MemorySSA about memory:

--8<-- "includes/examples/optimize/o9-alias-analysis/forwarding.ll.md"

Three loads became the constant 1, each for a different reason. In `@neighbours` the two stores are 4-byte accesses at offsets 0 and 4 from the same pointer, so offsets and sizes decide. In `@local` the slot is an identified object whose address never escapes, and `%q` is a parameter, so identity decides. In `@promised` the `noalias` attribute decides; it is a promise about every call of the function, and the section after next looks at what it promises. In `@unknown` nothing is known, the answer is MayAlias, and the load stays. In `@overlap` the answer is PartialAlias: bytes 2 and 3 now hold the second store's two bytes, and the load stays too.

For Vortex, local reasoning of this kind settles every question about locals. A `let mut` array, or a `sum` that lives in a stack slot because nothing promoted it, is an `alloca`, and no reference parameter can alias it. What local reasoning cannot settle is the question the kernel asks. `a`, `b` and `c` are three parameters: three addresses that the function did not create and knows nothing about.

## Points-to analysis

When a program copies, stores and loads pointers, the object behind an access is no longer visible in the code at hand. A **points-to analysis** computes, for each pointer variable, a **points-to set**: the objects it may point to. Two pointers may alias only if their sets intersect.[^spa] The objects are abstract. A common choice, the allocation-site abstraction, gives one abstract object to each program variable and one to each place in the program that allocates memory.[^spa]

The classic analyses are **flow-insensitive**: they ignore the order of statements and compute one set per variable for the whole program, instead of one per program point. Two of them mark the ends of the range between precision and cost.[^spa]

- **Andersen's analysis** is inclusion-based. It reads `r = p` as a subset constraint: whatever `p` may point to, `r` may point to. The constraints are solved by growing the sets until nothing changes, in cubic time in the worst case.[^spa]
- **Steensgaard's analysis** is unification-based. It treats the same assignment as if it ran both ways, and merges what `p` and `r` point to into one class with union-find.[^spa] Steensgaard presented it in 1996 as an interprocedural analysis with almost linear time cost, whose results equal those of an alias analysis that assumes alias relations are reflexive and transitive.[^steens]

The second example runs both on an eight-statement program:

--8<-- "includes/examples/optimize/o9-alias-analysis/points_to.cpp.md"

Andersen's analysis keeps `p` and `q` apart: `p` can point only to `x`, and `q` only to `y`, so `*p` and `*q` never alias. Steensgaard's analysis cannot keep them apart. The statements `r = p` and `r = q` made the targets of `p`, `q` and `r` one class, so from then on `x` and `y` are one place as far as the analysis knows. The merging spreads: the store `*s = u` merges `z` into the same class, so even `u`, which only ever received `&z`, appears to point to `x` and `y`. Andersen's result is never less precise than Steensgaard's; Møller and Schwartzbach leave the proof as an exercise.[^spa]

The implementations that LLVM's documentation lists for its core are local or specialized ones such as basic-aa; the Steensgaard-style pass it describes, `-steens-aa`, lives in an optional module outside the core.[^llvm-aa] So for a function's pointer parameters, the facts a front end writes into the IR carry most of the weight.

Vortex v0.1 needs none of this machinery. It has no pointer variables to copy. A reference may appear only as a parameter or as the type of a `let` declared without `mut`; it cannot be stored in a struct or an array, and no function can return one ([References 9.7](../specification/references.md#97-lifetimes)). A `let` reference is fixed by its initializer, `&place` or `&mut place`, and a place has one root, a variable the function can name ([9.2 and 9.3](../specification/references.md#93-local-mutability)). So every reference in a function points into one variable of that function, or into the storage its caller lent through one of its reference parameters. Its points-to set has one member, sometimes at an element chosen at run time.

??? check "Which statements of the second example could you delete so that Steensgaard's analysis also proves that `*p` and `*q` never alias?"

    Either `r = p` or `r = q`. Those two are the only statements that merge the targets of `p` and `q`: with one of them gone, `x` and `y` stay in different classes. Deleting `*s = u` does not help, because by then `x` and `y` are already one class. It only keeps `z` out of it.

## Promises the front end writes down

Some facts no analysis of one function can discover, because they depend on every caller. The vectorizer's documentation shows such a loop: a C function takes two `float` pointers `A` and `B` and updates `A[i]` from `B[i]`, and whether the arrays overlap depends on the call.[^llvm-vec] C gives the programmer a way to promise that they do not, the `restrict` qualifier. During each execution of a block that declares a restricted pointer, if an object reached through it is modified, every access to that object in the block must go through that pointer; otherwise the behavior is undefined.[^restrict] Nothing checks the promise. The programmer must keep it, and the compiler is free to ignore it.[^restrict]

LLVM IR carries the same kind of promise in the `noalias` parameter attribute. Memory locations accessed through pointers **based on** the argument are not also accessed, during the call, through pointers not based on it. The guarantee covers only locations that are modified during the call, by any means, and the Language Reference calls the definition intentionally similar to C99's `restrict`.[^langref] The current reference states the consequence outright: any other access is undefined behavior. It also extends the promise to accesses from other threads, unless they happen before the call starts or after it ends.[^langref-now] "Based on" follows how a pointer was computed: the result of a `getelementptr` is based on its pointer operand, and the relation is transitive, so the address of `c[row, column]`, computed from `c`, is based on `c`.[^langref]

Three more ways of stating facts matter here.

- `readonly` on a pointer parameter says the function does not write through that pointer, although it may write the same memory through another.[^langref] LLVM's alias analysis treats memory reached through a parameter that is both `readonly` and `noalias` as unchanging for the whole call.[^llvm-aa]
- **Scoped noalias metadata** attaches `alias.scope` and `noalias` lists to individual loads and stores and says that accesses in one set do not alias accesses in another. When LLVM inlines a function, it turns the callee's `noalias` parameters into such metadata, with a new domain for each inlined copy, so the promise survives inlining.[^langref]
- **Type-based alias analysis**, or TBAA, separates accesses by the types they use. LLVM IR does not attach types to memory, so TBAA does not apply to plain IR; a front end may add TBAA metadata that specialized passes read.[^langref]

LLVM's advice to front-end authors asks for all of these: add `noalias` and related attributes to arguments and return values, use aliasing metadata, especially TBAA, to state facts the optimizer cannot deduce, and mark functions `readonly` or `argmemonly` when known.[^perftips]

Rust's `&mut` makes the same exclusivity guarantee as Vortex's, and rustc marks such references `noalias` in the IR it hands to LLVM, with some exceptions. That became the default only in Rust 1.54, released on 29 July 2021, for LLVM 12 and later;[^rust-154] the change that enabled it gives as its reason that the previously known miscompilations had been resolved.[^rust-pr] A promise is only as good as both ends: the language that makes it and the optimizer that uses it.

For Vortex the promise is a theorem. [References 9.8](../specification/references.md#98-aliasing) says that while a variable is lent as `&mut` it may be used only through that reference, forbids it in any other argument of the same call, and states the consequence: storage reached through a `&mut` parameter is not reachable through any other parameter of that call, and code generation may rely on it. Here is the call that C would accept and Vortex rejects:

```vortex
// program: semantic error
fn add_into(target: &mut [f32; 4], source: &[f32; 4]) {
    for index in 0..4 {
        target[index] += source[index];
    }
}

fn main() {
    let mut values = [1.0, 2.0, 3.0, 4.0];
    add_into(&mut values, &values);   // semantic error: values is borrowed as &mut
}
```

Stage 5 checks the rule before any code exists, so a program that breaks it is rejected instead of miscompiled; C's `restrict` has no such check. [O1](o1-optimizer-contract.md#the-contract-travels-in-the-ir) gave the principle: attach a flag only when a rule of the language or a proof stands behind it. For `noalias` on a `&mut` parameter, a rule does.

??? check "Would TBAA metadata let LLVM separate the kernel's store to `c` from its loads of `a` and `b`?"

    No. TBAA separates accesses of different types, and every access in the kernel reads or writes an `f32`. The same is true of numerical C code whose arrays all hold `float`, which is one reason such code uses `restrict`. In Vortex the separation comes from References 9.8 instead.

## The price of not knowing: runtime checks

When the vectorizer cannot prove that two arrays are disjoint, it can still vectorize. It places code that checks at run time whether the arrays overlap, and runs the original scalar loop if they do.[^llvm-vec] The third example shows the check, and shows it disappear:

--8<-- "includes/examples/optimize/o9-alias-analysis/runtime_checks.ll.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 450" role="img" aria-label="A runtime alias check forks the loop into a vector version and a scalar fallback; with noalias the check disappears" aria-describedby="o9-f2-desc">
<title id="o9-f2-title">Runtime alias checks, with and without noalias</title>
<desc id="o9-f2-desc">At the top, addresses run from left to right. A solid bar marks the 256 bytes of x. A dashed bar marks the 256 bytes of y and slides from the left of x, across it, to its right, because where y lies depends on the caller. Bottom left, the function without noalias: its entry block tests whether y starts before the end of x and x starts before the end of y. If both hold, the ranges overlap and control goes to the scalar loop, one element per trip; otherwise it goes to the vector loop, four elements per trip. Both lead to done. Bottom right, the function with y marked noalias: entry leads straight to the vector loop and then to done, with no check and no scalar loop.</desc>
<defs><marker id="o9-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="740" y="22" text-anchor="end">addresses, left to right</text>
<rect class="vx-box-strong" x="280" y="30" width="200" height="26" rx="3"/>
<text class="vx-mono" x="380" y="48" text-anchor="middle">x: 256 bytes</text>
<g class="vx-travel" style="--vx-distance: 480px">
<rect class="vx-box-bad" x="40" y="64" width="200" height="26" rx="3"/>
<text class="vx-mono" x="140" y="82" text-anchor="middle">y: 256 bytes</text>
</g>
<text class="vx-text-muted" x="20" y="114">Where y lies relative to x depends on the caller.</text>
<text class="vx-text" x="20" y="146">Without noalias</text>
<rect class="vx-box-strong" x="20" y="158" width="350" height="58" rx="4"/>
<text class="vx-text" x="34" y="180">entry: runtime check</text>
<text class="vx-mono" x="34" y="204">y &lt; x + 256 and x &lt; y + 256</text>
<line class="vx-line" x1="100" y1="216" x2="100" y2="266" marker-end="url(#o9-f2-head)"/>
<text class="vx-text-muted" x="108" y="246">both true: overlap</text>
<line class="vx-flow" x1="290" y1="216" x2="290" y2="266" marker-end="url(#o9-f2-head)"/>
<text class="vx-text-accent" x="298" y="246">no overlap</text>
<rect class="vx-box" x="20" y="268" width="160" height="56" rx="4"/>
<text class="vx-text" x="34" y="290">loop</text>
<text class="vx-text-muted" x="34" y="310">1 element per trip</text>
<path class="vx-line" d="M180 282 C 200 282, 200 310, 182 310" marker-end="url(#o9-f2-head)"/>
<rect class="vx-box-accent" x="210" y="268" width="160" height="56" rx="4"/>
<text class="vx-text" x="224" y="290">vector.body</text>
<text class="vx-text-muted" x="224" y="310">4 elements per trip</text>
<path class="vx-line" d="M370 282 C 392 282, 392 310, 372 310" marker-end="url(#o9-f2-head)"/>
<line class="vx-line" x1="100" y1="324" x2="158" y2="374" marker-end="url(#o9-f2-head)"/>
<line class="vx-flow" x1="290" y1="324" x2="232" y2="374" marker-end="url(#o9-f2-head)"/>
<rect class="vx-box" x="115" y="376" width="160" height="40" rx="4"/>
<text class="vx-text" x="195" y="401" text-anchor="middle">done</text>
<text class="vx-text" x="410" y="146">With noalias on y</text>
<rect class="vx-box-strong" x="410" y="158" width="330" height="58" rx="4"/>
<text class="vx-text" x="424" y="180">entry</text>
<text class="vx-text-muted" x="424" y="204">no check: y cannot overlap x</text>
<line class="vx-flow" x1="575" y1="216" x2="575" y2="266" marker-end="url(#o9-f2-head)"/>
<rect class="vx-box-accent" x="495" y="268" width="160" height="56" rx="4"/>
<text class="vx-text" x="509" y="290">vector.body</text>
<text class="vx-text-muted" x="509" y="310">4 elements per trip</text>
<path class="vx-line" d="M655 282 C 677 282, 677 310, 657 310" marker-end="url(#o9-f2-head)"/>
<line class="vx-flow" x1="575" y1="324" x2="575" y2="374" marker-end="url(#o9-f2-head)"/>
<rect class="vx-box" x="495" y="376" width="160" height="40" rx="4"/>
<text class="vx-text" x="575" y="401" text-anchor="middle">done</text>
<text class="vx-text-muted" x="410" y="440">and no scalar copy of the loop</text>
</svg>
<figcaption>Figure 2. What the vectorizer did with the third example. Without a fact about <code>y</code> and <code>x</code>, it kept two versions of the loop and chose between them with two comparisons on entry. With <code>y</code> marked <code>noalias</code>, the check and the scalar copy are gone. The bars are not drawn to scale.</figcaption>
</figure>

The check is two comparisons of addresses. Each array covers 256 bytes, 64 elements of 4 bytes. The ranges overlap exactly when `y` starts before the end of `x` and `x` starts before the end of `y`, and the IR computes both comparisons and their `and`, `%found.conflict`. Keeping two copies of a loop and choosing one at run time is **loop versioning**. LLVM's loop-versioning utility describes a versioned loop as one that speculates that accesses which may alias do not overlap, and emits checks to prove it.[^lver] Inside the vector loop, the vectorizer then records what the check proved: the loads and the store carry `alias.scope` and `noalias` metadata in a domain named `LVerDomain`, so later passes know that, in this copy, the accesses to `y` never touch `x`.

The check costs code size, two copies of the loop, and a few instructions on every entry, and it does not scale. Each pair of arrays that may overlap needs a check when at least one of the two is written, since two reads never conflict, and LLVM 18's loop access analysis generates at most 8 such comparisons by default, a limit set by the hidden option `runtime-memory-check-threshold`.[^laa] With `noalias` on `y`, both costs disappear: `@axpy_noalias` has no check and no scalar loop. Each lane still performs its own `fmul` and then its own `fadd`, so the results are the same bits as the scalar loop's.

The Vortex kernel meets this loop in the ikj order of [P7](p7-loop-transformations.md), where the innermost loop runs over `column` and updates one row of `c` from one row of `b` scaled by `a[row, k]`: an axpy. Unless the compiler knows that `c` is disjoint from `a` and `b`, the vectorized kernel carries a check like this one in front of its inner loop, and the load of `a[row, k]` cannot leave the inner loop, because the store to `c[row, column]` might change it. References 9.8 settles both: with `c` marked `noalias`, the check goes, and the load may leave the loop. Vectorizing along `column` gives each lane a different element of `c` and keeps the order of the additions into every element, so [decision 56](../decisions/numbers.md#d56) holds.

## Memory in SSA form

[O3](o3-ssa.md#your-turn-the-kernel-in-ssa-form) left memory outside SSA form: LLVM keeps loads and stores as they are. Alias analysis compares two accesses, but it does not tell a pass which store a given load depends on. The older tool for that question is LLVM's memory dependence analysis, a lazy, caching layer that walks backwards from an access; the MemorySSA documentation warns that, without great care, using it can result in quadratic-time algorithms, and LLVM is moving its passes from it to MemorySSA.[^llvm-mssa] [^llvm-aa]

**MemorySSA** gives memory an SSA form. It treats the whole of memory as one variable and gives each state of memory a version, with def-use and use-def chains between the accesses.[^llvm-mssa] It lives beside the IR, not inside it: each instruction that touches memory is mapped to one of three kinds of **memory access**.[^llvm-mssa]

- A **MemoryDef** is an operation that may modify memory or that imposes an ordering: a store, a call, a fence, a volatile access, or a load with acquire or stronger ordering. It creates a new version of the whole of memory from exactly one earlier version.
- A **MemoryUse** reads memory without changing it, such as a load or a call that only reads. It names the version it reads.
- A **MemoryPhi** merges versions where control flow joins, like a phi for values. One difference matters: a phi for values merges definitions that must reach it, while a MemoryPhi merges definitions that may, since a version may or may not have changed the bytes a later access reads.

A special MemoryDef, **liveOnEntry**, stands for memory as it was when the function began. It dominates every other access, and a use of it means the memory read was defined before the function started.[^llvm-mssa]

Construction is O3's Cytron method with one variable. LLVM 18 creates an access for every instruction that touches memory, puts a MemoryPhi at the iterated dominance frontier of the blocks that contain MemoryDefs, and renames by walking the dominator tree in preorder, handing each access the nearest version above it.[^mssa-src] In O3's terms that is minimal placement for a single variable. LLVM's documentation describes the same property by saying that MemoryPhis go only where they are needed: a join that no store reaches from two directions gets none.[^llvm-mssa]

<figure class="vx-figure">
<svg viewBox="0 0 760 384" role="img" aria-label="MemorySSA for a small loop, with the walker's answer for each load" aria-describedby="o9-f3-desc">
<title id="o9-f3-title">MemorySSA and its walker on a loop</title>
<desc id="o9-f3-desc">Three blocks. Entry: 1 = MemoryDef(liveOnEntry) for store x, then 2 = MemoryDef(1) for store y. Entry leads to loop. Loop: 4 = MemoryPhi merging 2 from entry and 3 from loop, then MemoryUse(4) for load x, MemoryUse(4) for load y, and 3 = MemoryDef(4) for the store through p. Loop leads back to itself and on to exit. Exit: MemoryUse(3) for load x and MemoryUse(3) for load y. On the right, the walker's answers: the load of x in the loop is clobbered by 1, skipping 3 and 2; the load of y in the loop by the phi 4, because its paths give 2 and 3; the load of x in exit by 1; the load of y in exit by 3. An animated path runs from the load of x in the loop up to store 1 in entry.</desc>
<defs><marker id="o9-f3-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="60" y="16" width="400" height="78" rx="4"/>
<text class="vx-text" x="74" y="36">entry</text>
<text class="vx-mono" x="74" y="60">1 = MemoryDef(liveOnEntry)</text>
<text class="vx-mono" x="340" y="60">store x</text>
<text class="vx-mono" x="74" y="82">2 = MemoryDef(1)</text>
<text class="vx-mono" x="340" y="82">store y</text>
<line class="vx-line" x1="260" y1="94" x2="260" y2="128" marker-end="url(#o9-f3-head)"/>
<rect class="vx-box" x="60" y="130" width="400" height="120" rx="4"/>
<text class="vx-text" x="74" y="150">loop</text>
<text class="vx-mono" x="74" y="174">4 = MemoryPhi({entry,2},{loop,3})</text>
<text class="vx-mono" x="74" y="196">MemoryUse(4)</text>
<text class="vx-mono" x="340" y="196">load x</text>
<text class="vx-mono" x="74" y="218">MemoryUse(4)</text>
<text class="vx-mono" x="340" y="218">load y</text>
<text class="vx-mono" x="74" y="240">3 = MemoryDef(4)</text>
<text class="vx-mono" x="340" y="240">store *p</text>
<path class="vx-line" d="M460 236 C 500 236, 500 150, 462 150" marker-end="url(#o9-f3-head)"/>
<line class="vx-line" x1="260" y1="250" x2="260" y2="284" marker-end="url(#o9-f3-head)"/>
<rect class="vx-box" x="60" y="286" width="400" height="78" rx="4"/>
<text class="vx-text" x="74" y="306">exit</text>
<text class="vx-mono" x="74" y="330">MemoryUse(3)</text>
<text class="vx-mono" x="340" y="330">load x</text>
<text class="vx-mono" x="74" y="352">MemoryUse(3)</text>
<text class="vx-mono" x="340" y="352">load y</text>
<path class="vx-flow" d="M60 192 C 20 192, 20 56, 58 56" marker-end="url(#o9-f3-head)"/>
<text class="vx-text" x="530" y="150">walker's answer</text>
<text class="vx-text-accent" x="530" y="196">1, skipping 3 and 2</text>
<text class="vx-text-accent" x="530" y="218">4: paths give 2 and 3</text>
<text class="vx-text-accent" x="530" y="330">1</text>
<text class="vx-text-accent" x="530" y="352">3</text>
</svg>
<figcaption>Figure 3. MemorySSA for the fourth example's loop, and the walker's answer for each load. The chain on the left is what construction gives: every access names the nearest version above it. The walker climbs the chain and asks alias analysis about each store. The animated path is its walk for the load of <code>x</code> in the loop, which ends at store 1. The address of <code>x</code> is never taken, and <code>p</code> may point to <code>y</code>.</figcaption>
</figure>

The chain in Figure 3 is exact about order and says nothing about bytes. Store 2 names store 1 as the version it starts from, although it writes `y` and store 1 wrote `x`; without further work, every MemoryDef looks as if it clobbers every earlier one.[^llvm-mssa] Here is the fourth example, which builds that chain for the loop in Figure 3 and then answers each load's question with the walker of the next section:

--8<-- "includes/examples/optimize/o9-alias-analysis/memory_ssa.cpp.md"

Why only one variable for all of memory? Splitting memory into many variables, one per object or per field, sounds more precise, and LLVM's documentation explains why LLVM does not do it. Alias analyses can disagree about a pair of accesses; alias answers are not transitive, since `A` NoAlias `B` and `B` NoAlias `C` does not give `A` NoAlias `C`, so a precise partition could need a variable for every pair of accesses that might alias; and many operations, such as calls and copies, may write several parts at once. Experience in other compilers showed that the precision was not worth it, so LLVM partitions memory into one variable and lets queries disambiguate further.[^llvm-mssa] GCC took the same road. Novillo's paper on its Memory SSA describes symbols that stand for regions of memory and two virtual operators, VDEF and VUSE, attached to each statement that loads or stores.[^novillo] According to LLVM's documentation, GCC later changed from several memory partitions to one, as in LLVM.[^llvm-mssa]

## The walker

A **clobber** of an access is an earlier access that may write the bytes it reads. MemorySSA's **walker** answers the question "what is the nearest clobber of this access?" beyond what the chain records. It climbs from the access through the MemoryDefs above it, asks alias analysis about each one, and skips any that cannot touch the location.[^llvm-mssa] For anyone writing a walker, the MemorySSA documentation points out that the defining access of a MemoryDef is always the nearest MemoryDef or MemoryPhi that dominates it, so the defs above an access form a linked list of every possible clobber that dominates it.[^llvm-mssa]

Follow the load of `x` in the loop of Figure 3. It starts at the phi. Along the edge from `entry`, the walker skips store 2, since `y` is not `x`, and stops at store 1, which writes `x`. Along the back edge it skips store 3, since `*p` cannot reach `x`, whose address is never taken, and arrives back at the phi without having met anything that writes `x`. Every path ends at store 1, so the answer is 1: the load reads the same value on every trip around the loop, which makes it a candidate for hoisting. The load of `y` gets store 2 along one edge and store 3 along the other, so the walker stops at the phi. LLVM's walker optimizes phis on the same principle: it checks whether all paths from the starting access reach the same access, and when they do not, it cannot look past the phi.[^mssa-src] Run on the same program written in LLVM IR (`x` an `alloca`, `y` and `p` pointer parameters), LLVM's `print<memoryssa>` prints the same numbering and the same four answers (LLVM 18.1.8 on the owner's M4 Pro, checked on 2026-09-24). On the first example, it shows the three loads that became 1 as `MemoryUse(1)`, optimized past the second store, and the loads in `@unknown` and `@overlap` as `MemoryUse(2)`.

A few details decide what walking costs.

- A MemoryUse keeps one operand. LLVM once optimized every use at build time; the default changed, and a pass that wants optimized uses asks for them with `ensureOptimizedUses()`. A MemoryDef keeps two operands: its defining access, needed to walk the chain, and an optimized access that the walker fills in when asked. Optimizing every MemoryDef takes quadratic time, so it is not done by default.[^llvm-mssa]
- Walks have budgets. By default LLVM 18's walker considers at most 100 stores and phis when it tries to walk past them (the hidden option `memssa-check-limit`),[^mssa-src] and EarlyCSE stops calling the walker after 500 queries in one run, falling back to the unoptimized chain (the hidden option `earlycse-mssa-optimization-cap`).[^early-cse] Past a budget the answers get less precise, never wrong.
- MemorySSA is not the whole truth about memory. Its documentation warns that volatile and atomic operations need their own care: a volatile load that MemorySSA would let a pass hoist must still stay where it is.[^llvm-mssa]

Its users include passes that the next chapters return to. EarlyCSE, in the first example, asks the walker for the clobber of the later of two accesses and treats them as reading the same memory if that clobber dominates the earlier one.[^early-cse] Dead store elimination starts from a store, walks upwards to find an earlier store that it overwrites completely, and checks that nothing reads the location in between.[^dse] LICM, which [O6](o6-redundancy.md) covers, hoists loads that no store in the loop may alias, and promotes a location to a register for the whole loop when every access to it is to the same place and nothing else in the loop may touch it.[^licm] LLVM's documentation names LICM as a user of MemorySSA's update interface.[^llvm-mssa]

## Your turn: the kernel's memory

Take the kernel from the start of the chapter, lowered as in [O2](o2-cfg-and-dominance.md#your-turn-the-kernels-inner-loop) and [O3](o3-ssa.md#your-turn-the-kernel-in-ssa-form), with `sum` promoted to an SSA value. Its memory accesses are few. The k loop's block K3 loads `a[row, k]` and `b[k, column]`. Block X, after the k loop, stores `sum` into `c[row, column]`, adds 1 to `column` and branches back to the column loop's header. Block R, reached when an index check fails, calls the routine that reports the error, and the program exits. Blocks `row.head`, `column.head` and `k.head` are the three loop headers; `column.init` sets `column` to 0 before the column loop, and `row.next` adds 1 to `row`. Number the store 1 and the two phis 2 and 3:

```text
row.head:     2 = MemoryPhi({entry, liveOnEntry}, {row.next, ___})
column.head:  3 = MemoryPhi({column.init, ___}, {X, ___})
K3:           MemoryUse(___)         load a[row, k]
              MemoryUse(___)         load b[k, column]
X:            1 = MemoryDef(___)     store c[row, column]
```

Fill in the five blanks. Say why `k.head` gets no MemoryPhi, and why the call in R, which is a MemoryDef, adds no phi anywhere. Then run the walker for the load of `a[row, k]` twice: once with `a`, `b` and `c` as plain pointer parameters, and once with `c` marked `noalias`. What does each answer allow a later pass to do?

??? check "What fills the blanks, what does the walker return, and what does each answer allow?"

    - `2 = MemoryPhi({entry, liveOnEntry}, {row.next, 3})`, `3 = MemoryPhi({column.init, 2}, {X, 1})`, both loads `MemoryUse(3)`, and `1 = MemoryDef(3)`. The row loop's latch is reached only through the column loop's exit edge, on which memory is version 3, and no block between `column.head` and K3 writes memory, so the nearest version above both loads is 3.
    - The only stores are in X, and X's dominance frontier is `column.head`, whose own frontier adds `row.head`. No MemoryDef is inside the k loop, so `k.head` is in no frontier. R's call is a MemoryDef, but R has no successors, so its version of memory never meets another; its frontier is empty.
    - With plain pointers: from 3, the edge from `column.init` leads to 2, whose paths give liveOnEntry and, along `row.next`, 3 again, which is the phi being walked. The edge from X meets store 1, which may alias `a[row, k]`. Two different answers, liveOnEntry and 1, so the walker stops at 3: the load may see the store made in the previous column iteration.
    - With `c` noalias: store 1 cannot touch `a`, so the walk passes it and comes back to 3. Every path ends at liveOnEntry, and the same holds for `b[k, column]`. Neither array changes while the function runs.
    - With liveOnEntry, a pass may reuse one load of `a[row, k]` for several columns, which is what [P7](p7-loop-transformations.md)'s unroll-and-jam and [P12](p12-fast-gemm.md)'s register blocking need; in the ikj order, where `a[row, k]` does not change inside the column loop, LICM may hoist its load out of that loop. With the phi as the answer, none of this is allowed.

Two of the language's other rules shape the answer. Strict floating point ([decision 56](../decisions/numbers.md#d56)) is untouched by anything in this chapter: alias facts decide which loads may be reused or moved, and a reused load has the same bits as a repeated one. And fixed shapes turn every size in the chapter into a constant: each array covers 16,384 bytes, 64 × 64 elements of 4 bytes, and every offset `row * 64 + k` is known to stay inside it once [O8](o8-loops.md) proves the index ranges.

## For Vortex

!!! vortex "Exercise"

    **Decide first.** Write a design note of at most one page that lists what your compiler will tell the optimizer about memory, and the rule behind each fact. For each item, name the section of the specification or the decision that justifies it, or say why none does:

    1. `noalias` on a `&mut` parameter.
    2. `noalias` on a `&` parameter. Reread the Language Reference's condition about locations modified during the call before you decide.
    3. `readonly` on a `&` parameter.
    4. `nocapture` on reference parameters (the LLVM 18 spelling): can a callee keep a reference after it returns?
    5. A `memory(...)` attribute on each function: which memory can a Vortex function touch, given that v0.1 has no global variables ([Declarations 3.8](../specification/declarations.md#38-excluded-declaration-forms)) and that `print` writes output?
    6. Array parameters passed by value, if [stage 8](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying) led you to pass them by a hidden address.

    The note must also say, for any call, which memory it may read and which it may write. Your mod/ref queries follow from that answer.

    **Then build**, on one of two tracks:

    1. If your back end is LLVM: emit the attributes your note justifies, on definitions and on declarations. Emit nothing the note does not justify ([O1](o1-optimizer-contract.md#the-contract-travels-in-the-ir)).
    2. If your back end is your own: an alias query for your IR. Give every memory access a root (a local variable or a reference parameter), an offset (a constant or unknown) and a size, and answer NoAlias, MayAlias, PartialAlias or MustAlias from the roots, the facts in your note and the constant offsets. Add a mod/ref query for calls that follows your note, and use both in one client: store-to-load forwarding, or keeping an available load across a store in [O4](o4-dataflow.md#the-general-recipe)'s analysis.

    On both tracks, add an optimization remark ([O1](o1-optimizer-contract.md#remarks-the-optimizers-report)) whenever a loop needs no runtime alias check because of a reference rule, naming the rule, for example "no runtime alias checks: `c` is `&mut`, exclusive for the call".

    **Not yet:** a points-to analysis, since v0.1 references cannot be copied into other pointers and each has one root; TBAA metadata; MemorySSA of your own, unless a pass in your own back end already needs clobber queries ([O6](o6-redundancy.md)); dependence analysis between iterations of one loop over one array ([P6](p6-dependence-analysis.md)); relaxing any borrow rule to obtain more facts.

    **Proof that it works:**

    - An IR test on the kernel in ikj order, written by hand in Vortex or produced by [P7](p7-loop-transformations.md)'s interchange, or, if bounds checks still block vectorization, on a small Vortex function that updates a `&mut [f32; 64]` from a `&[f32; 64]`. Run your compiler's IR through `opt -passes='loop-vectorize,simplifycfg' -force-vector-width=4 -force-vector-interleave=1` and check that no runtime check is left (LLVM 18 names the comparison `%found.conflict`) and no scalar copy of the loop. The same test with your attributes switched off, by a compiler option or a hand-edited copy, must show the check.
    - On your own back end, a test in which forwarding or hoisting happens across a store to `c` and does not happen across a store to the same array.
    - `opt -passes='print<memoryssa>' -disable-output` on the kernel shows the loads of `a` and `b` as `MemoryUse(liveOnEntry)`.
    - The References 9.8 error tests still fail in stage 5, among them `add_into(&mut values, &values)` and a `&mut` borrow used while another borrow of the same variable is live. The attributes are only as sound as those checks.
    - Every golden output is byte for byte the same with the attributes on and off, including the stage 10 kernel's known answers.
    - A table filled in from your compiler's output, dated, with your compiler's version:

    | Function | Attributes | Runtime check comparisons | Vectorized? | Clobber of the loads of `a` and `b` |
    | --- | --- | --- | --- | --- |
    | ikj kernel, 64 × 64 | none | | | |
    | ikj kernel, 64 × 64 | from your note | | | |
    | your `&mut` axpy | none | | | |
    | your `&mut` axpy | from your note | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What does an alias query ask, and which answer is always safe?** Whether two locations, each a start address and a size, can share a byte; MayAlias is never wrong.
    - **What can a local analysis such as basic-aa prove without help?** That distinct identified objects never alias, that statically different offsets into one object do not overlap, and that a local whose address never escapes is out of every call's reach.
    - **How do Andersen's and Steensgaard's analyses differ?** Andersen's solves subset constraints, more precise but cubic in the worst case; Steensgaard's merges what assigned pointers point to, in almost linear time, and loses precision.
    - **What does `noalias` promise, and why can Vortex make the promise safely?** That locations modified through one pointer are not accessed through other pointers during the call; References 9.8 guarantees it for `&mut`, and stage 5 checks it.
    - **What does a vectorizer do when it cannot prove two arrays disjoint?** It versions the loop: an overlap check on entry chooses between the vector loop and the original scalar loop.
    - **What are MemoryDef, MemoryUse and MemoryPhi?** A new version of all memory, a read of one version, and a merge of versions at a join; all of memory is one variable.
    - **What does MemorySSA's walker return?** The nearest access above that may write the location, after skipping stores that cannot touch it and phis whose incoming paths all agree.

## Where this comes back

!!! next "You will use this again in"

    - [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md): *store-to-load forwarding*, *scalar promotion*, *clobber queries*
    - [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md): *noalias scope metadata*, *escaping addresses*
    - [O10. Pass managers and pipelines](o10-pass-pipelines.md): *cached analyses*, *preserving MemorySSA*
    - [O11. Undefined behavior, poison and correct optimization](o11-undefined-behavior.md): *a broken `noalias` promise*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *attributes on and off*, *golden outputs*
    - [P6. Dependence analysis](p6-dependence-analysis.md): *two accesses to one array*
    - [P7. Loop transformations](p7-loop-transformations.md): *legality from `&mut`*, *ikj order*
    - [P10. Vectorization](p10-vectorization.md): *runtime alias checks*, *loop versioning*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *reusing loads across columns*
    - [P13. Multithreading](p13-multithreading.md): *exclusive access across threads*
    - [C6. Instruction scheduling](../backend/c6-scheduling.md): *memory dependences between loads and stores*
    - [M7. Bufferization](../mlir/m7-bufferization.md): *aliasing buffers*

## Sources and further reading

Read LLVM's two short documents first, the alias analysis infrastructure and MemorySSA, with the first and fourth examples open beside them. Chapter 11 of Møller and Schwartzbach's *Static Program Analysis* is the clearest account of points-to analysis, and Steensgaard's paper is worth reading for how a type system becomes an alias analysis. For the price of missing facts, read the vectorizer's section on runtime checks next to the third example's output.

[^llvm-aa]: LLVM Project, "LLVM Alias Analysis Infrastructure", sections "Introduction", "Representation of Pointers", "Must, May, and No Alias Responses", "The getModRefInfo methods", "The getModRefInfoMask method", "The -basic-aa pass", "The -steens-aa pass" and "Memory Dependence Analysis", read on 2026-09-24. <https://llvm.org/docs/AliasAnalysis.html>
[^aa-src]: LLVM Project, `AliasAnalysis.cpp` (`isIdentifiedObject`, `isIdentifiedFunctionLocal`) and `BasicAliasAnalysis.cpp` (`BasicAAResult::aliasCheck`), release/18.x branch. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/AliasAnalysis.cpp> and <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/BasicAliasAnalysis.cpp>
[^spa]: Anders Møller and Michael I. Schwartzbach, *Static Program Analysis*, Aarhus University, online edition read on 2026-09-24: chapter 11, "Pointer Analysis", sections 11.1 to 11.3, with section 3.3 (unification in almost linear time) and section 10.2 (the cubic algorithm). <https://cs.au.dk/~amoeller/spa/>
[^steens]: Bjarne Steensgaard, "Points-to Analysis in Almost Linear Time", *Proceedings of the 23rd ACM Symposium on Principles of Programming Languages (POPL 1996)*: abstract and section 1. <https://doi.org/10.1145/237721.237727> (course copy: <https://www.cs.cornell.edu/courses/cs711/2005fa/papers/steensgaard-popl96.pdf>)
[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", section "Runtime Checks of Pointers", read on 2026-09-24. <https://llvm.org/docs/Vectorizers.html#runtime-checks-of-pointers>
[^restrict]: cppreference.com, "restrict type qualifier (since C99)", the description and the notes, read on 2026-09-24. <https://en.cppreference.com/w/c/language/restrict>
[^langref]: LLVM Project, "LLVM Language Reference Manual", version 18.1.8: parameter attributes `noalias`, `nocapture` and `readonly`; function attribute `memory(...)`; sections "Pointer Aliasing Rules" and "'noalias' and 'alias.scope' Metadata". <https://releases.llvm.org/18.1.8/docs/LangRef.html>
[^langref-now]: LLVM Project, "LLVM Language Reference Manual", current edition, parameter attribute `noalias`, read on 2026-09-24. <https://llvm.org/docs/LangRef.html>
[^perftips]: LLVM Project, "Performance Tips for Frontend Authors", sections "Describing Aliasing Properties" and "Modeling Memory Effects". <https://llvm.org/docs/Frontend/PerformanceTips.html>
[^rust-154]: The Rust Project, `RELEASES.md`, "Version 1.54.0 (2021-07-29)", compiler changes. <https://github.com/rust-lang/rust/blob/master/RELEASES.md>
[^rust-pr]: The Rust Project, pull request 82834, "Enable mutable noalias for LLVM >= 12", merged 2021-03-22. <https://github.com/rust-lang/rust/pull/82834>
[^lver]: LLVM Project, `LoopVersioning.cpp`, release/18.x branch: the file header and the alias scope domain `LVerDomain`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Utils/LoopVersioning.cpp>
[^laa]: LLVM Project, `LoopAccessAnalysis.cpp`, release/18.x branch: the file header and the option `runtime-memory-check-threshold`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/LoopAccessAnalysis.cpp>
[^llvm-mssa]: LLVM Project, "MemorySSA", sections "Introduction", "MemorySSA Structure", "The walker", "Locating clobbers yourself", "Use and Def optimization", "Invalidation and updating", "Phi placement", "Non-Goals" and "Precision", read on 2026-09-24. <https://llvm.org/docs/MemorySSA.html>
[^mssa-src]: LLVM Project, `MemorySSA.cpp`, release/18.x branch: `buildMemorySSA`, `placePHINodes`, `renamePass`, the comment on `tryOptimizePhi`, and the option `memssa-check-limit`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/MemorySSA.cpp>
[^novillo]: Diego Novillo, "Memory SSA: A Unified Approach for Sparsely Representing Memory Operations", sections 1, 2 and 2.1 (undated paper; the PDF was produced in May 2007). <https://www.airs.com/dnovillo/Papers/mem-ssa.pdf>
[^early-cse]: LLVM Project, `EarlyCSE.cpp`, release/18.x branch: `isSameMemGeneration` and the option `earlycse-mssa-optimization-cap`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/EarlyCSE.cpp>
[^dse]: LLVM Project, `DeadStoreElimination.cpp`, release/18.x branch: the file header. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/DeadStoreElimination.cpp>
[^licm]: LLVM Project, `LICM.cpp`, release/18.x branch: the file header on hoisting loads and on scalar promotion. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/LICM.cpp>
