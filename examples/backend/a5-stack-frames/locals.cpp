// average3 keeps its three inputs in a local array instead of three scalar
// locals, so that -O0 code, which keeps every local in memory rather than a
// register, has to reserve a real array's worth of frame space for it. The
// listing pasted in the chapter comes from compiling this file alone with
// `clang -O0 -S -target arm64-apple-macos`.

#include <print>

float average3(float a, float b, float c) {
  float values[3] = {a, b, c};
  float sum = 0.0f;
  for (int i = 0; i < 3; ++i) sum += values[i];
  return sum / 3.0f;
}

int main() {
  std::println("{}", average3(1.0f, 2.0f, 3.0f));
  std::println("{}", average3(10.0f, 20.0f, 30.0f));
}
