// A teaching model of a simple in-order pipeline: four stages (fetch,
// decode, execute, writeback), one instruction wide, no forwarding between
// stages. Each stage holds one instruction at a time, in program order, so
// a later instruction cannot enter a stage until the one ahead of it has
// left it. If an instruction reads a register the instruction just before
// it writes, decode also waits until that write reaches writeback: a data
// hazard becomes a stall (a bubble). This is not any real CPU's pipeline
// (real ones forward results between stages and stall far less); it only
// shows why overlapping stages beats running each instruction start to
// finish, and what an unhidden dependency costs.
//
// Follows: no external source; an original model built for this chapter.

#include <algorithm>
#include <cstddef>
#include <print>
#include <string_view>
#include <vector>

struct Instruction {
  std::string_view name;
  bool reads_previous_result;
};

struct Timing {
  int fetch, decode, execute, writeback;
};

std::vector<Timing> schedule(const std::vector<Instruction> &program) {
  std::vector<Timing> timing;
  for (std::size_t i = 0; i < program.size(); ++i) {
    const bool has_prev = i > 0;
    const int fetch = std::max(static_cast<int>(i), has_prev ? timing[i - 1].decode : 0);
    int decode = std::max(fetch + 1, has_prev ? timing[i - 1].execute : 0);
    if (program[i].reads_previous_result && has_prev) {
      decode = std::max(decode, timing[i - 1].writeback + 1);  // wait for the write
    }
    const int execute = std::max(decode + 1, has_prev ? timing[i - 1].writeback : 0);
    const int writeback = std::max(execute + 1, has_prev ? timing[i - 1].writeback + 1 : 0);
    timing.push_back({fetch, decode, execute, writeback});
  }
  return timing;
}

void show(const std::vector<Instruction> &program, const std::vector<Timing> &timing) {
  std::println("instr          F  D  E  W");
  for (std::size_t i = 0; i < program.size(); ++i) {
    const auto &t = timing[i];
    std::println("{:<14} {:>2} {:>2} {:>2} {:>2}", program[i].name, t.fetch, t.decode,
                 t.execute, t.writeback);
  }
}

int main() {
  const std::vector<Instruction> with_hazard = {
      {"mul t0", false}, {"add t1", false}, {"add t2, t1", true}, {"mul t3", false},
  };
  std::vector<Instruction> hazard_free = with_hazard;
  for (auto &instruction : hazard_free) instruction.reads_previous_result = false;

  const auto timed = schedule(with_hazard);
  show(with_hazard, timed);
  const int with_bubble = timed.back().writeback + 1;
  const int without_bubble = schedule(hazard_free).back().writeback + 1;
  const int one_at_a_time = 4 * static_cast<int>(with_hazard.size());

  std::println("cycles: {} pipelined with the hazard, {} pipelined without it, "
               "{} run one at a time",
               with_bubble, without_bubble, one_at_a_time);
  std::println("the hazard cost {} cycle(s) of bubble", with_bubble - without_bubble);
}
