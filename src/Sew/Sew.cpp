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

    char cwd[1024];
    if (::getcwd(cwd, sizeof(cwd))) {
        String tempDir = String(cwd) + "/.sew";
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
    Language* lang = languageFor(ext);
    return lang ? lang->name() : "";
}

String Engine::resolveImport(const ImportSpec& imp, const String& currentFile) {
    String spec = imp.specifier;
    String res = spec;

    // If already absolute or relative, use as-is
    if (spec.startsWith("/") || spec.startsWith("./") || spec.startsWith("../")) {
        res = spec;
    } else {
        // Resolve relative to current file's directory
        long long lastSlash = -1;
        for (usz i = 0; i < currentFile.size(); ++i) {
            if (currentFile.data()[i] == '/') lastSlash = (long long)i;
        }

        if (lastSlash >= 0) {
            String dir = currentFile.substring(0, (usz)lastSlash);
            res = dir + "/" + spec;
        }
    }

    return canonicalize(res);
}

void Engine::discoverFile(const String& rawPath) {
    String path = canonicalize(rawPath);
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

    // Get language and parse imports
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
        if (node.language == "cpp" && (ext == ".h" || ext == ".hpp" || ext == ".hxx")) {
            if (seenHeaders.has(node.path)) continue; // skip duplicates
            seenHeaders.set(node.path, true);
            // Skip engine internal and framework headers
            if (node.path.includes("/sew/include/") || node.path.includes("include/Languages/") || node.path.includes("include/Sew/") ||
                node.path.includes("/xic/include/") || node.path.includes("/xic/packages/")) {
                continue;
            }
            Languages::PreprocessorResult ppResult = preprocessor.process(node.content, node.path);
            parser.parse(ppResult.strippedSource);
            for (const auto& cls : parser.classes) {
                fprintf(stderr, "[PARSED CLASS] File: %s, Class: %s\n", node.path.c_str(), cls.name.c_str());
                for (const auto& f : cls.fields) {
                    fprintf(stderr, "  [FIELD] %s : %s\n", f.name.c_str(), f.type.c_str());
                }
            }
            String headerPath = canonicalize(node.path);
            if (headerPath.isEmpty()) headerPath = node.path;
            headerPathsForParse.push(headerPath);
            String includeRoot = inferIncludeRoot(headerPath);
            if (!includeRoot.isEmpty() && !containsPath(_inferredIncludeRoots, includeRoot)) {
                _inferredIncludeRoots.push(includeRoot);
            }
            Array<String> siblings = preprocessor.findSiblingSourceFiles(headerPath);
            for (usz s = 0; s < siblings.size(); ++s) {
                discoverFile(siblings[s]);
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



    if (onInfo) {
        String msg = "Found ";
        msg += String((long long)_graph.nodes.size());
        msg += " files in ";
        msg += String((long long)_plan.steps.size());
        msg += " build steps";
        onInfo(msg);
    }
}

bool Engine::build(const String& targetName) {
    Target* target = targetByName(targetName);
    if (!target) {
        if (onError) onError("Unknown target: " + targetName);
        return false;
    }

    String tempDir = getTempDir();

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

                    if (node.compiled) continue;

                    String ext;
                    long long lastDot = -1;
                    for (usz k = 0; k < node.path.size(); ++k) {
                        if (node.path.data()[k] == '.') lastDot = (long long)k;
                    }
                    if (lastDot >= 0) ext = node.path.substring((usz)lastDot);
                    if (ext == ".h" || ext == ".hpp" || ext == ".hxx") {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        node.compiled = true;
                        compiled++;
                        continue;
                    }

                    // Check cache
                    String contentHash = node.contentHash;
                    Array<String> depHashes;
                    for (usz d = 0; d < node.dependencies.size(); ++d) {
                        depHashes.push(
                            _graph.nodes[node.dependencies[d]].contentHash);
                    }
                    String cacheKey = Cache::computeKey(
                        node.content, targetName, {}, depHashes);

                    bool cachedHit = false;
                    String cachedPath;
                    {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        if (!node.path.endsWith("sew_bridge.cpp") && !node.path.endsWith("sew_bridge.js") && onCacheHas && onCacheHas(cacheKey)) {
                            cachedHit = true;
                            if (onProgress) {
                                compiled++;
                                onProgress("Cached", compiled, totalNodes);
                            }
                            cachedPath = onCacheGet(cacheKey);
                        }
                    }

                    if (cachedHit) {
                        CompileResult res;
                        res.outputPath = cachedPath;
                        res.success = true;
                        
                        std::lock_guard<std::mutex> lock(stateMutex);
                        allResults.push(Xi::Move(res));
                        node.compiled = true;
                        continue;
                    }

                    if (node.path.endsWith("sew_bridge.cpp")) {
                        FILE* f = fopen(node.path.c_str(), "w");
                        if (f) {
                            fwrite(node.content.data(), 1, node.content.size(), f);
                            fclose(f);
                        }
                    }
                    if (node.path.endsWith("sew_bridge.js")) {
                        FILE* f = fopen(node.path.c_str(), "w");
                        if (f) {
                            fwrite(node.content.data(), 1, node.content.size(), f);
                            fclose(f);
                        }
                    }

                    Language** langPtr = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        langPtr = _langsByName.get(node.language);
                    }
                    if (!langPtr) continue;

                    CompileRequest req;
                    req.sourcePath = node.path;
                    req.sourceContent = node.content;
                    req.form = node.form;
                    req.targetTriple = target->triple();
                    req.assetsDir = assetsDir;
                    for (usz k = 0; k < includePaths.size(); ++k) {
                        req.includePaths.push(includePaths[k]);
                    }
                    for (usz k = 0; k < _inferredIncludeRoots.size(); ++k) {
                        req.includePaths.push(_inferredIncludeRoots[k]);
                    }

                    String outPath = node.path;
                    if (node.language == "cpp" || node.language == "c" || node.form == CompileForm::Bytecode) {
                        String safePath = node.path;
                        safePath = safePath.replace("/", "_");
                        safePath = safePath.replace("\\", "_");
                        
                        long long dotPos = -1;
                        for (usz k = 0; k < safePath.size(); ++k) {
                            if (safePath.data()[k] == '.') dotPos = (long long)k;
                        }
                        if (dotPos >= 0) {
                            safePath = safePath.substring(0, (usz)dotPos);
                        }
                        outPath = tempDir + "/sew_obj_" + safePath + ".o";
                    }
                    req.outputPath = outPath;



                    CompileResult res = (*langPtr)->compile(req);



                    if (!res.success) {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        stepSuccess = false;
                        stepErrors = "Failed to compile " + node.path + ": " + res.errors;
                        break;
                    }

                    {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        if (!node.path.endsWith("sew_bridge.cpp") && onCacheSet && res.outputPath.length() > 0) {
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
    if (onInfo) onInfo("Linking...");

    LinkRequest linkReq;
    linkReq.units = Xi::Move(allResults);

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
        _evalCtx.init(language);
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
