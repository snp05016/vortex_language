// Follows: Goff, Kennedy & Tseng, "Practical Dependence Testing", PLDI 1991
// (the GCD test as the first, cheapest step of the SIV case),
// doi:10.1145/113445.113448
#include <cstdio>
#include <numeric>

// The GCD test decides, for one pair of subscripts that share exactly one
// loop index (Goff, Kennedy and Tseng's SIV case), whether an integer
// dependence distance can possibly exist. Two subscripts a1*i + c1 and
// a2*i + c2 touch the same array element, for some pair of iterations i1
// and i2, exactly when a1*i1 - a2*i2 = c2 - c1 has an integer solution,
// which requires gcd(a1, a2) to divide (c2 - c1). The test is necessary,
// not sufficient: passing it does not prove a dependence, only fails to
// rule one out, so a compiler that relies on it alone must still treat a
// pass as "assume dependence".

struct Subscript {
  int coeff, constant;
};

bool gcd_test(Subscript s1, Subscript s2) {
  int g = std::gcd(s1.coeff, s2.coeff);
  int rhs = s2.constant - s1.constant;
  return g != 0 && rhs % g == 0;
}

struct Case {
  const char* name;
  Subscript s1, s2;
};

int main() {
  Case cases[] = {
      {"a[i] vs a[i - 1]", {1, 0}, {1, -1}},        // the stencil's a-1
      {"a[2*i] vs a[2*i + 1]", {2, 0}, {2, 1}},     // even vs odd elements
      {"a[2*i] vs a[4*i + 1]", {2, 0}, {4, 1}},     // two strides, no overlap
      {"a[3*i] vs a[3*i + 6]", {3, 0}, {3, 6}},     // a real coincidence
      {"a[6*i] vs a[4*i + 1]", {6, 0}, {4, 1}},     // always even vs odd
  };
  for (auto& c : cases) {
    std::printf("%-22s gcd test: %s\n", c.name,
                gcd_test(c.s1, c.s2) ? "cannot rule out" : "no dependence");
  }
}
