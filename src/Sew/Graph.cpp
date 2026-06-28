/**
 * @file Graph.cpp
 * @brief Dependency DAG implementation with Tarjan's SCC and topological sort.
 */

#include <Sew/Graph.hpp>

namespace Sew {

usz DepGraph::addNode(const String& path, const String& language) {
    const usz* existing = pathToIndex.get(path);
    if (existing) return *existing;

    usz idx = nodes.size();
    SourceNode node;
    node.path = path;
    node.language = language;
    nodes.push(Xi::Move(node));
    pathToIndex.set(path, idx);
    return idx;
}

void DepGraph::addEdge(usz from, usz to) {
    if (from >= nodes.size() || to >= nodes.size()) return;
    // Avoid duplicate edges
    for (usz i = 0; i < nodes[from].dependencies.size(); ++i) {
        if (nodes[from].dependencies[i] == to) return;
    }
    nodes[from].dependencies.push(to);
}

bool DepGraph::hasNode(const String& path) const {
    return pathToIndex.get(path) != nullptr;
}

usz DepGraph::indexOf(const String& path) const {
    const usz* idx = pathToIndex.get(path);
    return idx ? *idx : (usz)-1;
}

// --- Tarjan's SCC Algorithm ---

void DepGraph::tarjanVisit(usz v, TarjanState& state) const {
    state.index[v] = state.nextIndex;
    state.lowlink[v] = state.nextIndex;
    state.nextIndex++;
    state.stack.push(v);
    state.onStack[v] = true;

    for (usz i = 0; i < nodes[v].dependencies.size(); ++i) {
        usz w = nodes[v].dependencies[i];
        if (state.index[w] == (usz)-1) {
            tarjanVisit(w, state);
            if (state.lowlink[w] < state.lowlink[v])
                state.lowlink[v] = state.lowlink[w];
        } else if (state.onStack[w]) {
            if (state.index[w] < state.lowlink[v])
                state.lowlink[v] = state.index[w];
        }
    }

    // Root of an SCC
    if (state.lowlink[v] == state.index[v]) {
        Array<usz> scc;
        usz w;
        do {
            w = state.stack[state.stack.size() - 1];
            state.stack.pop();
            state.onStack[w] = false;
            scc.push(w);
        } while (w != v);
        state.sccs.push(Xi::Move(scc));
    }
}

Array<Array<usz>> DepGraph::findSCCs() const {
    TarjanState state;
    usz n = nodes.size();
    state.index.allocate(n);
    state.lowlink.allocate(n);
    state.onStack.allocate(n);
    for (usz i = 0; i < n; ++i) {
        state.index[i] = (usz)-1;
        state.lowlink[i] = (usz)-1;
        state.onStack[i] = false;
    }
    state.nextIndex = 0;

    for (usz i = 0; i < n; ++i) {
        if (state.index[i] == (usz)-1)
            tarjanVisit(i, state);
    }

    return Xi::Move(state.sccs);
}

BuildPlan DepGraph::computeBuildPlan() const {
    BuildPlan plan;
    usz n = nodes.size();
    if (n == 0) return plan;

    // Assign SCC IDs
    Array<Array<usz>> sccs = findSCCs();
    Array<usz> sccId;
    sccId.allocate(n);
    for (usz i = 0; i < n; ++i) sccId[i] = 0;
    for (usz s = 0; s < sccs.size(); ++s) {
        for (usz j = 0; j < sccs[s].size(); ++j) {
            sccId[sccs[s][j]] = s;
        }
    }

    // Build SCC-level DAG and compute in-degrees
    usz sccCount = sccs.size();
    Array<Array<usz>> sccDeps;
    sccDeps.allocate(sccCount);
    Array<usz> inDegree;
    inDegree.allocate(sccCount);
    for (usz i = 0; i < sccCount; ++i) inDegree[i] = 0;

    for (usz i = 0; i < n; ++i) {
        for (usz j = 0; j < nodes[i].dependencies.size(); ++j) {
            usz dep = nodes[i].dependencies[j];
            usz fromScc = sccId[i];
            usz toScc = sccId[dep];
            if (fromScc != toScc) {
                // Check for duplicate
                bool dup = false;
                for (usz k = 0; k < sccDeps[fromScc].size(); ++k) {
                    if (sccDeps[fromScc][k] == toScc) { dup = true; break; }
                }
                if (!dup) {
                    sccDeps[fromScc].push(toScc);
                    inDegree[toScc]++;
                }
            }
        }
    }

    // Kahn's algorithm on SCC DAG (topological sort)
    Array<usz> queue;
    for (usz i = 0; i < sccCount; ++i) {
        if (inDegree[i] == 0) queue.push(i);
    }

    // SCCs are output in reverse order by Tarjan, and Kahn gives us
    // dependency-first ordering. We build steps bottom-up.
    Array<usz> topoOrder;
    usz front = 0;
    while (front < queue.size()) {
        usz scc = queue[front++];
        topoOrder.push(scc);
        for (usz i = 0; i < sccDeps[scc].size(); ++i) {
            usz dep = sccDeps[scc][i];
            inDegree[dep]--;
            if (inDegree[dep] == 0) queue.push(dep);
        }
    }

    // Reverse: dependencies first
    for (usz i = 0; i < topoOrder.size() / 2; ++i) {
        Xi::Swap(topoOrder[i], topoOrder[topoOrder.size() - 1 - i]);
    }

    // Group SCCs into BuildSteps by their dependency levels to enable parallel compilation
    Array<usz> levels;
    levels.allocate(sccCount);
    for (usz i = 0; i < sccCount; ++i) levels[i] = 0;

    usz maxLevel = 0;
    for (usz i = 0; i < topoOrder.size(); ++i) {
        usz scc = topoOrder[i];
        usz myLevel = 0;

        bool hasCppSource = false;
        for (usz j = 0; j < sccs[scc].size(); ++j) {
            usz nodeIdx = sccs[scc][j];
            String path = nodes[nodeIdx].path;
            if (path.endsWith(".cpp") || path.endsWith(".c") || path.endsWith(".cc") || path.endsWith(".cxx")) {
                hasCppSource = true;
                break;
            }
        }

        if (hasCppSource) {
            myLevel = 0;
        } else {
            for (usz d = 0; d < sccDeps[scc].size(); ++d) {
                usz dep = sccDeps[scc][d];
                if (levels[dep] + 1 > myLevel) {
                    myLevel = levels[dep] + 1;
                }
            }
        }
        levels[scc] = myLevel;
        if (myLevel > maxLevel) {
            maxLevel = myLevel;
        }
    }

    Array<BuildStep> steps;
    for (usz i = 0; i <= maxLevel; ++i) {
        steps.push(BuildStep());
    }

    for (usz i = 0; i < topoOrder.size(); ++i) {
        usz scc = topoOrder[i];
        usz lvl = levels[scc];
        for (usz j = 0; j < sccs[scc].size(); ++j) {
            steps[lvl].nodeIndices.push(sccs[scc][j]);
        }
    }

    for (usz i = 0; i < steps.size(); ++i) {
        if (steps[i].nodeIndices.size() > 0) {
            plan.steps.push(Xi::Move(steps[i]));
        }
    }

    return plan;
}

void DepGraph::clear() {
    nodes.clear();
    pathToIndex.clear();
}

} // namespace Sew
