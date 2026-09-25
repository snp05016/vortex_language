// Resolve jump targets in a tiny list of labelled steps, where a jump may name
// a label written below it. A single pass fails on that forward jump; a first
// pass that records every label, then a second that resolves, does not.
//
// Follows cppreference's documentation of unordered_map::find and
// unordered_map::insert_or_assign (C++17).

#include <print>
#include <string>
#include <unordered_map>
#include <vector>

struct Step {
  std::string label; // empty when the step has no label
  std::string jump;  // empty when the step does not jump
};

const std::vector<Step> steps = {
    {"start", "finish"}, // forward: finish is written below
    {"loop", "loop"},    // a step may name its own label
    {"finish", "start"}, // backward
    {"", "nowhere"},     // no step has this label
};

void resolve(bool record_all_labels_first) {
  std::unordered_map<std::string, std::size_t> labels;
  if (record_all_labels_first) {
    for (std::size_t i = 0; i < steps.size(); ++i) {
      if (!steps[i].label.empty()) labels.insert_or_assign(steps[i].label, i);
    }
  }
  for (std::size_t i = 0; i < steps.size(); ++i) {
    if (!record_all_labels_first && !steps[i].label.empty()) {
      labels.insert_or_assign(steps[i].label, i); // seen only when reached
    }
    if (auto found = labels.find(steps[i].jump); found != labels.end()) {
      std::println("  step {}: jump {} -> step {}", i, steps[i].jump,
                   found->second);
    } else {
      std::println("  step {}: jump {} -> unknown label", i, steps[i].jump);
    }
  }
}

int main() {
  std::println("one pass:");
  resolve(false);
  std::println("two passes:");
  resolve(true);
}
