// Two fixed-length term-count vectors compared by cosine similarity. Every
// vector is taken by const reference: the function only reads its inputs, the
// same shape the matrix multiplication stage asks for its own arguments.
// The function's parameter type, std::array<int, 4>, fixes the length the
// same way a Vortex function's array type fixes a shape: to compare vectors
// of a different length, write another function.
//
// Follows: cppreference std::inner_product and std::array.

#include <array>
#include <cmath>
#include <numeric>
#include <print>

using TermCounts = std::array<int, 4>;

double cosine_similarity(const TermCounts& a, const TermCounts& b) {
  const double dot = std::inner_product(a.begin(), a.end(), b.begin(), 0.0);
  const double norm_a = std::sqrt(
      std::inner_product(a.begin(), a.end(), a.begin(), 0.0));
  const double norm_b = std::sqrt(
      std::inner_product(b.begin(), b.end(), b.begin(), 0.0));
  return dot / (norm_a * norm_b);
}

int main() {
  // Counts of four words ("cat", "dog", "fish", "bird") in two documents.
  const TermCounts document_a{3, 1, 0, 0};
  const TermCounts document_b{2, 0, 0, 1};
  const TermCounts document_c{0, 0, 4, 2};

  std::println("a vs b: {:.4f}", cosine_similarity(document_a, document_b));
  std::println("a vs c: {:.4f}", cosine_similarity(document_a, document_c));
  std::println("a vs a: {:.4f}", cosine_similarity(document_a, document_a));

  // A three-word vector has a different type, so this call does not compile:
  // cosine_similarity(document_a, std::array<int, 3>{1, 2, 3});
}
