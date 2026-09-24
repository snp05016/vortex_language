# Language decisions

<p class="page-intro">The v0.1 documents left 56 questions open or answered them in two different ways. Each record below settles one: what the question was, what the documents said, what other languages do, the decision, and what it changed.</p>

The [specification](../specification/index.md) states the rules; these records explain why each rule was chosen. When the two disagree, the specification wins (see [document authority](../specification/conformance.md#11-document-authority)), and the disagreement is a documentation bug.

**Status.** Every record below is *Accepted*, decided on 2026-09-23 by applying the recommendations of the open-questions review. A record can be revisited: a new record then replaces it, and the old one links to its replacement.

The [implementation choices](implementation.md) page is different: it lists eleven questions the compiler guide leaves to the implementer, each with a suggested default. They are not language rules.

## [Lexical structure and source text](lexical.md)

| Record | Decision |
| --- | --- |
| [15. Source encoding](lexical.md#d15) | Source must be UTF-8; leading BOM ignored; stray controls and non-ASCII code rejected; char is one scalar value. |
| [17. Malformed numeric literals and comment markers](lexical.md#d17) | A number is read as one run; invalid runs, leading zeros, 1e5, /* and */ are lexical errors. |
| [18. `true` and `false` token kind](lexical.md#d18) | true and false are keywords denoting the bool values; keyword or boolean-literal token kind both conform. |
| [19. Lexical error list](lexical.md#d19) | Lexical structure 2.7 is the complete normative list of eight lexical errors; diagnostics links to it. |
| [29. Reserved words and `as`](lexical.md#d29) | Reserve i8, i16, i64, u8, u16, u64, f16, bf16, const; using one is a lexical error; no as. |

## [Names and scopes](names.md)

| Record | Decision |
| --- | --- |
| [2. No shadowing](names.md#d2) | A declaration must not reuse any visible name; parameters share the body's scope; locals are visible after their declaration. |
| [3. Declaration order](names.md#d3) | Top-level names are visible file-wide, so calls to later functions, recursion and mutual recursion are allowed. |
| [5. One namespace for top-level names](names.md#d5) | Functions, structs and print share one namespace; a clash is a name error; fields are separate. |

## [Numbers, literals and casts](numbers.md)

| Record | Decision |
| --- | --- |
| [1. Cast syntax](numbers.md#d1) | Casts use their own grammar production: a numeric type keyword and one parenthesized operand; a cast is not a call. |
| [24. Floating-point special values](numbers.md#d24) | Float division by zero follows IEEE 754 without error; NaN is unordered; casting NaN or infinity to integers fails. |
| [27. Cast conversion matrix](numbers.md#d27) | Numeric-only casts; integer casts are range-checked; float-to-integer truncates and fails on NaN, infinity or overflow; no saturation. |
| [30. "Compatible" types and implicit conversion](numbers.md#d30) | Compatible means the identical type after literal typing; Vortex v0.1 has no implicit conversions anywhere. |
| [31. Literal typing contexts](numbers.md#d31) | A literal takes a typed peer's type, else the expected type, else i32 or f32; it never changes kind. |
| [32. Literal range and float rounding](numbers.md#d32) | Out-of-range literals are type errors at the literal; floating literals round once, to nearest-even, to their final type. |
| [42. `usize` width](numbers.md#d42) | The width of usize is implementation-defined and documented; every v0.1 target uses 64 bits. |
| [56. Floating-point strictness](numbers.md#d56) | Each float operation is one IEEE 754 operation rounded to nearest-even; no fusion, reordering, wider precision or flush-to-zero. |

## [Operators, structs and void](operators.md)

| Record | Decision |
| --- | --- |
| [7. Struct expression errors and kind misuse](operators.md#d7) | Missing, unknown or repeated fields and wrong-kind name uses are type errors; unresolved struct names are name errors. |
| [22. Shift semantics](operators.md#d22) | Any integer count type, checked 0 to width minus 1; `<<` drops bits; `>>` arithmetic for signed types. |
| [26. Empty structs](operators.md#d26) | Every struct must declare at least one field; `struct Marker {}` is a syntax error. |
| [33. Integer division and remainder](operators.md#d33) | `/` truncates toward zero; `%` takes the dividend's sign; `MIN / -1` overflows; `MIN % -1` is 0. |
| [35. Equality and ordering per type](operators.md#d35) | `==` and `!=` on same-typed bool, char, integers, floats; ordering on integers and floats; nothing else compares. |
| [37. Chained comparisons and bitwise precedence](operators.md#d37) | Comparisons neither chain nor mix without parentheses (a syntax error); C's bitwise precedence stays, with a hint. |
| [44. `void` values](operators.md#d44) | `void` only as a return type; `void` calls only as expression statements; anything else is a type error. |
| [45. Recursive structs](operators.md#d45) | A struct containing itself by value, directly or through arrays or other structs, is a type error. |

## [Arrays and shapes](arrays.md)

| Record | Decision |
| --- | --- |
| [10. Zero-length arrays](arrays.md#d10) | Every extent must be at least 1; a zero extent, written or computed, is a constant-evaluation error. |
| [11. Constant dimension expressions](arrays.md#d11) | Dimensions must be integer constant expressions: literals, unary minus, parentheses, + - * / %; names and calls never count. |
| [12. Index type and bounds](arrays.md#d12) | Any integer type indexes; 0 <= i < extent; constant indexes fail at compile time, others at run time. |
| [21. Nested literal for a multidimensional array](arrays.md#d21) | A list of lists fills [T; d1, ..., dn] only with that expected type; nested and multidimensional types never convert. |
| [43. Memory layout](arrays.md#d43) | Arrays are contiguous and row-major; struct fields in declaration order; sizes, alignment and padding implementation-defined. |
| [47. Index arity and partial indexing](arrays.md#d47) | The index count must equal the rank; partial indexing is a type error; nested arrays take one suffix per level. |
| [52. When dimensions are evaluated](arrays.md#d52) | Dimensions are evaluated when the type checker resolves array types; later constant evaluation handles only item 39's operations. |

## [Statements and control flow](statements.md)

| Record | Decision |
| --- | --- |
| [8. Return errors](statements.md#d8) | Return mismatches (missing value, value in void function, wrong type) are type errors; a non-terminating body is semantic. |
| [9. Terminating statements and reachability](statements.md#d9) | Structural terminating-statement rule: return, last statement of a block, if with else; loops never terminate. |
| [13. `for` loop semantics](statements.md#d13) | Same-type integer endpoints evaluated once; immutable fresh loop variable; empty when start is past end; no overflow at maximum. |
| [36. Ranges outside `for`](statements.md#d36) | Grammar unchanged; a range is valid only as a for iterable, elsewhere a type error; arrays as iterables planned. |
| [38. Evaluation order](statements.md#d38) | Strict left-to-right evaluation; assignments evaluate place, then value, then bounds check, then store; checks fire at their operation. |

## [References and mutability](references.md)

| Record | Decision |
| --- | --- |
| [23. Parameters and mutable places](references.md#d23) | Parameters are immutable; a place is mutable only when rooted at a `let mut` variable or a `&mut` reference. |
| [25. Value semantics](references.md#d25) | Initialization, assignment, argument passing and return copy whole values; no moves; a `&mut` argument's variable cannot appear in other arguments. |
| [40. Reading and writing through references](references.md#d40) | A reference name reads and writes its referent implicitly; no `*` operator, no rebinding; indexing and fields reach through. |
| [41. Where references may appear, and how long a borrow lasts](references.md#d41) | References only as parameters and immutable `let` bindings; lexical borrows; whole-variable conflicts are semantic errors; misplacement is a type error. |

## [Diagnostics and runtime checks](diagnostics.md)

| Record | Decision |
| --- | --- |
| [16. Source positions](diagnostics.md#d16) | Spans are byte offsets and lengths; lines and columns count from 1; columns count characters; CR LF is one break. |
| [34. Checked integer operations](diagnostics.md#d34) | Normative list of checked integer operations: + - *, negation, / %, compound assignment, integer casts and shift counts. |
| [39. Provable runtime failures](diagnostics.md#d39) | Checks with integer-constant deciding operands fail at compile time; all others at run time; never reject provable runtime failures. |
| [46. Stack exhaustion](diagnostics.md#d46) | Stack exhaustion must stop the program with an implementation-limit report (kind stack, exit 101); stack size is documented. |
| [48. Warnings](diagnostics.md#d48) | Warnings are optional, never required, and never change acceptance, exit status or output; conformance tests ignore them. |

## [Programs, output and the driver](program.md)

| Record | Decision |
| --- | --- |
| [4. What `print` writes](program.md#d4) | Built-in variadic print for primitives and String: space-separated, newline-terminated, stdout, shortest round-trip floats. |
| [6. The `main` check](program.md#d6) | Name resolution checks main; missing main or wrong signature is one semantic error; duplicate main is one name error. |
| [14. Exit status and runtime error output](program.md#d14) | Exit 0 normally; a runtime error flushes stdout, writes one stderr line with kind and position, exits 101. |
| [20. The `vortex` command line](program.md#d20) | vortex <source> [-o out | --tokens | --ast]; diagnostics on stderr; exit 0, 1 for source errors, 2 for usage or I/O. |

## [How the documents work](documentation.md)

| Record | Decision |
| --- | --- |
| [28. Example labels](documentation.md#d28) | Every vortex block starts with a kind and result label; checkers complete statements and items blocks and test them. |
| [49. One statement of document authority](documentation.md#d49) | Conformance 1.1 is the only authority statement; specification chapters are normative, every other page is informative. |
| [50. What "Planned" means](documentation.md#d50) | Planned means not part of v0.1 and must be rejected; unbuilt v0.1 behavior is 'Specified, not yet implemented'. |
| [51. The "Static error" label](documentation.md#d51) | The example label 'Semantic error' becomes 'Static error'; each example names its exact diagnostic category. |
| [53. Literal storage in the cheat sheet](documentation.md#d53) | Cheat sheet stores char literals as char32_t and keeps numeric spellings until typing, then converts once. |
| [54. Ten wording corrections](documentation.md#d54) | Ten tour, cheat sheet and home page passages are corrected to match the specification. |
| [55. The list of implementation-defined behavior](documentation.md#d55) | New Conformance 1.10 lists every implementation-defined behavior and limit; everything else is fully specified. |
