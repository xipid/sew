/**
 * @file Sew.hpp
 * @brief The Sew engine — polyglot build orchestrator.
 *
 * Sew has ZERO filesystem knowledge. All I/O goes through callbacks.
 * The caller (main.cpp) wires it to the real filesystem.
 */

#pragma once

#include <Sew/Graph.hpp>
#include <Sew/Language.hpp>
#include <Sew/Target.hpp>
#include <Sew/Cache.hpp>
#include <Sew/EvalContext.hpp>
#include <Xi/Func.hpp>

namespace Sew {

/**
 * @struct SourceInput
 * @brief A named input to the build system.
 */
struct SourceInput {
    String name;
    String content;
};

/**
 * @class Engine
 * @brief The main Sew orchestrator.
 */
class Engine {
public:
    // --- Registration ---
    void registerLanguage(Language* lang);
    void registerTarget(Target* target);

    // --- I/O Callbacks (Sew has NO filesystem access) ---
    Func<String(String)> onRead;            ///< Read file by path
    Func<void(String, String)> onCacheSet;  ///< key → content
    Func<String(String)> onCacheGet;        ///< key → content (empty = miss)
    Func<bool(String)> onCacheHas;          ///< key → exists?
    Func<void(String, String)> onAsset;     ///< name, contents
    Func<void(String)> onFinish;            ///< final output path/content

    // --- Progress callbacks ---
    Func<void(String, usz, usz)> onProgress; ///< message, current, total
    Func<void(String)> onInfo;               ///< info message
    Func<void(String)> onWarn;               ///< warning message
    Func<void(String)> onError;              ///< error message

    // --- Configuration ---
    String assetsDir;
    String outputPath;

    // --- Pipeline ---
    void input(const String& name, const String& content);
    void find();    ///< Discover all imports, build DAG, compute build plan
    void build(const String& targetName);  ///< Execute the build plan
    void eval(const String& language);     ///< REPL/eval mode
    void destroy();

    ~Engine() { destroy(); }

    // --- Introspection ---
    const DepGraph& graph() const { return _graph; }
    Language* languageFor(const String& ext) const;
    Target* targetByName(const String& name) const;
    usz nodeCount() const { return _graph.nodes.size(); }

private:
    Map<String, Language*> _languages;      ///< ext → Language*
    Map<String, Language*> _langsByName;     ///< name → Language*
    Map<String, Target*> _targets;          ///< name → Target*
    Array<SourceInput> _inputs;
    DepGraph _graph;
    BuildPlan _plan;
    EvalContext _evalCtx;

    /// Detect language by file extension.
    String detectLanguage(const String& path) const;

    /// Resolve an import specifier to a file path.
    String resolveImport(const ImportSpec& imp, const String& currentFile);

    /// Recursively discover all dependencies starting from a file.
    void discoverFile(const String& path);
};

} // namespace Sew
