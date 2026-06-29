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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <utility>
#include <atomic>

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
    Array<String> includePaths;
    bool isRepl = false;

    // --- Pipeline ---
    void input(const String& name, const String& content);
    void find(const String& targetName = "");    ///< Discover all imports, build DAG, compute build plan
    bool build(const String& targetName);  ///< Execute the build plan
    void eval(const String& language);     ///< REPL/eval mode
    String evalCode(const String& code);  ///< Evaluate code in current eval context
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
    String _generatedTsGlue;
    String _generatedJsGlue;
    String _generatedQuickjsBindings;
    Array<String> _inferredIncludeRoots;

    /// Detect language by file extension.
    String detectLanguage(const String& path) const;

    /// Resolve an import specifier to a file path.
    String resolveImport(const ImportSpec& imp, const String& currentFile);

    /// Recursively discover all dependencies starting from a file.
    void discoverFile(const String& path);

    // --- On-the-fly compilation ---
    struct CompileTask {
        usz nodeIdx;
        String language;
        CompileRequest req;
        Array<String> depHashes;
    };

    std::mutex _compileMutex;
    std::vector<std::thread> _compileThreads;
    std::vector<CompileTask> _compileQueue;
    std::condition_variable _compileCv;
    bool _compileDone = false;
    Array<CompileResult> _asyncCompileResults;
    Array<usz> _asyncCompileNodeIndices;
    bool _compileSuccess = true;
    String _compileErrors;
    std::atomic<usz> _discoveredCount{0};
    std::atomic<usz> _compiledCount{0};

    Target* _activeTarget = nullptr;
    String _activeTargetName;

    struct CachedFileEntry {
        String path;
        String language;
        long long mtime = 0;
        long long size = 0;
        String contentHash;
        Array<String> resolvedImports;
    };
    Map<String, CachedFileEntry> _cachedFiles;

    void loadDepDb();
    void saveDepDb();

    void startCompileWorkers(Target* target, const String& targetName);
    void stopCompileWorkers();
    void queueCompile(usz nodeIdx, Target* target, const String& targetName);
};

} // namespace Sew
