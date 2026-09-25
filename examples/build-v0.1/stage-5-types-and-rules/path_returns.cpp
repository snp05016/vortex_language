// A quiz flowchart: each question has a "yes" branch and, if its author
// remembered, a "no" branch; a node with no branches is an answer. Before
// anyone plays, a checker walks the chart once, without answering any
// question, and lists every route that ends with no answer. It never asks
// whether a route could really be taken: a question whose answer never
// changes still needs both branches.
//
// Follows Robert Nystrom, Crafting Interpreters, "Resolving and Binding",
// which walks a tree once, without running it, to find mistakes early.

#include <memory>
#include <print>
#include <string>
#include <vector>

struct Node {
  std::string text;
  std::unique_ptr<Node> yes;
  std::unique_ptr<Node> no;
};

std::unique_ptr<Node> answer(std::string text) {
  return std::make_unique<Node>(std::move(text));
}

std::unique_ptr<Node> ask(std::string text, std::unique_ptr<Node> yes,
                          std::unique_ptr<Node> no = nullptr) {
  return std::make_unique<Node>(std::move(text), std::move(yes), std::move(no));
}

void find_gaps(const Node& node, const std::string& route,
               std::vector<std::string>& gaps) {
  if (!node.yes && !node.no) {
    return;  // an answer: this route is complete
  }
  for (const auto& [branch, label] : {std::pair{node.yes.get(), "yes"},
                                      std::pair{node.no.get(), "no"}}) {
    const std::string next = route + node.text + " " + label + ", ";
    if (branch != nullptr) {
      find_gaps(*branch, next, gaps);
    } else {
      gaps.push_back(next + "then nothing");
    }
  }
}

void report(const char* name, const Node& chart) {
  std::vector<std::string> gaps;
  find_gaps(chart, "", gaps);
  std::println("{}: {} route(s) with no answer", name, gaps.size());
  for (const auto& gap : gaps) {
    std::println("  {}", gap);
  }
}

int main() {
  const auto complete = ask("alive?", ask("flies?", answer("bird"), answer("dog")),
                            answer("rock"));
  report("complete chart", *complete);

  // "2 > 1?" is always yes when played, but the checker does not evaluate
  // it, so its missing "no" branch is still a gap.
  const auto gappy = ask("alive?", ask("flies?", answer("bird")),
                         ask("2 > 1?", answer("rock")));
  report("gappy chart", *gappy);
}
