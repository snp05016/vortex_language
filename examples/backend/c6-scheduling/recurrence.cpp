// How fast can a loop go, whatever the scheduler does? Two lower bounds on
// the initiation interval II, the cycles between the starts of two
// consecutive iterations:
//
//   ResMII: each kind of unit can start only so many operations per cycle.
//   RecMII: a value that feeds itself through a cycle of dependences, such
//           as an accumulator, cannot come round faster than the latencies
//           on that cycle allow. An edge that crosses d iterations lets
//           the II be spent d times, so the bound is ceil(latency / d).
//
// The loops are dot-product bodies on a made-up machine: 2 load units,
// 2 floating-point units, 2 integer units; add latency 4, the same as the
// scalar fadd in LLVM's apple-m1 scheduling model. "columns" is how many
// outputs one iteration computes, each summed in its own order.
//
// Follows: LLVM MachinePipeliner.cpp, release/18.x (MII = max(ResMII,
// RecMII); RecMII = ceil(delay / distance) for each recurrence), and Rau,
// "Iterative modulo scheduling", MICRO-27, 1994.

#include <algorithm>
#include <cstdio>
#include <vector>

struct Edge { int from, to, latency, distance; };

struct Loop {
    const char* name;
    int outputs;
    int loads, fp_ops, int_ops;  // operations per iteration
    std::vector<Edge> edges;     // dependences, including loop-carried ones
    int nodes;
};

int ceil_div(int a, int b) { return (a + b - 1) / b; }

int res_mii(const Loop& l) {
    return std::max({ceil_div(l.loads, 2), ceil_div(l.fp_ops, 2),
                     ceil_div(l.int_ops, 2), 1});
}

// Smallest II with no dependence cycle whose latency exceeds II times its
// distance: no positive cycle under weights latency - II * distance.
int rec_mii(const Loop& l) {
    for (int ii = 1;; ++ii) {
        const long kNone = -1'000'000;
        std::vector<std::vector<long>> w(l.nodes,
                                         std::vector<long>(l.nodes, kNone));
        for (const Edge& e : l.edges)
            w[e.from][e.to] = std::max(w[e.from][e.to],
                                       long(e.latency - ii * e.distance));
        for (int k = 0; k < l.nodes; ++k)  // longest paths, Floyd-Warshall
            for (int i = 0; i < l.nodes; ++i)
                for (int j = 0; j < l.nodes; ++j)
                    if (w[i][k] > kNone && w[k][j] > kNone)
                        w[i][j] = std::max(w[i][j], w[i][k] + w[k][j]);
        bool positive = false;
        for (int i = 0; i < l.nodes; ++i) positive |= w[i][i] > 0;
        if (!positive) return ii;
    }
}

// Nodes 0..c-1 are the products, c..2c-1 the accumulating adds, 2c the
// index increment. Each add reads its product (same iteration) and its own
// result from the previous iteration (distance 1).
Loop dot(const char* name, int c) {
    Loop l{name, c, 1 + c, 2 * c, 2, {}, 2 * c + 1};
    for (int k = 0; k < c; ++k) {
        l.edges.push_back({k, c + k, 4, 0});      // product -> add
        l.edges.push_back({c + k, c + k, 4, 1});  // add -> next add
    }
    l.edges.push_back({2 * c, 2 * c, 1, 1});      // i -> next i
    return l;
}

int main() {
    std::printf("%-10s %6s %7s %3s %17s\n", "loop", "ResMII", "RecMII", "II",
                "cycles per output");
    for (const Loop& l : {dot("1 column", 1), dot("4 columns", 4),
                          dot("8 columns", 8)}) {
        int res = res_mii(l), rec = rec_mii(l), ii = std::max(res, rec);
        std::printf("%-10s %6d %7d %3d %17.2f\n", l.name, res, rec, ii,
                    double(ii) / l.outputs);
    }
}
