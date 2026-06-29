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

#include <cstdint> // Ensure standard integer types are included

// --- High-Performance Binary String Helpers ---


namespace Sew {



static String readBinaryString(FILE* f, char* buffer, size_t bufferSize) {
    uint32_t len = 0;
    if (::fread(&len, sizeof(len), 1, f) != 1) return "";
    if (len == 0) return "";
    
    if (len < bufferSize) {
        if (::fread(buffer, 1, len, f) != len) return "";
        buffer[len] = '\0';
        return String(buffer);
    } else {
        char* heapBuf = new char[len + 1];
        if (::fread(heapBuf, 1, len, f) != len) {
            delete[] heapBuf;
            return "";
        }
        heapBuf[len] = '\0';
        String res(heapBuf);
        delete[] heapBuf;
        return res;
    }
}

static void writeBinaryString(FILE* f, const String& str) {
    uint32_t len = (uint32_t)str.length();
    ::fwrite(&len, sizeof(len), 1, f);
    if (len > 0) {
        ::fwrite(str.c_str(), 1, len, f);
    }
}

// --- Binary Database Implementation ---

void Engine::loadDepDb() {
    _cachedFiles.clear();
    const char* home = ::getenv("HOME");
    if (!home) home = "/tmp";
    String dbPath = String(home) + "/.cache/sew/dep_db.bin"; // Changed from .txt to .bin
    
    FILE* f = fopen(dbPath.c_str(), "rb"); // Open in binary read mode
    if (!f) return;
    
    // Validate Magic Number & Version
    uint32_t magic = 0;
    uint32_t version = 0;
    if (::fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x53455744 || // "SEWD" in hex
        ::fread(&version, sizeof(version), 1, f) != 1 || version != 1) {
        fclose(f);
        return; // Invalid or outdated database format
    }
    
    uint32_t entryCount = 0;
    if (::fread(&entryCount, sizeof(entryCount), 1, f) != 1) {
        fclose(f);
        return;
    }
    
    char stackBuffer[4096]; // Stack buffer to avoid heap allocations during read
    
    for (uint32_t i = 0; i < entryCount; ++i) {
        CachedFileEntry entry;
        entry.language = readBinaryString(f, stackBuffer, sizeof(stackBuffer));
        
        if (::fread(&entry.mtime, sizeof(entry.mtime), 1, f) != 1 ||
            ::fread(&entry.size, sizeof(entry.size), 1, f) != 1) {
            break;
        }
        
        entry.contentHash = readBinaryString(f, stackBuffer, sizeof(stackBuffer));
        entry.path = readBinaryString(f, stackBuffer, sizeof(stackBuffer));
        
        uint32_t importCount = 0;
        if (::fread(&importCount, sizeof(importCount), 1, f) != 1) {
            break;
        }
        
        for (uint32_t j = 0; j < importCount; ++j) {
            entry.resolvedImports.push(readBinaryString(f, stackBuffer, sizeof(stackBuffer)));
        }
        
        _cachedFiles.set(entry.path, entry);
    }
    
    fclose(f);
}

void Engine::saveDepDb() {
    const char* home = ::getenv("HOME");
    if (!home) home = "/tmp";
    String dbDir = String(home) + "/.cache/sew";
    ::mkdir(dbDir.c_str(), 0755);
    String dbPath = dbDir + "/dep_db.bin"; // Changed from .txt to .bin
    
    FILE* f = fopen(dbPath.c_str(), "wb"); // Open in binary write mode
    if (!f) return;
    
    // Write Header: Magic Number ("SEWD") + Version (1)
    uint32_t magic = 0x53455744;
    uint32_t version = 1;
    ::fwrite(&magic, sizeof(magic), 1, f);
    ::fwrite(&version, sizeof(version), 1, f);
    
    uint32_t entryCount = (uint32_t)_cachedFiles.size();
    ::fwrite(&entryCount, sizeof(entryCount), 1, f);
    
    for (auto& kv : _cachedFiles) {
        CachedFileEntry& entry = kv.value;
        
        writeBinaryString(f, entry.language);
        ::fwrite(&entry.mtime, sizeof(entry.mtime), 1, f);
        ::fwrite(&entry.size, sizeof(entry.size), 1, f);
        writeBinaryString(f, entry.contentHash);
        writeBinaryString(f, entry.path);
        
        uint32_t importCount = (uint32_t)entry.resolvedImports.size();
        ::fwrite(&importCount, sizeof(importCount), 1, f);
        
        for (usz j = 0; j < entry.resolvedImports.size(); ++j) {
            writeBinaryString(f, entry.resolvedImports[j]);
        }
    }
    
    fclose(f);
}


static void collectDepHashesRecursive(const DepGraph& graph, usz nodeIdx, Array<String>& hashes, Array<bool>& visited);

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
        String tempDir = String(home) + "/.cache/sew";
        struct stat st;
        if (::stat(tempDir.c_str(), &st) != 0) {
            ::mkdir(tempDir.c_str(), 0755);
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

static bool getFileStat(const String& path, long long& mtime, long long& size) {
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        mtime = (long long)st.st_mtime;
        size = (long long)st.st_size;
        return true;
    }
    return false;
}

void Engine::discoverFile(const String& rawPath) {
    String path = canonicalize(rawPath);
    if (_graph.hasNode(path)) return;

     if (path.includes("/sew/src/")) {
        return;
    }

    String langName = detectLanguage(path);
    if (langName.isEmpty()) {
        if (onWarn) onWarn("Unknown file type: " + path);
        return;
    }

    Language** langPtr = _langsByName.get(langName);
    Array<String> currentSearchPaths;
    if (langPtr && langName == "cpp") {
        Languages::CppLanguage* cppLang = dynamic_cast<Languages::CppLanguage*>(*langPtr);
        if (cppLang) {
            currentSearchPaths = cppLang->preprocessor().includePaths;
        }
    }

    bool cacheHit = false;
    Array<String> resolvedImports;
    String contentHash;

    long long mtime = 0, size = 0;
    bool hasStat = getFileStat(path, mtime, size);
    
    CachedFileEntry* cachedEntry = _cachedFiles.get(path);
    if (cachedEntry && hasStat && cachedEntry->mtime == mtime && cachedEntry->size == size && cachedEntry->language == langName) {
        cacheHit = true;
        contentHash = cachedEntry->contentHash;
        resolvedImports = cachedEntry->resolvedImports;
    }

    String content;
    if (!cacheHit) {
        // Read file content
        if (onRead) {
            content = onRead(path);
        }
        if (content.isEmpty()) {
            // File might not exist (e.g. candidate sibling) — skip silently
            return;
        }
        contentHash = Cache::hashContent(content);
    }

    usz nodeIdx;
    {
        std::lock_guard<std::mutex> lock(_compileMutex);
        nodeIdx = _graph.addNode(path, langName);
        _graph.nodes[nodeIdx].content = content;
        _graph.nodes[nodeIdx].contentHash = contentHash;
    }
    _discoveredCount++;

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

    if (cacheHit) {
        // Report progress
        if (onProgress) {
            std::lock_guard<std::mutex> lock(_compileMutex);
            onProgress("Building", _compiledCount.load(), _discoveredCount.load());
        }
        for (usz i = 0; i < resolvedImports.size(); ++i) {
            discoverFile(resolvedImports[i]);
            {
                std::lock_guard<std::mutex> lock(_compileMutex);
                if (_graph.hasNode(resolvedImports[i])) {
                    _graph.addEdge(nodeIdx, _graph.indexOf(resolvedImports[i]));
                }
            }
        }
    } else {
        if (!langPtr) return;
        Language* lang = *langPtr;

        Array<ImportSpec> imports = lang->parseImports(content, path);

        // Report progress
        if (onProgress) {
            std::lock_guard<std::mutex> lock(_compileMutex);
            onProgress("Building", _compiledCount.load(), _discoveredCount.load());
        }

        for (usz i = 0; i < imports.size(); ++i) {
            if (imports[i].isSystem) continue;
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
                    // Check if readable via fast stat check instead of heavy read!
                    if (::access(candidate.c_str(), 0) == 0) {
                        resolved = candidate;
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }

            discoverFile(resolved);

            {
                std::lock_guard<std::mutex> lock(_compileMutex);
                if (_graph.hasNode(resolved)) {
                    _graph.addEdge(nodeIdx, _graph.indexOf(resolved));
                }
            }
            resolvedImports.push(resolved);
        }

        // Save/Update in our database map
        CachedFileEntry entry;
        entry.path = path;
        entry.language = langName;
        entry.mtime = mtime;
        entry.size = size;
        entry.contentHash = contentHash;
        entry.resolvedImports = resolvedImports;
        _cachedFiles.set(path, entry);
    }

    if (_activeTarget) {
        queueCompile(nodeIdx, _activeTarget, _activeTargetName);
    }
}

void Engine::input(const String& name, const String& content) {
    SourceInput inp;
    inp.name = name;
    inp.content = content;
    _inputs.push(Xi::Move(inp));
}

void Engine::find(const String& targetName) {
    _discoveredCount = 0;
    _compiledCount = 0;
    loadDepDb();
    Target* target = nullptr;
    if (!targetName.isEmpty()) {
        target = targetByName(targetName);
        if (target) {
            _activeTarget = target;
            _activeTargetName = targetName;
            startCompileWorkers(target, targetName);
        }
    }

    if (onInfo) onInfo("Discovering dependencies...");

    for (usz i = 0; i < _inputs.size(); ++i) {
        String path = _inputs[i].name;

        // If content was provided inline, add it to graph directly
        if (_inputs[i].content.length() > 0) {
            String langName = detectLanguage(path);
            if (langName.isEmpty()) continue;

            usz nodeIdx = _graph.addNode(path, langName);
            _graph.nodes[nodeIdx].content = _inputs[i].content;
            _discoveredCount++;

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
                if (imports[j].isSystem) continue;
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
    CppHeaderParser headerParser;     // For generating the C++ bridge/JS bindings
    CppHeaderParser cppParser;
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
        if ((node.language == "cpp" || node.language == "c")) {
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

            if (node.path.includes("/sew/include/") || node.path.includes("include/Languages/") || node.path.includes("include/Sew/") ||
                node.path.includes("/deps/") || node.path.includes("/thirdparty/") ||
                node.path.includes("/diligent/") || node.path.includes("/glfw/")) {
                continue;
            }
            if (node.content.isEmpty() && !node.path.isEmpty()) {
                if (onRead) node.content = onRead(node.path);
            }

            ppResult = preprocessor.process(node.content, node.path);
            
            // Always parse into cppParser (so .cpp files get reflection traits)
            cppParser.parse(ppResult.strippedSource);
            
            bool isHeader = (ext == ".h" || ext == ".hpp" || ext == ".hxx");
            if (isHeader) {
                // Only parse into headerParser for sew_bridge.cpp bindings
                headerParser.parse(ppResult.strippedSource);
                headerPathsForParse.push(headerPath);
            }
            
            String includeRoot = inferIncludeRoot(headerPath);
            if (!includeRoot.isEmpty() && !containsPath(_inferredIncludeRoots, includeRoot)) {
                _inferredIncludeRoots.push(includeRoot);
            }
            
            // Only add to bridge #includes if this is a HEADER file AND was an explicit user input!
            if (isHeader && explicitInputPaths.has(headerPath)) {
                headerPaths.push(headerPath);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_parsedClassesMutex);
        Array<ParsedClass> uniqueClasses;
        Map<String, bool> seenClassNames;
        for (usz i = 0; i < cppParser.classes.size(); ++i) { // Changed parser to cppParser
            const String& className = cppParser.classes[i].name;
            if (className.isEmpty() || className.length() == 0) continue;
            
            if (!seenClassNames.has(className)) {
                seenClassNames.set(className, true);
                uniqueClasses.push(cppParser.classes[i]);
            }
        }
        g_allParsedClasses = uniqueClasses;
    }

    Array<ParsedClass> filteredHeaderClasses;
    for (usz i = 0; i < headerParser.classes.size(); ++i) {
        const auto& cls = headerParser.classes[i];
        
        // Skip explicitly known abstract/private utility classes
        if (cls.name == "Graphics::Renderable3" || cls.name == "Renderable3" ||
            cls.name == "Xi::Log" || cls.name == "Log" ||
            cls.name == "Xi::IMemoryDevice" || cls.name == "IMemoryDevice") {
            continue;
        }
        
        // Skip any class containing pure virtual methods
        bool isAbstract = false;
        for (usz m = 0; m < cls.methods.size(); ++m) {
            if (cls.methods[m].isPureVirtual) {
                isAbstract = true;
                break;
            }
        }
        if (isAbstract) continue;
        
        filteredHeaderClasses.push(cls);
    }

    if (headerParser.classes.size() > 0 || headerParser.functions.size() > 0) {
    // Generate C++ bridge
        String bridgeCode = BindingGenerator::generateCppBridge(headerParser.classes, headerParser.functions, headerParser.namespaces, headerPathsForParse);

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
        bool hasJs = (targetName == "js" || targetName == "ts");
        if (!hasJs) {
            for (usz i = 0; i < _graph.nodes.size(); ++i) {
                if (_graph.nodes[i].language == "js" || _graph.nodes[i].language == "ts") {
                    hasJs = true;
                    break;
                }
            }
        }

        if (hasJs || isRepl) {
            // Generate TS glue and store it
            _generatedTsGlue = BindingGenerator::generateTsGlue(headerParser.classes, headerParser.functions, wasmName); // Changed parser to headerParser

            // Generate JS glue and store it
            _generatedJsGlue = BindingGenerator::generateJsGlue(headerParser.classes, headerParser.functions); // Changed parser to headerParser

            // Generate QuickJS bindings and store it
            _generatedQuickjsBindings = BindingGenerator::generateQuickjsBindings(headerParser.classes, headerParser.functions);

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

    if (_activeTarget) {
        _activeTarget = nullptr;
        _activeTargetName.clear();
    }
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
        if (_graph.nodes[i].contentHash.isEmpty()) {
            if (_graph.nodes[i].content.isEmpty() && !_graph.nodes[i].path.isEmpty()) {
                if (onRead) _graph.nodes[i].content = onRead(_graph.nodes[i].path);
            }
            _graph.nodes[i].contentHash = Cache::hashContent(_graph.nodes[i].content);
        }
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
    // printf("Sew DEBUG: globalKey = %s, onCacheHas = %d\n", globalKey.c_str(), onCacheHas ? (int)onCacheHas(globalKey) : -1);
    // fflush(stdout);

    if (onCacheHas && onCacheHas(globalKey)) {
        String cachedPath = onCacheGet(globalKey);
        if (cachedPath.length() > 0) {
            bool wasmOk = true;
            // printf("Sew DEBUG: cachedPath = '%s', wasmOk = %d\n", cachedPath.c_str(), wasmOk);
            // fflush(stdout);
            String cachedWasmPath;
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
                    cachedWasmPath = onCacheGet(globalKey + "_wasm");
                    if (cachedWasmPath.length() == 0) {
                        wasmOk = false;
                    }
                } else {
                    wasmOk = false;
                }
            }

            if (wasmOk) {
                if (onRead) {
                    String bin = onRead(cachedPath);
                    if (bin.length() > 0) {
                        FILE* fOut = fopen(outputPath.c_str(), "wb");
                        if (fOut) {
                            fwrite(bin.data(), 1, bin.size(), fOut);
                            fclose(fOut);
                            ::chmod(outputPath.c_str(), 0755);
                        }
                    }
                    if (targetName == "js" && cachedWasmPath.length() > 0) {
                        String wasmBin = onRead(cachedWasmPath);
                        if (wasmBin.length() > 0) {
                            FILE* fWasm = fopen(wasmOutput.c_str(), "wb");
                            if (fWasm) {
                                fwrite(wasmBin.data(), 1, wasmBin.size(), fWasm);
                                fclose(fWasm);
                            }
                        }
                    }
                }
                stopCompileWorkers();
                saveDepDb();
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
    
    // Global cache miss: wait for parallel compile workers to finish
    stopCompileWorkers();

    std::unique_lock<std::mutex> compileLock(_compileMutex);
    if (onProgress) {
        onProgress("Building", _compiledCount.load(), _discoveredCount.load());
    }
    _asyncCompileNodeIndices.clear();

    if (!_compileSuccess) {
        if (onError) onError(_compileErrors);
        return false;
    }

    Array<CompileResult> allResults;
    for (usz i = 0; i < _asyncCompileResults.size(); ++i) {
        allResults.push(Xi::Move(_asyncCompileResults[i]));
    }
    _asyncCompileResults.clear();

    usz totalNodes = _graph.nodes.size();
    usz compiled = 0;
    for (usz i = 0; i < _graph.nodes.size(); ++i) {
        if (_graph.nodes[i].compiled) {
            compiled++;
        }
    }
    compileLock.unlock();

    // Execute build plan step by step
    for (usz stepIdx = 0; stepIdx < _plan.steps.size(); ++stepIdx) {
        BuildStep& step = _plan.steps[stepIdx];

        if (onInfo) {
            String msg = "Step " + String((long long)stepIdx) + " nodes:";
            for (usz i = 0; i < step.nodeIndices.size(); ++i) {
                msg += "\n  - " + _graph.nodes[step.nodeIndices[i]].path;
            }
            onInfo(msg);
        }

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
                            if (node.content.isEmpty() && !nodePath.isEmpty()) {
                                if (onRead) node.content = onRead(nodePath);
                            }
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
                                Array<String> depHashes;
                                Array<bool> visited;
                                visited.allocate(_graph.nodes.size());
                                for (usz i = 0; i < node.dependencies.size(); ++i) {
                                    collectDepHashesRecursive(_graph, node.dependencies[i], depHashes, visited);
                                }
                                cacheKey = Cache::computeKey(
                                    nodeContent + (isPic ? ":fPIC" : ""), targetName, {}, depHashes);

                                if (onCacheHas && onCacheHas(cacheKey)) {
                                    cachedHit = true;
                                    compiled++;
                                    if (onProgress) {
                                        onProgress("Building", compiled, totalNodes);
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
                                    onProgress("Building", compiled, totalNodes);
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
        if (::access(outputPath.c_str(), 0) == 0) {
            onCacheSet(globalKey, outputPath);
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

            if (::access(wasmOutput.c_str(), 0) == 0) {
                onCacheSet(globalKey + "_wasm", wasmOutput);
            }
        }
    }

    saveDepDb();
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

void Engine::startCompileWorkers(Target* target, const String& targetName) {
    unsigned int numCores = std::thread::hardware_concurrency();
    if (numCores == 0) numCores = 4;
    
    std::lock_guard<std::mutex> lock(_compileMutex);
    _compileDone = false;
    _compileSuccess = true;
    _compileErrors.clear();
    _asyncCompileResults.clear();
    _asyncCompileNodeIndices.clear();
    _compileQueue.clear();
    _compileThreads.clear();
    
    for (unsigned int i = 0; i < numCores; ++i) {
        _compileThreads.push_back(std::thread([this, target, targetName]() {
            for (;;) {
                CompileTask task;
                {
                    std::unique_lock<std::mutex> lock(_compileMutex);
                    _compileCv.wait(lock, [this]() { return _compileDone || !_compileQueue.empty(); });
                    if (_compileQueue.empty() && _compileDone) {
                        break;
                    }
                    task = Xi::Move(_compileQueue.back());
                    _compileQueue.pop_back();
                }
                
                CompileRequest& req = task.req;
                Language* lang = nullptr;
                
                Language** langPtr = _langsByName.get(task.language);
                if (langPtr) {
                    lang = *langPtr;
                }
                
                if (lang) {
                    String cacheKey = Cache::computeKey(req.sourceContent + (req.outputPath.endsWith(".so") ? ":fPIC" : ""), targetName, {}, task.depHashes);
                    bool cacheHit = false;
                    CompileResult res;
                    if (onCacheHas && onCacheHas(cacheKey)) {
                        res.outputPath = onCacheGet(cacheKey);
                        res.success = true;
                        cacheHit = true;
                    } else {
                        res = lang->compile(req);
                    }
                    
                    std::lock_guard<std::mutex> lock(_compileMutex);
                    if (!res.success) {
                        _compileSuccess = false;
                        _compileErrors = "Failed to compile " + req.sourcePath + ": " + res.errors;
                    } else {
                        if (!cacheHit && onCacheSet && res.outputPath.length() > 0) {
                            onCacheSet(cacheKey, res.outputPath);
                        }
                        _asyncCompileResults.push(Xi::Move(res));
                        _asyncCompileNodeIndices.push(task.nodeIdx);
                        _compiledCount++;
                        if (onProgress) {
                            onProgress("Building", _compiledCount.load(), _discoveredCount.load());
                        }
                    }
                }
            }
        }));
    }
}

void Engine::stopCompileWorkers() {
    {
        std::lock_guard<std::mutex> lock(_compileMutex);
        _compileDone = true;
        _compileCv.notify_all();
    }
    for (auto& t : _compileThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    _compileThreads.clear();

    for (usz i = 0; i < _asyncCompileNodeIndices.size(); ++i) {
        _graph.nodes[_asyncCompileNodeIndices[i]].compiled = true;
    }
}

// static void collectDepHashesRecursive(const DepGraph& graph, usz nodeIdx, Array<String>& hashes, Map<usz, bool>& visited) {
//     if (visited.has(nodeIdx)) return;
//     visited.set(nodeIdx, true);
//     const auto& node = graph.nodes[nodeIdx];
//     for (usz i = 0; i < node.dependencies.size(); ++i) {
//         collectDepHashesRecursive(graph, node.dependencies[i], hashes, visited);
//     }
//     hashes.push(node.contentHash);
// }

static void collectDepHashesRecursive(const DepGraph& graph, usz nodeIdx, Array<String>& hashes, Array<bool>& visited) {
    if (nodeIdx >= graph.nodes.size() || visited[nodeIdx]) return;
    visited[nodeIdx] = true;
    
    const auto& node = graph.nodes[nodeIdx];
    for (usz i = 0; i < node.dependencies.size(); ++i) {
        collectDepHashesRecursive(graph, node.dependencies[i], hashes, visited);
    }

    hashes.push(node.contentHash);
}

void Engine::queueCompile(usz nodeIdx, Target* target, const String& targetName) {
    std::lock_guard<std::mutex> lock(_compileMutex);
    
    SourceNode& node = _graph.nodes[nodeIdx];
    if (node.compiled) return;
    
    node.form = target->formFor(node.language);

    String ext;
    long long lastDot = -1;
    for (usz k = 0; k < node.path.size(); ++k) {
        if (node.path.data()[k] == '.') lastDot = (long long)k;
    }
    if (lastDot >= 0) ext = node.path.substring((usz)lastDot);
    
    if (ext == ".h" || ext == ".hpp" || ext == ".hxx") {
        node.compiled = true;
        _compiledCount++;
        if (onProgress) {
            onProgress("Building", _compiledCount.load(), _discoveredCount.load());
        }
        return;
    }
    if (isRepl && (node.language == "cpp" || node.language == "c")) {
        bool isGenerated = node.path.endsWith("sew_bridge.cpp") || node.path.endsWith("sew_qjs_bindings.cpp");
        if (!isGenerated) {
            node.compiled = true;
            _compiledCount++;
            if (onProgress) {
                onProgress("Building", _compiledCount.load(), _discoveredCount.load());
            }
            return;
        }
    }
    
    if (node.content.isEmpty()) {
        if (onRead) node.content = onRead(node.path);
    }
    bool isPic = outputPath.endsWith(".so");
    
    CompileRequest req;
    req.sourcePath = node.path;
    req.sourceContent = node.content;
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
    Language** cppLangPtr = _langsByName.get("cpp");
    if (cppLangPtr) {
        Languages::CppLanguage* cppLang = dynamic_cast<Languages::CppLanguage*>(*cppLangPtr);
        if (cppLang) {
            for (usz k = 0; k < cppLang->preprocessor().includePaths.size(); ++k) {
                bool exists = false;
                for (usz j = 0; j < req.includePaths.size(); ++j) {
                    if (req.includePaths[j] == cppLang->preprocessor().includePaths[k]) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    req.includePaths.push(cppLang->preprocessor().includePaths[k]);
                }
            }
        }
    }
    
    if (node.path.indexOf("sew_qjs_bindings.cpp") != (usz)-1) {
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
    
    String tempDir = getTempDir();
    String safePath = node.path;
    safePath = safePath.replace("/", "_");
    safePath = safePath.replace("\\", "_");
    safePath = safePath.replace(".", "_");
    String outPath = tempDir + "/sew_obj_" + safePath + ".o";
    req.outputPath = outPath;
    
    Array<String> depHashes;
    Array<bool> visited;
    visited.allocate(_graph.nodes.size());
    for (usz i = 0; i < node.dependencies.size(); ++i) {
        collectDepHashesRecursive(_graph, node.dependencies[i], depHashes, visited);
    }

    CompileTask task;
    task.nodeIdx = nodeIdx;
    task.language = node.language;
    task.req = Xi::Move(req);
    task.depHashes = Xi::Move(depHashes);
    _compileQueue.push_back(Xi::Move(task));
    _compileCv.notify_one();
}

// void Engine::loadDepDb() {
//     _cachedFiles.clear();
//     const char* home = ::getenv("HOME");
//     if (!home) home = "/tmp";
//     String dbPath = String(home) + "/.cache/sew/dep_db.txt";
    
//     FILE* f = fopen(dbPath.c_str(), "r");
//     if (!f) return;
    
//     char line[4096];
//     CachedFileEntry current;
//     bool hasCurrent = false;
//     while (fgets(line, sizeof(line), f)) {
//         size_t len = strlen(line);
//         while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
//             line[len - 1] = '\0';
//             len--;
//         }
//         if (len == 0) continue;
        
//         if (line[0] == 'F' && line[1] == ' ') {
//             if (hasCurrent) {
//                 _cachedFiles.set(current.path, current);
//             }
//             current = CachedFileEntry();
//             char langBuf[64] = {0};
//             long long mtime = 0;
//             long long size = 0;
//             char hashBuf[128] = {0};
//             char pathBuf[2048] = {0};
//             if (sscanf(line, "F %63s %lld %lld %127s %[^\n]", langBuf, &mtime, &size, hashBuf, pathBuf) >= 5) {
//                 current.language = langBuf;
//                 current.mtime = mtime;
//                 current.size = size;
//                 current.contentHash = hashBuf;
//                 current.path = pathBuf;
//                 hasCurrent = true;
//             } else {
//                 hasCurrent = false;
//             }
//         } else if (line[0] == 'I' && line[1] == ' ' && hasCurrent) {
//             current.resolvedImports.push(line + 2);
//         }
//     }
//     if (hasCurrent) {
//         _cachedFiles.set(current.path, current);
//     }
//     fclose(f);
// }

// void Engine::saveDepDb() {
//     const char* home = ::getenv("HOME");
//     if (!home) home = "/tmp";
//     String dbDir = String(home) + "/.cache/sew";
//     ::mkdir(dbDir.c_str(), 0755);
//     String dbPath = dbDir + "/dep_db.txt";
    
//     FILE* f = fopen(dbPath.c_str(), "w");
//     if (!f) return;
    
//     for (auto& kv : _cachedFiles) {
//         CachedFileEntry& entry = kv.value;
//         fprintf(f, "F %s %lld %lld %s %s\n",
//                 entry.language.c_str(),
//                 entry.mtime,
//                 entry.size,
//                 entry.contentHash.c_str(),
//                 entry.path.c_str());
//         for (usz j = 0; j < entry.resolvedImports.size(); ++j) {
//             fprintf(f, "I %s\n", entry.resolvedImports[j].c_str());
//         }
//     }
//     fclose(f);
// }

} // namespace Sew
