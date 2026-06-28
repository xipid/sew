/**
 * @file Sew.cpp
 * @brief Sew engine implementation — the heart of the polyglot build system.
 */

#include <Sew/Sew.hpp>
#include <Sew/Parser.hpp>
#include <Sew/Generator.hpp>
#include <Languages/CPP/CppPreprocessor.hpp>
#include <Languages/CPP/CppLanguage.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>

namespace Sew {

static String canonicalize(const String& path) {
    char* rp = ::realpath(path.c_str(), nullptr);
    if (rp) {
        String res(rp);
        free(rp);
        return res;
    }
    String absPath = path;
    if (!path.startsWith("/")) {
        char cwd[1024];
        if (::getcwd(cwd, sizeof(cwd))) {
            absPath = String(cwd) + "/" + path;
        }
    }
    Array<String> parts = absPath.split("/");
    Array<String> clean;
    for (usz i = 0; i < parts.size(); ++i) {
        if (parts[i] == "." || parts[i].isEmpty()) continue;
        if (parts[i] == "..") {
            if (clean.size() > 0 && clean[clean.size() - 1] != "..") {
                clean.pop();
            } else {
                clean.push("..");
            }
        } else {
            clean.push(parts[i]);
        }
    }
    String res;
    if (absPath.startsWith("/")) res += "/";
    for (usz i = 0; i < clean.size(); ++i) {
        if (i > 0) res += "/";
        res += clean[i];
    }
    return res;
}

static String makeRelativePath(const String& path) {
    char cwd[1024];
    if (::getcwd(cwd, sizeof(cwd))) {
        String cwdStr(cwd);
        if (!cwdStr.endsWith("/")) cwdStr += "/";
        if (path.startsWith(cwdStr)) {
            return path.substring(cwdStr.size());
        }
    }
    return path;
}

static String getTempDir() {
    const char* envTemp = ::getenv("SEW_TEMP_DIR");
    if (envTemp && envTemp[0] != '\0') {
        return String(envTemp);
    }

    const char* home = ::getenv("HOME");
    if (home && home[0] != '\0') {
        String tempDir = String(home) + "/.sew";
        struct stat st;
        if (::stat(tempDir.c_str(), &st) != 0) {
            ::mkdir(tempDir.c_str(), 0700);
        }
        return tempDir;
    }

    return "/tmp";
}

static String inferIncludeRoot(const String& path) {
    const String needle = "/include/";
    long long includePos = path.indexOf(needle);
    if (includePos < 0) return "";
    return path.substring(0, (usz)includePos + needle.size() - 1);
}

static bool containsPath(const Array<String>& paths, const String& path) {
    for (usz i = 0; i < paths.size(); ++i) {
        if (paths[i] == path) return true;
    }
    return false;
}

static bool startsWithPath(const String& path, const String& prefix) {
    if (prefix.isEmpty()) return false;
    if (path.size() < prefix.size()) return false;
    for (usz i = 0; i < prefix.size(); ++i) {
        if (path.data()[i] != prefix.data()[i]) return false;
    }
    return true;
}

void Engine::registerLanguage(Language* lang) {
    Array<String> exts = lang->extensions();
    for (usz i = 0; i < exts.size(); ++i) {
        _languages.set(exts[i], lang);
    }
    _langsByName.set(lang->name(), lang);
}

void Engine::registerTarget(Target* target) {
    _targets.set(target->name(), target);
    Array<String> aliases = target->aliases();
    for (usz i = 0; i < aliases.size(); ++i) {
        _targets.set(aliases[i], target);
    }
}

Language* Engine::languageFor(const String& ext) const {
    Language* const* found = _languages.get(ext);
    return found ? *found : nullptr;
}

Target* Engine::targetByName(const String& name) const {
    Target* const* found = _targets.get(name);
    return found ? *found : nullptr;
}

String Engine::detectLanguage(const String& path) const {
    // Find the last dot
    long long lastDot = -1;
    for (usz i = 0; i < path.size(); ++i) {
        if (path.data()[i] == '.') lastDot = (long long)i;
    }
    if (lastDot < 0) return "";

    String ext = path.substring((usz)lastDot);
    if (ext == ".a" || ext == ".so" || ext == ".dylib" || ext == ".dll") {
        return "lib";
    }
    Language* lang = languageFor(ext);
    return lang ? lang->name() : "";
}

String Engine::resolveImport(const ImportSpec& imp, const String& currentFile) {
    String spec = imp.specifier;
    String res = spec;

    // If already absolute or relative, use as-is
    if (spec.startsWith("/") || spec.startsWith("./") || spec.startsWith("../")) {
        res = spec;
        return canonicalize(res);
    }

    // Bare specifier walk-up and search helper
    auto checkCandidate = [](const String& path, String& outFound) -> bool {
        struct stat st;
        if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            outFound = path;
            return true;
        }
        const char* tryExts[] = {
            ".cpp", ".c", ".cc", ".cxx",
            ".hpp", ".h", ".hxx",
            ".js", ".ts", ".mjs",
            ".py",
            ".wasm",
            nullptr
        };
        for (int e = 0; tryExts[e]; ++e) {
            String cand = path + tryExts[e];
            if (::stat(cand.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                outFound = cand;
                return true;
            }
        }
        return false;
    };

    // 1. Walking up tree with wildcard includes and user includes
    bool found = false;
    Language** langPtr = _langsByName.get("cpp");
    if (langPtr) {
        Languages::CppLanguage* cppLang = dynamic_cast<Languages::CppLanguage*>(*langPtr);
        if (cppLang) {
            Array<String> paths = cppLang->preprocessor().getSearchPaths(currentFile);
            for (usz i = 0; i < paths.size(); ++i) {
                if (checkCandidate(paths[i] + "/" + spec, res)) {
                    found = true;
                    break;
                }
            }
        }
    }

    // Fallback: resolve relative to nearest dir
    if (!found) {
        long long lastSlash = -1;
        for (usz i = 0; i < currentFile.size(); ++i) {
            if (currentFile.data()[i] == '/') lastSlash = (long long)i;
        }
        String currentDir = (lastSlash >= 0) ? currentFile.substring(0, (usz)lastSlash) : ".";
        res = currentDir + "/" + spec;
    }

    return canonicalize(res);
}

void Engine::discoverFile(const String& rawPath) {
    String path = canonicalize(rawPath);
    // if (path.includes("disassemble")) {
    //     ::printf("DEBUG: discoverFile called for path: %s\n", path.c_str());
    //     ::fflush(stdout);
    // }
    if (_graph.hasNode(path)) return;

    String langName = detectLanguage(path);
    if (langName.isEmpty()) {
        if (onWarn) onWarn("Unknown file type: " + path);
        return;
    }

    // Read file content
    String content;
    if (onRead) {
        content = onRead(path);
    }


    if (content.isEmpty()) {
        // File might not exist (e.g. candidate sibling) — skip silently
        return;
    }

    usz nodeIdx = _graph.addNode(path, langName);
    _graph.nodes[nodeIdx].content = content;

    // If it's a C++ header file, infer and register its include root
    String ext;
    long long lastDot = -1;
    for (usz k = 0; k < path.size(); ++k) {
        if (path.data()[k] == '.') lastDot = (long long)k;
    }
    if (lastDot >= 0) ext = path.substring((usz)lastDot);
    if (langName == "cpp" && (ext == ".h" || ext == ".hpp" || ext == ".hxx")) {
        String includeRoot = inferIncludeRoot(path);
        if (!includeRoot.isEmpty()) {
            Language** langPtr = _langsByName.get("cpp");
            if (langPtr) {
                Languages::CppLanguage* cppLang = dynamic_cast<Languages::CppLanguage*>(*langPtr);
                if (cppLang) {
                    bool exists = false;
                    for (usz j = 0; j < cppLang->preprocessor().includePaths.size(); ++j) {
                        if (cppLang->preprocessor().includePaths[j] == includeRoot) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        cppLang->preprocessor().includePaths.push(includeRoot);
                    }
                }
            }
        }
    }

    Language** langPtr = _langsByName.get(langName);
    if (!langPtr) return;
    Language* lang = *langPtr;



    Array<ImportSpec> imports = lang->parseImports(content, path);

    if (onProgress) {
        onProgress("Discovering", _graph.nodes.size(), 0);
    }

    for (usz i = 0; i < imports.size(); ++i) {
        String resolved = resolveImport(imports[i], path);
        if (!resolved.isEmpty()) resolved = canonicalize(resolved);

        // Try to discover the resolved file
        // For files that might not have an extension, try common extensions
        if (detectLanguage(resolved).isEmpty()) {
            // Try common extensions
            const char* tryExts[] = {
                ".cpp", ".c", ".cc", ".cxx",
                ".hpp", ".h", ".hxx",
                ".js", ".ts", ".mjs",
                ".py",
                ".wasm",
                nullptr
            };
            bool found = false;
            for (int e = 0; tryExts[e]; ++e) {
                String candidate = resolved;
                candidate += tryExts[e];
                // Check if readable
                if (onRead) {
                    String test = onRead(candidate);
                    if (test.length() > 0) {
                        resolved = candidate;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) continue;
        }

        discoverFile(resolved);

        if (_graph.hasNode(resolved)) {
            _graph.addEdge(nodeIdx, _graph.indexOf(resolved));
        }
    }
}

void Engine::input(const String& name, const String& content) {
    SourceInput inp;
    inp.name = name;
    inp.content = content;
    _inputs.push(Xi::Move(inp));
}

void Engine::find() {
    if (onInfo) onInfo("Discovering dependencies...");

    for (usz i = 0; i < _inputs.size(); ++i) {
        String path = _inputs[i].name;

        // If content was provided inline, add it to graph directly
        if (_inputs[i].content.length() > 0) {
            String langName = detectLanguage(path);
            if (langName.isEmpty()) continue;

            usz nodeIdx = _graph.addNode(path, langName);
            _graph.nodes[nodeIdx].content = _inputs[i].content;

            // If it's a C++ header file, infer and register its include root
            String ext;
            long long lastDot = -1;
            for (usz k = 0; k < path.size(); ++k) {
                if (path.data()[k] == '.') lastDot = (long long)k;
            }
            if (lastDot >= 0) ext = path.substring((usz)lastDot);
            if (langName == "cpp" && (ext == ".h" || ext == ".hpp" || ext == ".hxx")) {
                String includeRoot = inferIncludeRoot(path);
                if (!includeRoot.isEmpty()) {
                    Language** langPtr = _langsByName.get("cpp");
                    if (langPtr) {
                        Languages::CppLanguage* cppLang = dynamic_cast<Languages::CppLanguage*>(*langPtr);
                        if (cppLang) {
                            bool exists = false;
                            for (usz j = 0; j < cppLang->preprocessor().includePaths.size(); ++j) {
                                if (cppLang->preprocessor().includePaths[j] == includeRoot) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists) {
                                cppLang->preprocessor().includePaths.push(includeRoot);
                            }
                        }
                    }
                }
            }

            // Parse imports
            Language** langPtr = _langsByName.get(langName);
            if (!langPtr) continue;

            Array<ImportSpec> imports = (*langPtr)->parseImports(
                _inputs[i].content, path);

            for (usz j = 0; j < imports.size(); ++j) {
                String resolved = resolveImport(imports[j], path);

                if (detectLanguage(resolved).isEmpty()) {
                    const char* tryExts[] = {
                        ".cpp", ".c", ".hpp", ".h",
                        ".js", ".ts", ".py", ".wasm", nullptr
                    };
                    for (int e = 0; tryExts[e]; ++e) {
                        String candidate = resolved + tryExts[e];
                        if (onRead) {
                            String test = onRead(candidate);
                            if (test.length() > 0) {
                                resolved = candidate;
                                break;
                            }
                        }
                    }
                }

                discoverFile(resolved);

                if (_graph.hasNode(resolved)) {
                    _graph.addEdge(nodeIdx, _graph.indexOf(resolved));
                }
            }
        } else {
            discoverFile(path);
        }
    }

    // Parse C++ headers to automatically generate WASM bindings and TS glue.
    // Build a set of user-provided input paths so we can distinguish them from
    // transitively-discovered headers.
    Map<String, bool> explicitInputPaths;
    for (usz i = 0; i < _inputs.size(); ++i) {
        explicitInputPaths.set(canonicalize(_inputs[i].name), true);
    }

    String tempDir = getTempDir();
    CppHeaderParser parser;
    Array<String> headerPaths;        // headers to #include in the bridge
    Array<String> headerPathsForParse; // all headers to parse for bindings
    _inferredIncludeRoots.clear();
    Languages::CppPreprocessor preprocessor;
    Map<String, bool> seenHeaders; // dedup: avoid parsing the same header twice
    for (usz i = 0; i < _graph.nodes.size(); ++i) {
        SourceNode& node = _graph.nodes[i];
        String ext;
        long long lastDot = -1;
        for (usz k = 0; k < node.path.size(); ++k) {
            if (node.path.data()[k] == '.') lastDot = (long long)k;
        }
        if (lastDot >= 0) ext = node.path.substring((usz)lastDot);
        if ((node.language == "cpp" || node.language == "c") && (ext == ".h" || ext == ".hpp" || ext == ".hxx")) {
            if (seenHeaders.has(node.path)) continue; // skip duplicates
            seenHeaders.set(node.path, true);
            Languages::PreprocessorResult ppResult;
            String headerPath = canonicalize(node.path);
            if (headerPath.isEmpty()) headerPath = node.path;
            
            if (!isRepl) {
                Array<String> siblings = preprocessor.findSiblingSourceFiles(headerPath);
                for (usz s = 0; s < siblings.size(); ++s) {
                    discoverFile(siblings[s]);
                    if (_graph.hasNode(siblings[s])) {
                        _graph.addEdge(i, _graph.indexOf(siblings[s]));
                    }
                }
            }

            // Skip engine internal, framework, and third-party dependency headers for JS reflection/bindings
            if (node.path.includes("/sew/include/") || node.path.includes("include/Languages/") || node.path.includes("include/Sew/") ||
                node.path.includes("/xic/include/") || node.path.includes("/xic/packages/") ||
                node.path.includes("/deps/") || node.path.includes("/thirdparty/") ||
                node.path.includes("/diligent/") || node.path.includes("/glfw/")) {
                continue;
            }
            ppResult = preprocessor.process(node.content, node.path);
            parser.parse(ppResult.strippedSource);
            headerPathsForParse.push(headerPath);
            String includeRoot = inferIncludeRoot(headerPath);
            if (!includeRoot.isEmpty() && !containsPath(_inferredIncludeRoots, includeRoot)) {
                _inferredIncludeRoots.push(includeRoot);
            }
            // Only add to bridge #includes if this was an explicit user input
            if (explicitInputPaths.has(headerPath)) {
                headerPaths.push(headerPath);
            }
        }
    }

    g_allParsedClasses = parser.classes;

    if (parser.classes.size() > 0 || parser.functions.size() > 0) {
        // Generate C++ bridge
        String bridgeCode = BindingGenerator::generateCppBridge(parser.classes, parser.functions, parser.namespaces, headerPathsForParse);

        // Add to graph
        String bridgePath = tempDir + "/sew_bridge.cpp";
        usz bridgeIdx = _graph.addNode(bridgePath, "cpp");
        _graph.nodes[bridgeIdx].content = bridgeCode;

        // Add dependency edges from sew_bridge.cpp to each header
        for (usz h = 0; h < headerPathsForParse.size(); ++h) {
            usz hIdx = _graph.indexOf(headerPathsForParse[h]);
            if (hIdx != (usz)-1) _graph.addEdge(bridgeIdx, hIdx);
        }

        // Determine WASM name
        String wasmName = "sew.wasm";
        if (outputPath.length() > 0) {
            long long slashPos = -1;
            for (usz k = 0; k < outputPath.size(); ++k) {
                if (outputPath.data()[k] == '/') slashPos = (long long)k;
            }
            String base = (slashPos >= 0) ? outputPath.substring((usz)slashPos + 1) : outputPath;
            long long dotPos = -1;
            for (usz k = 0; k < base.size(); ++k) {
                if (base.data()[k] == '.') dotPos = (long long)k;
            }
            if (dotPos >= 0) {
                wasmName = base.substring(0, (usz)dotPos) + ".wasm";
            } else {
                wasmName = base + ".wasm";
            }
        }

        // Add sew_qjs_bindings.cpp and sew_bridge.js only if JS target is involved or isRepl is true
        bool hasJs = false;
        for (usz i = 0; i < _graph.nodes.size(); ++i) {
            if (_graph.nodes[i].language == "js") {
                hasJs = true;
                break;
            }
        }

        if (hasJs || isRepl) {
            // Generate TS glue and store it
            _generatedTsGlue = BindingGenerator::generateTsGlue(parser.classes, parser.functions, wasmName);

            // Generate JS glue and store it
            _generatedJsGlue = BindingGenerator::generateJsGlue(parser.classes, parser.functions);

            // Generate QuickJS bindings and store it
            _generatedQuickjsBindings = BindingGenerator::generateQuickjsBindings(parser.classes, parser.functions);

            // Add sew_bridge.js to graph
            String jsGluePath = tempDir + "/sew_bridge.js";
            usz jsGlueIdx = _graph.addNode(jsGluePath, "js");
            _graph.nodes[jsGlueIdx].content = _generatedJsGlue;

            // Make sew_bridge.js depend on sew_bridge.cpp
            _graph.addEdge(jsGlueIdx, bridgeIdx);

            // Add sew_qjs_bindings.cpp to graph
            String qjsBindingsPath = tempDir + "/sew_qjs_bindings.cpp";
            usz qjsIdx = _graph.addNode(qjsBindingsPath, "cpp");
            _graph.nodes[qjsIdx].content = _generatedQuickjsBindings;

            // Make sew_qjs_bindings.cpp depend on all headers
            for (usz h = 0; h < headerPathsForParse.size(); ++h) {
                usz hIdx = _graph.indexOf(headerPathsForParse[h]);
                if (hIdx != (usz)-1) _graph.addEdge(qjsIdx, hIdx);
            }
        }
    }

    // Auto-discover Xylem core sources if xylem is imported
    String xylemRoot;
    for (usz i = 0; i < _inferredIncludeRoots.size(); ++i) {
        String includeRoot = _inferredIncludeRoots[i];
        String xylemRootCand = includeRoot;
        if (includeRoot.endsWith("/include")) {
            xylemRootCand = includeRoot.substring(0, includeRoot.length() - 8);
        }
        String testPath = xylemRootCand + "/src/Xylem/Xylem.cpp";
        if (onRead) {
            String test = onRead(testPath);
            if (test.length() > 0) {
                xylemRoot = xylemRootCand;
                break;
            }
        }
    }
    if (!xylemRoot.isEmpty()) {
        const char* names[] = {
            "Allocator", "BlobStore", "BlockDevice", "Cache", "Journal",
            "QueryParser", "TableStore", "XBDiff", "Xylem", "Watcher", nullptr
        };
        for (int i = 0; names[i]; ++i) {
            String src = xylemRoot + "/src/Xylem/" + names[i] + ".cpp";
            discoverFile(src);
        }
    }

    // Compute build plan
    _plan = _graph.computeBuildPlan();



}

bool Engine::build(const String& targetName) {
    Target* target = targetByName(targetName);
    if (!target) {
        if (onError) onError("Unknown target: " + targetName);
        return false;
    }

    String tempDir = getTempDir();
    // printf("Engine::build target: %s, nodes: %d, steps: %d, onCacheHas: %s\n",
        //    targetName.c_str(), (int)_graph.nodes.size(), (int)_plan.steps.size(),
        //    onCacheHas ? "yes" : "no");
    // fflush(stdout);

    Map<String, bool> explicitInputPaths;
    for (usz i = 0; i < _inputs.size(); ++i) {
        explicitInputPaths.set(canonicalize(_inputs[i].name), true);
    }

    // Assign compile forms based on target and precompute content hashes
    for (usz i = 0; i < _graph.nodes.size(); ++i) {
        _graph.nodes[i].form = target->formFor(_graph.nodes[i].language);
        _graph.nodes[i].contentHash = Cache::hashContent(_graph.nodes[i].content);
    }

    // ─── Level 2 Global Build Cache Check ─────────────────────────────────
    String globalInput = targetName;
    globalInput += ":";
    
    std::vector<usz> sortedNodeIndices;
    for (usz i = 0; i < _graph.nodes.size(); ++i) {
        sortedNodeIndices.push_back(i);
    }
    std::sort(sortedNodeIndices.begin(), sortedNodeIndices.end(), [&](usz a, usz b) {
        return makeRelativePath(_graph.nodes[a].path) < makeRelativePath(_graph.nodes[b].path);
    });

    for (usz i = 0; i < sortedNodeIndices.size(); ++i) {
        usz nodeIdx = sortedNodeIndices[i];
        globalInput += makeRelativePath(_graph.nodes[nodeIdx].path);
        globalInput += "=";
        globalInput += _graph.nodes[nodeIdx].contentHash;
        globalInput += ";";
    }

    globalInput += _generatedTsGlue;
    globalInput += "|";
    globalInput += _generatedJsGlue;
    globalInput += "|";
    globalInput += _generatedQuickjsBindings;

    String globalKey = Cache::computeKey(globalInput, targetName, {}, {});

    if (onCacheHas && onCacheHas(globalKey)) {
        String cachedOutContent = onCacheGet(globalKey);
        if (cachedOutContent.length() > 0) {
            bool wasmOk = true;
            String cachedWasmContent;
            String wasmOutput;
            if (targetName == "js") {
                wasmOutput = outputPath;
                long long dotPos = -1;
                for (usz i = 0; i < wasmOutput.size(); ++i) {
                    if (wasmOutput.data()[i] == '.') dotPos = (long long)i;
                }
                if (dotPos >= 0) {
                    wasmOutput = wasmOutput.substring(0, (usz)dotPos);
                }
                wasmOutput += ".wasm";

                if (onCacheHas(globalKey + "_wasm")) {
                    cachedWasmContent = onCacheGet(globalKey + "_wasm");
                    if (cachedWasmContent.length() == 0) {
                        wasmOk = false;
                    }
                } else {
                    wasmOk = false;
                }
            }

            if (wasmOk) {
                FILE* fOut = fopen(outputPath.c_str(), "wb");
                if (fOut) {
                    fwrite(cachedOutContent.data(), 1, cachedOutContent.size(), fOut);
                    fclose(fOut);
                }
                if (targetName == "js" && cachedWasmContent.length() > 0) {
                    FILE* fWasm = fopen(wasmOutput.c_str(), "wb");
                    if (fWasm) {
                        fwrite(cachedWasmContent.data(), 1, cachedWasmContent.size(), fWasm);
                        fclose(fWasm);
                    }
                }
                if (onInfo) {
                    onInfo("Global build cache hit! Restored outputs instantly.");
                }
                if (onFinish) {
                    onFinish(outputPath);
                }
                return true;
            }
        }
    }

    Array<CompileResult> allResults;
    usz totalNodes = _graph.nodes.size();
    usz compiled = 0;

    // Execute build plan step by step
    for (usz stepIdx = 0; stepIdx < _plan.steps.size(); ++stepIdx) {
        BuildStep& step = _plan.steps[stepIdx];

        unsigned int numCores = std::thread::hardware_concurrency();
        if (numCores == 0) numCores = 4;

        usz numNodesInStep = step.nodeIndices.size();
        usz nextNodeIdx = 0;
        std::mutex queueMutex;
        std::mutex stateMutex;
        bool stepSuccess = true;
        String stepErrors;

        std::vector<std::thread> workers;
        for (unsigned int t = 0; t < numCores; ++t) {
            workers.push_back(std::thread([&]() {
                for (;;) {
                    usz j = 0;
                    {
                        std::lock_guard<std::mutex> lock(queueMutex);
                        if (!stepSuccess || nextNodeIdx >= numNodesInStep) {
                            break;
                        }
                        j = nextNodeIdx++;
                    }

                    usz nodeIdx = step.nodeIndices[j];
                    SourceNode& node = _graph.nodes[nodeIdx];

                    // All String manipulation on shared state / nodes must be locked because String is non-atomic COW.
                    String cacheKey;
                    bool cachedHit = false;
                    String cachedPath;
                    Language* lang = nullptr;
                    CompileRequest req;
                    bool skip = false;
                    bool isBridgeCpp = false;
                    bool isBridgeJs = false;
                    String nodePath;
                    String nodeContent;

                    {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        if (node.compiled) {
                            skip = true;
                        } else {
                            nodePath = node.path;
                            nodeContent = node.content;

                            String ext;
                            long long lastDot = -1;
                            for (usz k = 0; k < nodePath.size(); ++k) {
                                if (nodePath.data()[k] == '.') lastDot = (long long)k;
                            }
                            if (lastDot >= 0) ext = nodePath.substring((usz)lastDot);
                            if (ext == ".h" || ext == ".hpp" || ext == ".hxx") {
                                node.compiled = true;
                                compiled++;
                                skip = true;
                            } else if (isRepl && (node.language == "cpp" || node.language == "c")) {
                                bool isGenerated = nodePath.endsWith("sew_bridge.cpp") || nodePath.endsWith("sew_qjs_bindings.cpp");
                                if (!isGenerated) {
                                    node.compiled = true;
                                    compiled++;
                                    skip = true;
                                }
                            }

                            if (!skip) {
                                isBridgeCpp = nodePath.endsWith("sew_bridge.cpp");
                                isBridgeJs = nodePath.endsWith("sew_bridge.js");

                                // Check cache
                                bool isPic = outputPath.endsWith(".so");
                                cacheKey = Cache::computeKey(
                                    nodeContent + (isPic ? ":fPIC" : ""), targetName, {}, {});

                                if (onCacheHas && onCacheHas(cacheKey)) {
                                    cachedHit = true;
                                    compiled++;
                                    if (onProgress) {
                                        onProgress("Cached", compiled, totalNodes);
                                    }
                                    cachedPath = onCacheGet(cacheKey);
                                    
                                    CompileResult res;
                                    res.outputPath = cachedPath;
                                    res.success = true;
                                    allResults.push(Xi::Move(res));
                                    node.compiled = true;
                                    skip = true;
                                } else {
                                    Language** langPtr = _langsByName.get(node.language);
                                    if (langPtr) {
                                        lang = *langPtr;
                                        
                                        req.sourcePath = nodePath;
                                        req.sourceContent = nodeContent;
                                        req.form = node.form;
                                        req.targetTriple = target->triple();
                                        req.assetsDir = assetsDir;
                                        if (isPic) {
                                            req.flags.push("-fPIC");
                                        }
                                        for (usz k = 0; k < includePaths.size(); ++k) {
                                            req.includePaths.push(includePaths[k]);
                                        }
                                        for (usz k = 0; k < _inferredIncludeRoots.size(); ++k) {
                                            req.includePaths.push(_inferredIncludeRoots[k]);
                                        }
                                        if (nodePath.indexOf("sew_qjs_bindings.cpp") != (usz)-1) {
                                            char pathBuf[1024];
                                            ssize_t len = ::readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
                                            if (len != -1) {
                                                pathBuf[len] = '\0';
                                                char* lastSlash = strrchr(pathBuf, '/');
                                                if (lastSlash) {
                                                    *lastSlash = '\0';
                                                    String execDir(pathBuf);
                                                    String qjsPath = execDir + "/_deps/quickjs_src-src";
                                                    struct stat st;
                                                    if (::stat(qjsPath.c_str(), &st) != 0) {
                                                        qjsPath = execDir + "/../thirdparty/quickjs";
                                                    }
                                                    req.includePaths.push(qjsPath);
                                                }
                                            }
                                        }

                                        String outPath = nodePath;
                                        if (node.language == "cpp" || node.language == "c" || node.form == CompileForm::Bytecode) {
                                            String safePath = nodePath;
                                            safePath = safePath.replace("/", "_");
                                            safePath = safePath.replace("\\", "_");
                                            safePath = safePath.replace(".", "_");
                                            outPath = tempDir + "/sew_obj_" + safePath + ".o";
                                        }
                                        req.outputPath = outPath;
                                    } else {
                                        skip = true;
                                    }
                                }
                            }
                        }
                    }

                    if (skip) {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        req = CompileRequest();
                        nodePath.clear();
                        nodeContent.clear();
                        cacheKey.clear();
                        cachedPath.clear();
                        continue;
                    }

                    if (isBridgeCpp || isBridgeJs) {
                        FILE* f = fopen(nodePath.c_str(), "w");
                        if (f) {
                            fwrite(nodeContent.data(), 1, nodeContent.size(), f);
                            fclose(f);
                        }
                    }

                    CompileResult res;
                    if (lang) {
                        res = lang->compile(req);
                    }

                    {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        if (lang) {
                            if (!res.success) {
                                stepSuccess = false;
                                stepErrors = "Failed to compile " + nodePath + ": " + res.errors;
                            } else {
                                if (onCacheSet && res.outputPath.length() > 0) {
                                    onCacheSet(cacheKey, res.outputPath);
                                }
                                allResults.push(Xi::Move(res));
                                node.compiled = true;
                                compiled++;
                                if (onProgress) {
                                    onProgress("Compiling", compiled, totalNodes);
                                }
                            }
                        }
                        req = CompileRequest();
                        nodePath.clear();
                        nodeContent.clear();
                        cacheKey.clear();
                        cachedPath.clear();
                    }
                }
            }));
        }

        for (auto& worker : workers) {
            worker.join();
        }

        if (!stepSuccess) {
            if (onError) onError(stepErrors);
            return false;
        }
    }

    // Link

    LinkRequest linkReq;
    linkReq.units = Xi::Move(allResults);

    // Add precompiled library archives/shared libraries to LinkRequest
    for (usz i = 0; i < _graph.nodes.size(); ++i) {
        if (_graph.nodes[i].language == "lib") {
            CompileResult libRes;
            libRes.outputPath = _graph.nodes[i].path;
            libRes.success = true;
            linkReq.units.push(Xi::Move(libRes));
        }
    }

    linkReq.outputPath = outputPath;
    linkReq.assetsDir = assetsDir;
    linkReq.tsGlue = _generatedTsGlue;
    linkReq.jsGlue = _generatedJsGlue;
    linkReq.quickjsBindings = _generatedQuickjsBindings;

    LinkResult linkResult = target->link(linkReq);

    if (!linkResult.success) {
        if (onError) onError("Link failed: " + linkResult.errors);
        return false;
    }

    if (onFinish) onFinish(linkResult.outputPath);

    // Save to global build cache
    if (onCacheSet) {
        FILE* fOut = fopen(outputPath.c_str(), "rb");
        if (fOut) {
            fseek(fOut, 0, SEEK_END);
            long long outSize = ftell(fOut);
            fseek(fOut, 0, SEEK_SET);
            if (outSize > 0) {
                String outContent;
                outContent.allocate((usz)outSize);
                fread((void*)outContent.data(), 1, (usz)outSize, fOut);
                onCacheSet(globalKey, outContent);
            }
            fclose(fOut);
        }

        if (targetName == "js") {
            String wasmOutput = outputPath;
            long long dotPos = -1;
            for (usz i = 0; i < wasmOutput.size(); ++i) {
                if (wasmOutput.data()[i] == '.') dotPos = (long long)i;
            }
            if (dotPos >= 0) {
                wasmOutput = wasmOutput.substring(0, (usz)dotPos);
            }
            wasmOutput += ".wasm";

            FILE* fWasm = fopen(wasmOutput.c_str(), "rb");
            if (fWasm) {
                fseek(fWasm, 0, SEEK_END);
                long long wasmSize = ftell(fWasm);
                fseek(fWasm, 0, SEEK_SET);
                if (wasmSize > 0) {
                    String wasmContent;
                    wasmContent.allocate((usz)wasmSize);
                    fread((void*)wasmContent.data(), 1, (usz)wasmSize, fWasm);
                    onCacheSet(globalKey + "_wasm", wasmContent);
                }
                fclose(fWasm);
            }
        }
    }

    // Clean up temporary sew_bridge files
    // ::unlink((tempDir + "/sew_bridge.cpp").c_str());
    // ::unlink((tempDir + "/sew_bridge.js").c_str());
    // ::unlink((tempDir + "/sew_obj__tmp_sew_bridge.o").c_str());
    return true;
}

void Engine::eval(const String& language) {
    if (!_evalCtx.hasContext()) {
        String soPath = "";
        if (outputPath.endsWith(".so") && ::access(outputPath.c_str(), 0) == 0) {
            soPath = outputPath;
        }
        _evalCtx.init(language, soPath, _generatedJsGlue);
    }
}

String Engine::evalCode(const String& code) {
    return _evalCtx.eval(code);
}

void Engine::destroy() {
    _graph.clear();
    _inputs.clear();
    _inferredIncludeRoots.clear();
    _evalCtx.destroy();
}

} // namespace Sew
