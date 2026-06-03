/**
 * @file Sew.cpp
 * @brief Sew engine implementation — the heart of the polyglot build system.
 */

#include <Sew/Sew.hpp>
#include <Sew/Parser.hpp>
#include <Sew/Generator.hpp>
#include <cstdio>

namespace Sew {

static String canonicalize(const String& path) {
    Array<String> parts = path.split("/");
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
    if (path.startsWith("/")) res += "/";
    for (usz i = 0; i < clean.size(); ++i) {
        if (i > 0) res += "/";
        res += clean[i];
    }
    return res;
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

    if (path.endsWith("String.cpp")) {
        fprintf(stderr, "=== DISCOVER STRING.CPP ===\n");
        fprintf(stderr, "  Path: %s\n", path.c_str());
        fprintf(stderr, "  Content size: %zu\n", content.size());
    }

    if (content.isEmpty()) {
        // File might not exist (e.g. candidate sibling) — skip silently
        return;
    }

    usz nodeIdx = _graph.addNode(path, langName);
    _graph.nodes[nodeIdx].content = content;

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

    // Parse C++ headers to automatically generate WASM bindings and TS glue
    CppHeaderParser parser;
    Array<String> headerPaths;
    for (usz i = 0; i < _graph.nodes.size(); ++i) {
        SourceNode& node = _graph.nodes[i];
        String ext;
        long long lastDot = -1;
        for (usz k = 0; k < node.path.size(); ++k) {
            if (node.path.data()[k] == '.') lastDot = (long long)k;
        }
        if (lastDot >= 0) ext = node.path.substring((usz)lastDot);
        if (node.language == "cpp" && (ext == ".h" || ext == ".hpp" || ext == ".hxx")) {
            parser.parse(node.content);
            headerPaths.push(node.path);
        }
    }

    if (parser.classes.size() > 0 || parser.functions.size() > 0) {
        // Generate C++ bridge
        String bridgeCode = BindingGenerator::generateCppBridge(parser.classes, parser.functions, parser.namespaces, headerPaths);

        // Add to graph
        String bridgePath = "sew_bridge.cpp";
        usz bridgeIdx = _graph.addNode(bridgePath, "cpp");
        _graph.nodes[bridgeIdx].content = bridgeCode;

        // Add dependency edges from sew_bridge.cpp to each header file in headerPaths
        for (usz h = 0; h < headerPaths.size(); ++h) {
            _graph.addEdge(bridgeIdx, _graph.indexOf(headerPaths[h]));
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

void Engine::build(const String& targetName) {
    Target* target = targetByName(targetName);
    if (!target) {
        if (onError) onError("Unknown target: " + targetName);
        return;
    }

    // Assign compile forms based on target
    for (usz i = 0; i < _graph.nodes.size(); ++i) {
        _graph.nodes[i].form = target->formFor(_graph.nodes[i].language);
    }

    fprintf(stderr, "=== GRAPH NODES ===\n");
    for (usz i = 0; i < _graph.nodes.size(); ++i) {
        fprintf(stderr, "  [%zu] %s (lang: %s, compiled: %s)\n", i, _graph.nodes[i].path.c_str(), _graph.nodes[i].language.c_str(), _graph.nodes[i].compiled ? "yes" : "no");
    }

    Array<CompileResult> allResults;
    usz totalNodes = _graph.nodes.size();
    usz compiled = 0;

    // Execute build plan step by step
    for (usz stepIdx = 0; stepIdx < _plan.steps.size(); ++stepIdx) {
        BuildStep& step = _plan.steps[stepIdx];

        for (usz j = 0; j < step.nodeIndices.size(); ++j) {
            usz nodeIdx = step.nodeIndices[j];
            SourceNode& node = _graph.nodes[nodeIdx];
            fprintf(stderr, "Processing node [%zu]: %s\n", nodeIdx, node.path.c_str());

            if (node.compiled) continue;

            // Skip header files — they're included, not compiled
            String ext;
            long long lastDot = -1;
            for (usz k = 0; k < node.path.size(); ++k) {
                if (node.path.data()[k] == '.') lastDot = (long long)k;
            }
            if (lastDot >= 0) ext = node.path.substring((usz)lastDot);
            if (ext == ".h" || ext == ".hpp" || ext == ".hxx") {
                node.compiled = true;
                compiled++;
                continue;
            }

            // Check cache
            String contentHash = Cache::hashContent(node.content);
            Array<String> depHashes;
            for (usz d = 0; d < node.dependencies.size(); ++d) {
                depHashes.push(Cache::hashContent(
                    _graph.nodes[node.dependencies[d]].content));
            }
            String cacheKey = Cache::computeKey(
                node.content, targetName, {}, depHashes);

            if (node.path != "sew_bridge.cpp" && onCacheHas && onCacheHas(cacheKey)) {
                if (onProgress) {
                    compiled++;
                    onProgress("Cached", compiled, totalNodes);
                }
                String cached = onCacheGet(cacheKey);
                CompileResult res;
                res.outputPath = cached;
                res.success = true;
                allResults.push(Xi::Move(res));
                node.compiled = true;
                continue;
            }

            // If this is the dynamically generated sew_bridge.cpp, write it to disk
            if (node.path == "sew_bridge.cpp") {
                FILE* f = fopen("sew_bridge.cpp", "w");
                if (f) {
                    fwrite(node.content.data(), 1, node.content.size(), f);
                    fclose(f);
                }
            }

            // Compile
            Language** langPtr = _langsByName.get(node.language);
            if (!langPtr) continue;

            CompileRequest req;
            req.sourcePath = node.path;
            req.sourceContent = node.content;
            req.form = node.form;
            req.targetTriple = target->triple();
            req.assetsDir = assetsDir;

            // Output path: same name but .o extension in build dir
            String outPath = node.path;
            if (lastDot >= 0) {
                outPath = node.path.substring(0, (usz)lastDot);
            }
            outPath += ".o";
            req.outputPath = outPath;

            CompileResult res = (*langPtr)->compile(req);

            if (!res.success) {
                if (onError) {
                    onError("Failed to compile " + node.path + ": " + res.errors);
                }
                return;
            }

            // Cache the result
            if (node.path != "sew_bridge.cpp" && onCacheSet && res.outputPath.length() > 0) {
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

    // Link
    if (onInfo) onInfo("Linking...");

    LinkRequest linkReq;
    linkReq.units = Xi::Move(allResults);
    linkReq.outputPath = outputPath;
    linkReq.assetsDir = assetsDir;
    linkReq.tsGlue = _generatedTsGlue;

    LinkResult linkResult = target->link(linkReq);

    if (!linkResult.success) {
        if (onError) onError("Link failed: " + linkResult.errors);
        return;
    }

    if (onFinish) onFinish(linkResult.outputPath);

    // Clean up temporary sew_bridge files
    remove("sew_bridge.cpp");
    remove("sew_bridge.o");
}

void Engine::eval(const String& language) {
    _evalCtx.init(language);
}

void Engine::destroy() {
    _graph.clear();
    _inputs.clear();
    _evalCtx.destroy();
}

} // namespace Sew
