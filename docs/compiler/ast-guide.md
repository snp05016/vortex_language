# Building the Vortex AST: a C++ learning guide

This guide explains the C++ ideas needed to build the Vortex abstract syntax
tree (AST). It is a learning path, not an implementation claim or a replacement
for the [grammar](../specification/grammar.md) and
[parser design](parser-design.md). The current
[`ast.h`](https://github.com/snp05016/vortex_language/blob/main/src/frontend/ast.h)
is work in progress; examples here show the intended relationships, not an API
that already compiles unchanged in this repository.

## 1. What the AST represents

The lexer turns characters into tokens. The parser recognizes how tokens fit
together and creates objects that preserve the *meaningful structure* of the
source. Those objects form a tree.

```vortex
// statements: valid
let result = 2 + 3 * 4;
```

```text
VarDeclStmt(result)
└── initializer: BinaryExpr(+)
    ├── left: Literal(2)
    └── right: BinaryExpr(*)
        ├── left: Literal(3)
        └── right: Literal(4)
```

The AST records that multiplication belongs under addition. The parser does
not need separate AST nodes for `let`, `=`, or `;`: the declaration node and
its fields already represent their structural roles. Keep a source location
on nodes so later errors can point back to the original text.

**Checkpoint:** Draw the tree for `2 * (3 + 4)`. Which operator is the root?

## 2. A type is a blueprint; an object is one instance

```cpp
struct IntegerLiteral {
    int value;
};

IntegerLiteral first{2};
IntegerLiteral second{3};
```

`IntegerLiteral` is a C++ type. `first` and `second` are two separate objects
with the same shape and different stored values. In a compiler, each object
represents one particular occurrence in the source. A *constructor* runs when
an object is created and initializes its data. The parser decides **when** to
create an AST object; the constructor only initializes it.

**Checkpoint:** If a file contains `2 + 2`, how many literal occurrences must
the AST represent?

## 3. Categories versus concrete nodes

Vortex's base categories are `Expr`, `Stmt`, `Decl`, and `Type`. They answer
different questions:

| Category | Represents | Example |
| --- | --- | --- |
| `Expr` | Something used as a value | `a + 2` |
| `Stmt` | An action or control-flow step | `return a;` |
| `Decl` | A named program-level definition | `fn add(...) {}` |
| `Type` | A written type | `[i32; 4]` |

`BinaryExpr` is a specific kind of `Expr`; `ReturnStmt` is a specific kind of
`Stmt`. In C++, `struct BinaryExpr : public Expr` expresses that relationship.
The base category provides common behavior, such as a source location. A
concrete node stores the details unique to that syntax, such as an operator
and two operands.

A protected base constructor prevents unrelated code from constructing a
meaningless base node while allowing a derived constructor to forward its
location:

```cpp
struct Expr : Node {
protected:
    explicit Expr(SourceLocation where) : Node(where) {}
};
```

This is an illustration of *constructor chaining*: constructing a derived
object first initializes its base part. See `ast.h` for the actual naming and
access choices.

**Checkpoint:** Would `return 2;` as a whole be an expression or a statement?
What part of it is an expression?

## 4. A C++ pointer is an address; ownership is a separate question

As in C, `Expr*` holds an address. The pointer type alone does **not** tell you
who must destroy the object at that address. If two parents both delete the
same child, there is a double delete. If nobody deletes it, there is a leak.

For this AST, use one owner per child. `std::unique_ptr<Expr>` says that one
object owns an expression node and destroys it automatically when ownership
ends. The pointed-to object can still be a derived kind such as `Literal` or
`BinaryExpr`.

```text
BinaryExpr owns left child
           owns right child
```

An ordinary local object is destroyed when its scope ends. The parser must be
able to return a node that remains alive after the parsing function ends. A
returned `unique_ptr` transfers ownership to its caller rather than leaving
the caller with an address and an unclear cleanup duty.

**Checkpoint:** If `parse_primary()` returns a `unique_ptr<Expr>`, who owns the
node after the caller stores that returned pointer?

## 5. Why child expressions cannot be stored as `Expr` values

An expression child could be a literal, call, unary expression, or another
binary expression. A field declared as `Expr child;` has space only for the
`Expr` base portion. Passing a derived object into it **slices off** its
derived data. `std::vector<Expr>` has the same problem for a collection.
`std::move` does not fix slicing: moving into an `Expr` value still constructs
an `Expr` value.

The current `ast.h` uses by-value `Expr` children in several places. Treat
those declarations as work in progress, not as the pattern to repeat. A
heterogeneous child needs indirection, normally `std::unique_ptr<Expr>`; a
heterogeneous list needs `std::vector<std::unique_ptr<Expr>>`.

```cpp
// Shape only: not a complete replacement for the current BinaryExpr.
std::unique_ptr<Expr> left;
std::unique_ptr<Expr> right;
std::vector<std::unique_ptr<Expr>> arguments;
```

By contrast, plain non-polymorphic data can live directly in the parent:
operator enums, booleans, names, and parameter records are examples. Use a
pointer because the child can have different concrete node types, not because
every piece of AST data must be allocated separately.

**Checkpoint:** Why is `std::vector<ParamDecl>` reasonable for function
parameters while `std::vector<Expr>` is unsuitable for call arguments?

## 6. Constructing and transferring one child

The parser can make a concrete node and return it as a pointer to the general
category. This abbreviated example omits constructors and source locations:

```cpp
std::unique_ptr<Expr> child = std::make_unique<Literal>(/* arguments */);
std::unique_ptr<Expr> parent =
    std::make_unique<Unary>(/* operator */, std::move(child));
```

`std::make_unique` creates the object and returns its owning pointer.
`std::move(child)` lets the `Unary` node take ownership. It does not physically
move the entire `Literal` object; it transfers the pointer's ownership. After
that transfer, do not use `child` as though it still owns the node. The new
parent keeps the child alive.

An owning node's constructor should accept the required children. That makes
it harder to create a half-initialized node. Optional syntax is different:
`return;` has no expression, whereas `return value;` does. Document exactly
which child pointers may be empty and what an empty pointer means.

**Checkpoint:** In the example, which pointer owns the literal after the
`Unary` constructor takes it?

## 7. Locations belong to complete constructs

`SourceLocation` currently stores a start offset and length. The
specification measures both in bytes, and a diagnostic turns the offset into a
line and column
([Conformance 1.7](../specification/conformance.md#17-source-locations)). A
literal node's span covers its literal text. For `2 + 3`, the binary node's
span should cover the complete expression, while its two children keep their
own smaller spans. This lets a diagnostic point at either the whole operation
or a particular operand. The token for `+` alone is not the complete
binary-expression span.

The parser chooses the span when it has recognized the full construct. The
base `Node` constructor stores it. The `location()` accessor later lets
diagnostics read it without changing the node.

**Checkpoint:** For `(2 + 3) * 4`, should the root node's span cover just `*`
or the whole expression?

## 8. Build one vertical slice before the whole AST

Work through these checkpoints in order. Keep the existing code untouched
until you can explain the ownership and tree shape for each step.

1. **One literal:** Represent `900`; inspect its value and location.
2. **One unary expression:** Represent `-900`; the unary node owns the literal.
3. **One binary expression:** Represent `2 + 3`; it owns two expressions.
4. **Nested precedence:** Represent `2 + 3 * 4`; verify multiplication is the
   addition node's right child.
5. **One statement:** Represent `let result = 2 + 3 * 4;`; its initializer is
   an expression child.
6. **One function:** Represent `fn main() { ... }`; its body owns an ordered
   sequence of statements.

Only after you can manually construct and print these trees should the parser
produce them. The parser turns tokens into nodes; later passes resolve names,
check types and mutability, and generate executable behavior. An AST node for
`2 + true` can be structurally valid even though type checking later rejects
the operation.

The [parser design](parser-design.md) maps the remaining grammar constructs to
parser functions. The [cheat sheet](../language-and-compiler-cheatsheet.md)
defines terms you encounter along the way.

## Answers to the checkpoints

1. For `2 * (3 + 4)`, multiplication is the root; addition is its right child.
2. `2 + 2` has two literal occurrences, so it needs two literal nodes.
3. `return 2;` is a statement; its child `2` is an expression.
4. The caller owns the node through the returned `unique_ptr`.
5. `ParamDecl` is a fixed record type; expressions can have many derived
   concrete types, so storing them as base `Expr` values would slice them.
6. The `Unary` node owns the literal after the ownership transfer.
7. The root span should cover the complete expression.
