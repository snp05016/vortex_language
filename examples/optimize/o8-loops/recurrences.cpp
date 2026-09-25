// A chain of recurrences {c0,+,c1,+,...,+,cn} lists the values of a polynomial
// at i = 0, 1, 2, ... with additions only. Each step adds every coefficient's
// right neighbour into it, left to right, so c0 becomes the next value; a chain
// of length n costs n additions per step and no multiplication. This is what
// strength reduction does to i * 256 in an address, and what LLVM's scalar
// evolution writes as {start,+,step}.
//
// The coefficients are a table of differences at i = 0. The value at a single
// iteration needs no stepping at all: c0 + c1*C(i,1) + c2*C(i,2) + ..., where
// C is the binomial coefficient. A compiler uses that form to compute what a
// loop leaves behind after its last iteration.
//
// Follows: Bachmann, Wang and Zima, "Chains of Recurrences", ISSAC 1994,
// sections 1, 2 and 2.1; LLVM 18's ScalarEvolution.cpp, evaluateAtIteration.

#include <cstddef>
#include <cstdint>
#include <format>
#include <print>
#include <string>
#include <vector>

using Chain = std::vector<std::int64_t>;

// c[j] becomes the j-th forward difference of p at 0.
Chain chain_of(std::int64_t (*p)(std::int64_t), int degree) {
  Chain c;
  for (int i = 0; i <= degree; ++i) c.push_back(p(i));
  for (int level = 1; level <= degree; ++level)
    for (int j = degree; j >= level; --j) c[j] -= c[j - 1];
  return c;
}

// One step: every coefficient still reads its neighbour's old value.
void step(Chain &c) {
  for (std::size_t j = 0; j + 1 < c.size(); ++j) c[j] += c[j + 1];
}

// The value at iteration i, as a sum of binomial coefficients.
std::int64_t at(const Chain &c, std::int64_t i) {
  std::int64_t value = 0, binomial = 1;  // C(i, 0)
  for (std::int64_t j = 0; j < static_cast<std::int64_t>(c.size()); ++j) {
    value += c[j] * binomial;
    binomial = binomial * (i - j) / (j + 1);  // C(i, j + 1), exact
  }
  return value;
}

std::string show(const Chain &c) {
  std::string s = "{";
  for (std::size_t j = 0; j < c.size(); ++j) s += (j ? ",+," : "") + std::to_string(c[j]);
  return s + "}";
}

void run(const char *name, std::int64_t (*p)(std::int64_t), int degree) {
  const Chain start = chain_of(p, degree);
  Chain c = start;
  std::string values;
  bool same = true;
  for (std::int64_t i = 0; i < 8; ++i, step(c)) {
    values += std::format("{} ", c[0]);
    same = same && c[0] == p(i);
  }
  const std::int64_t last = at(start, 64);
  same = same && last == p(64);
  std::println("{:<12}{:<17}{}| i = 64: {} | {}", name, show(start), values, last,
               same ? "matches" : "DIFFERS");
}

int main() {
  run("4*(64*i+5)", [](std::int64_t i) { return 4 * (64 * i + 5); }, 1);
  run("i*i", [](std::int64_t i) { return i * i; }, 2);
  run("i*(i-1)/2", [](std::int64_t i) { return i * (i - 1) / 2; }, 2);
  run("i*i*i", [](std::int64_t i) { return i * i * i; }, 3);
}
