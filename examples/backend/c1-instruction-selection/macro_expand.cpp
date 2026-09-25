// Macro expansion: one fixed template per tree node, chosen by the node's own
// kind alone. The tree is load(a + ((i * 4 + j) << 2)): the byte address of
// element [i][j] of an int32 array whose rows hold 4 elements, then a load.
// Output is AArch64-style assembly with unlimited temporaries t1, t2, ...
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

enum class Op { Var, Const, Add, Mul, Shl, Load };
struct Node {
    Op op;
    std::string name;  // a variable's name, or a constant's digits
    std::vector<Node*> kids;
};

std::deque<Node> arena;  // a deque never moves its elements, so Node* stays valid
Node* var(std::string n) { return &arena.emplace_back(Node{Op::Var, n, {}}); }
Node* num(long v) { return &arena.emplace_back(Node{Op::Const, std::to_string(v), {}}); }
Node* op(Op o, std::vector<Node*> k) { return &arena.emplace_back(Node{o, "", k}); }

int count = 0;

// Returns where the node's value lives. A variable is already in a register.
// Every other node gets its own instruction, even a constant: the template
// for Mul cannot know that its right operand happens to be the constant 4.
std::string expand(Node* n) {
    if (n->op == Op::Var) return n->name;
    std::vector<std::string> in;
    for (Node* k : n->kids) in.push_back(expand(k));
    std::string d = "t" + std::to_string(++count);
    switch (n->op) {
        case Op::Const: std::printf("mov  %s, #%s\n", d.c_str(), n->name.c_str()); break;
        case Op::Add:   std::printf("add  %s, %s, %s\n", d.c_str(), in[0].c_str(), in[1].c_str()); break;
        case Op::Mul:   std::printf("mul  %s, %s, %s\n", d.c_str(), in[0].c_str(), in[1].c_str()); break;
        case Op::Shl:   std::printf("lsl  %s, %s, %s\n", d.c_str(), in[0].c_str(), in[1].c_str()); break;
        case Op::Load:  std::printf("ldr  %s, [%s]\n", d.c_str(), in[0].c_str()); break;
        case Op::Var:   break;
    }
    return d;
}

int main() {
    Node* index = op(Op::Add, {op(Op::Mul, {var("i"), num(4)}), var("j")});
    Node* tree = op(Op::Load, {op(Op::Add, {var("a"), op(Op::Shl, {index, num(2)})})});
    expand(tree);
    std::printf("%d instructions\n", count);
}
