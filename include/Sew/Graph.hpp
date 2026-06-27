/**
 * @file Graph.hpp
 * @brief Dependency DAG with topological sort and SCC detection.
 */

#pragma once

#include <Sew/Language.hpp>

namespace Sew {

/**
 * @struct SourceNode
 * @brief A single file in the dependency graph.
 */
struct SourceNode {
    String path;
    String language;        ///< "cpp", "js", "py"
    String content;
    String contentHash;
    CompileForm form = CompileForm::Native;
    Array<usz> dependencies;
    usz sccId = 0;
    bool visited = false;
    bool compiled = false;
};

/**
 * @struct BuildStep
 * @brief A group of nodes that can be compiled in parallel.
 */
struct BuildStep {
    Array<usz> nodeIndices;
};

/**
 * @struct BuildPlan
 * @brief Ordered sequence of build steps.
 */
struct BuildPlan {
    Array<BuildStep> steps;
};

/**
 * @class DepGraph
 * @brief Dependency graph with topological ordering and cycle detection.
 */
class DepGraph {
public:
    Array<SourceNode> nodes;
    Map<String, usz> pathToIndex;

    /// Add a node, returns its index. No-op if already present.
    usz addNode(const String& path, const String& language);

    /// Add a dependency edge: 'from' depends on 'to'.
    void addEdge(usz from, usz to);

    /// Check if a path is already in the graph.
    bool hasNode(const String& path) const;

    /// Get the index of a path (assumes it exists).
    usz indexOf(const String& path) const;

    /// Tarjan's SCC algorithm — returns groups of mutually-dependent nodes.
    Array<Array<usz>> findSCCs() const;

    /// Compute a topological build order (respects SCCs).
    BuildPlan computeBuildPlan() const;

    /// Reset the graph.
    void clear();

private:
    // Tarjan's internals
    struct TarjanState {
        Array<usz> index;
        Array<usz> lowlink;
        Array<bool> onStack;
        Array<usz> stack;
        usz nextIndex = 0;
        Array<Array<usz>> sccs;
    };

    void tarjanVisit(usz v, TarjanState& state) const;
};

} // namespace Sew
