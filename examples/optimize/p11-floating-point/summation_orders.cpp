// Two inputs, each summed four ways in f32, against a double-precision
// reference. The first input is built to fail: one value at 2^24, where the
// gap between neighbouring floats is 2, followed by 99,999 ones. The second
// is ordinary: the terms 1, 1/2, 1/3, ..., 1/100000 of the harmonic series.
//
// Follows: Goldberg, "What Every Computer Scientist Should Know About
// Floating-Point Arithmetic", ACM Computing Surveys 23(1), 1991 (Theorem 8,
// the Kahan summation formula, and the error bound for the plain sum).

#include <cmath>
#include <print>
#include <vector>

float sequential(const std::vector<float>& v) {
  float s = 0.0f;
  for (float x : v) s += x;
  return s;
}

// The shape a vectorizer builds under `reassoc`: lane j adds elements
// j, j+4, j+8, ..., and the four partial sums are combined at the end.
float four_lanes(const std::vector<float>& v) {
  float lane[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  for (std::size_t i = 0; i < v.size(); ++i) lane[i % 4] += v[i];
  return (lane[0] + lane[1]) + (lane[2] + lane[3]);
}

float pairwise(const float* v, std::size_t n) {
  if (n <= 8) {
    float s = 0.0f;
    for (std::size_t i = 0; i < n; ++i) s += v[i];
    return s;
  }
  std::size_t half = n / 2;
  return pairwise(v, half) + pairwise(v + half, n - half);
}

float kahan(const std::vector<float>& v) {
  float sum = 0.0f, carry = 0.0f;  // carry: the low part the last add lost
  for (float x : v) {
    float y = x - carry;
    float t = sum + y;
    carry = (t - sum) - y;
    sum = t;
  }
  return sum;
}

void report(const char* name, const std::vector<float>& v) {
  double exact = 0.0;  // 53 bits: far more than these sums need
  for (float x : v) exact += x;
  // Errors are printed in units of the f32 spacing next to the exact sum.
  float near = static_cast<float>(exact);
  double ulp = std::nextafter(near, INFINITY) - near;
  auto show = [&](const char* how, float got) {
    std::println("  {:<10} {:>12.9g}  error {:>9.1f} ulp", how, got,
                 (got - exact) / ulp);
  };
  std::println("{}: exact {:.10g}", name, exact);
  show("sequential", sequential(v));
  show("four lanes", four_lanes(v));
  show("pairwise", pairwise(v.data(), v.size()));
  show("kahan", kahan(v));
}

int main() {
  const int n = 100'000;
  std::vector<float> big_then_ones(n, 1.0f);
  big_then_ones[0] = 16'777'216.0f;
  std::vector<float> harmonic(n);
  for (int i = 0; i < n; ++i) harmonic[i] = 1.0f / static_cast<float>(i + 1);
  report("2^24 then ones", big_then_ones);
  report("harmonic", harmonic);
}
