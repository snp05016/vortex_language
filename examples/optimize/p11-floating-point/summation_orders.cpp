// One big value plus many small ones, summed three ways. The exact
// mathematical answer is an integer small enough for a double to hold
// exactly, so it is a fair reference for how far each f32 result strays.
//
// Follows: Goldberg, "What Every Computer Scientist Should Know About
// Floating-Point Arithmetic", ACM Computing Surveys 23(1), 1991 (error
// accumulation in summation, and compensated summation).

#include <print>
#include <vector>

float sequential_sum(const std::vector<float>& v) {
  float s = 0.0f;
  for (float x : v) s += x;
  return s;
}

float pairwise_sum(const float* v, std::size_t n) {
  if (n <= 8) {
    float s = 0.0f;
    for (std::size_t i = 0; i < n; ++i) s += v[i];
    return s;
  }
  std::size_t half = n / 2;
  return pairwise_sum(v, half) + pairwise_sum(v + half, n - half);
}

float kahan_sum(const std::vector<float>& v) {
  float sum = 0.0f, carry = 0.0f;  // carry holds the low bits lost so far
  for (float x : v) {
    float y = x - carry;
    float t = sum + y;
    carry = (t - sum) - y;
    sum = t;
  }
  return sum;
}

int main() {
  const int n = 100'000;
  std::vector<float> v(n, 1.0f);
  v[0] = 16'777'216.0f;  // 2^24: past this, f32 cannot add 1.0 and change
  double exact = 16'777'216.0 + double(n - 1);

  float sequential = sequential_sum(v);
  float pairwise = pairwise_sum(v.data(), v.size());
  float kahan = kahan_sum(v);

  std::println("exact      = {}", exact);
  std::println("sequential = {}  (error {})", sequential, exact - sequential);
  std::println("pairwise   = {}  (error {})", pairwise, exact - pairwise);
  std::println("kahan      = {}  (error {})", kahan, exact - kahan);
}
