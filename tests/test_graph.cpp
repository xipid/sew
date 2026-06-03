/**
 * @file test_graph.cpp
 * @brief Tests for the dependency graph.
 */

#include <Sew/Graph.hpp>
#include <cstdio>

using namespace Sew;
using namespace Collection;

static int passed = 0;
static int failed = 0;

static void check(const char* name, bool cond) {
    if (cond) {
        fprintf(stderr, "  \033[38;2;80;250;123m✓\033[0m %s\n", name);
        passed++;
    } else {
        fprintf(stderr, "  \033[38;2;255;85;85m✗\033[0m %s\n", name);
        failed++;
    }
}

// ─── Test: Basic node add/lookup ─────────────────────────────────────────

static void test_add_node() {
    DepGraph g;
    usz i0 = g.addNode("main.cpp", "cpp");
    usz i1 = g.addNode("utils.cpp", "cpp");
    usz i2 = g.addNode("main.cpp", "cpp"); // Duplicate

    check("First node index is 0", i0 == 0);
    check("Second node index is 1", i1 == 1);
    check("Duplicate returns same index", i2 == 0);
    check("Node count is 2", g.nodes.size() == 2);
    check("hasNode works", g.hasNode("main.cpp") && !g.hasNode("other.cpp"));
    check("indexOf works", g.indexOf("utils.cpp") == 1);
}

// ─── Test: Edge deduplication ────────────────────────────────────────────

static void test_edges() {
    DepGraph g;
    usz a = g.addNode("a.cpp", "cpp");
    usz b = g.addNode("b.cpp", "cpp");

    g.addEdge(a, b);
    g.addEdge(a, b); // Duplicate

    check("Single edge after dedup", g.nodes[a].dependencies.size() == 1);
}

// ─── Test: Linear chain → correct topo order ────────────────────────────

static void test_linear_chain() {
    DepGraph g;
    usz a = g.addNode("a.cpp", "cpp");
    usz b = g.addNode("b.cpp", "cpp");
    usz c = g.addNode("c.cpp", "cpp");

    // a → b → c
    g.addEdge(a, b);
    g.addEdge(b, c);

    BuildPlan plan = g.computeBuildPlan();

    check("Linear chain: 3 build steps", plan.steps.size() == 3);

    // c must come before b, b before a
    usz posA = 0, posB = 0, posC = 0;
    for (usz i = 0; i < plan.steps.size(); ++i) {
        for (usz j = 0; j < plan.steps[i].nodeIndices.size(); ++j) {
            if (plan.steps[i].nodeIndices[j] == a) posA = i;
            if (plan.steps[i].nodeIndices[j] == b) posB = i;
            if (plan.steps[i].nodeIndices[j] == c) posC = i;
        }
    }
    check("Topo order: c before b", posC < posB);
    check("Topo order: b before a", posB < posA);
}

// ─── Test: Diamond dependency ────────────────────────────────────────────

static void test_diamond() {
    DepGraph g;
    usz a = g.addNode("a.cpp", "cpp");
    usz b = g.addNode("b.cpp", "cpp");
    usz c = g.addNode("c.cpp", "cpp");
    usz d = g.addNode("d.cpp", "cpp");

    // a → b → d
    // a → c → d
    g.addEdge(a, b);
    g.addEdge(a, c);
    g.addEdge(b, d);
    g.addEdge(c, d);

    BuildPlan plan = g.computeBuildPlan();

    // d must come first, then b and c (can be parallel), then a
    usz posA = 0, posB = 0, posC = 0, posD = 0;
    for (usz i = 0; i < plan.steps.size(); ++i) {
        for (usz j = 0; j < plan.steps[i].nodeIndices.size(); ++j) {
            usz n = plan.steps[i].nodeIndices[j];
            if (n == a) posA = i;
            if (n == b) posB = i;
            if (n == c) posC = i;
            if (n == d) posD = i;
        }
    }
    check("Diamond: d before b", posD < posB);
    check("Diamond: d before c", posD < posC);
    check("Diamond: b before a", posB < posA);
    check("Diamond: c before a", posC < posA);
}

// ─── Test: Cycle detection (SCC) ─────────────────────────────────────────

static void test_cycle_scc() {
    DepGraph g;
    usz a = g.addNode("a.cpp", "cpp");
    usz b = g.addNode("b.cpp", "cpp");
    usz c = g.addNode("c.cpp", "cpp");

    // a → b → c → a (cycle)
    g.addEdge(a, b);
    g.addEdge(b, c);
    g.addEdge(c, a);

    Array<Array<usz>> sccs = g.findSCCs();

    // All three should be in one SCC
    check("Cycle: single SCC", sccs.size() == 1);
    check("Cycle: SCC has 3 nodes", sccs.size() > 0 && sccs[0].size() == 3);
}

// ─── Test: Mixed SCC + non-SCC ───────────────────────────────────────────

static void test_mixed_scc() {
    DepGraph g;
    usz a = g.addNode("a.cpp", "cpp");
    usz b = g.addNode("b.cpp", "cpp");
    usz c = g.addNode("c.cpp", "cpp");
    usz d = g.addNode("d.cpp", "cpp");

    // a → b → a (cycle), a → c, c → d (no cycle)
    g.addEdge(a, b);
    g.addEdge(b, a);
    g.addEdge(a, c);
    g.addEdge(c, d);

    Array<Array<usz>> sccs = g.findSCCs();

    check("Mixed: 3 SCCs (a-b cycle, c alone, d alone)", sccs.size() == 3);

    BuildPlan plan = g.computeBuildPlan();
    check("Mixed: build plan has steps", plan.steps.size() >= 3);
}

// ─── Test: No edges → all independent ────────────────────────────────────

static void test_independent() {
    DepGraph g;
    g.addNode("a.cpp", "cpp");
    g.addNode("b.cpp", "cpp");
    g.addNode("c.cpp", "cpp");

    BuildPlan plan = g.computeBuildPlan();

    check("Independent: each node is its own step", plan.steps.size() == 3);
}

// ─── Test: Clear ─────────────────────────────────────────────────────────

static void test_clear() {
    DepGraph g;
    g.addNode("a.cpp", "cpp");
    g.addNode("b.cpp", "cpp");
    g.clear();

    check("Clear: no nodes", g.nodes.size() == 0);
    check("Clear: hasNode returns false", !g.hasNode("a.cpp"));
}

// ─── Main ────────────────────────────────────────────────────────────────

int main() {
    fprintf(stderr, "\n\033[38;2;0;210;255m\033[1m  Dependency Graph Tests\033[0m\n\n");

    test_add_node();
    test_edges();
    test_linear_chain();
    test_diamond();
    test_cycle_scc();
    test_mixed_scc();
    test_independent();
    test_clear();

    fprintf(stderr, "\n  \033[1m%d passed, %d failed\033[0m\n\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
