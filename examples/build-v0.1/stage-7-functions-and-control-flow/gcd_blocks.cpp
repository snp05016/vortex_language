// Euclid's algorithm, written as an explicit control-flow graph instead of a
// while loop: each state is one basic block, and moving between states is
// one edge. This is the shape a while loop takes once it is lowered to
// blocks and branches, written out by hand.
//
// Follows: LLVM Project, "LLVM Language Reference Manual", which describes a
// basic block as straight-line code that ends in a single terminator that
// picks the next block.

#include <print>

enum class Block { Header, Body, Done };

int gcd_traced(int a, int b) {
  Block block = Block::Header;
  while (block != Block::Done) {
    switch (block) {
    case Block::Header:
      std::println("  header: a={}, b={}", a, b);
      block = (b != 0) ? Block::Body : Block::Done; // conditional branch
      break;
    case Block::Body: {
      std::println("  body: a={}, b={}", a, b);
      int remainder = a % b;
      a = b;
      b = remainder;
      block = Block::Header; // back edge
      break;
    }
    case Block::Done:
      break; // unreachable: the loop condition already left
    }
  }
  std::println("  done: a={}", a);
  return a;
}

int main() {
  std::println("gcd(48, 18):");
  int result = gcd_traced(48, 18);
  std::println("result = {}", result);
}
