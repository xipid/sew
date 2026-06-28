/**
 * @file main.cpp
 * @brief Sew CLI — polyglot build system entry point.
 *
 * Features:
 *   - Colored terminal output with progress bars
 *   - Build cache at ~/.cache/sew/cache/
 *   - Auto-cleanup of cache entries older than 30 days on exit
 *   - All 12+ targets registered
 *   - Both Target mode and Run mode
 */

#include <Sew/Sew.hpp>
#include <Sew/Cache.hpp>
#include <Sew/NativeTarget.hpp>
#include <Sew/JsTarget.hpp>
#include <Sew/PyTarget.hpp>
#include <Languages/CPP/CppLanguage.hpp>
#include <Languages/JS/JsLanguage.hpp>
#include <Languages/Python/PyLanguage.hpp>
#include <Terminal/Command.hpp>
#include <Terminal/Format.hpp>
#include <Xi/Primitives.hpp>
#include <Xi/Time.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <glob.h>
#include <dirent.h>
#include <iostream>
#include <string>

using namespace Sew;
using namespace Sew::Languages;
using namespace Terminal;
using namespace Xi;
using namespace Collection;

// ─── File I/O ───────────────────────────────────────────────────────────

static String readFile(const String& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return "";

    struct stat st;
    if (::fstat(fd, &st) == 0 && st.st_size > 0) {
        String result;
        result.allocate((usz)st.st_size);
        usz total = 0;
        while (total < (usz)st.st_size) {
            ssize_t n = ::read(fd, (void*)(result.data() + total),
                               (usz)st.st_size - total);
            if (n <= 0) break;
            total += (usz)n;
        }
        ::close(fd);
        return result;
    }

    String result;
    u8 buf[8192];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i)
            result.push(buf[i]);
    }
    ::close(fd);
    return result;
}

static void writeFile(const String& path, const String& content) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    if (content.size() > 0)
        ::write(fd, content.data(), content.size());
    ::close(fd);
}

static bool fileExists(const String& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

static String canonicalizePath(const String& path) {
    char* rp = ::realpath(path.c_str(), nullptr);
    if (rp) {
        String res(rp);
        ::free(rp);
        return res;
    }
    return path;
}

static String findPrecompiledLibrary(const String& headerPath) {
    String path = canonicalizePath(headerPath);
    if (path.isEmpty()) path = headerPath;
    long long lastSlash = -1;
    for (usz i = 0; i < path.size(); ++i) {
        if (path.data()[i] == '/') lastSlash = (long long)i;
    }
    if (lastSlash < 0) return "";
    String dir = path.substring(0, (usz)lastSlash);
    
    for (int level = 0; level < 4; ++level) {
        String buildDir = dir + "/build";
        struct stat st;
        if (::stat(buildDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            long long dirSlash = -1;
            for (usz i = 0; i < dir.size(); ++i) {
                if (dir.data()[i] == '/') dirSlash = (long long)i;
            }
            String dirName = (dirSlash >= 0) ? dir.substring((usz)dirSlash + 1) : "";
            if (!dirName.isEmpty()) {
                String candidate1 = buildDir + "/lib" + dirName + ".a";
                if (::stat(candidate1.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                    return candidate1;
                }
                DIR* d = ::opendir(buildDir.c_str());
                if (d) {
                    struct dirent* entry;
                    while ((entry = ::readdir(d)) != nullptr) {
                        String name = entry->d_name;
                        if (name.endsWith(".a") && name.startsWith("lib") && name.indexOf("test") < 0) {
                            String result = buildDir + "/" + name;
                            ::closedir(d);
                            return result;
                        }
                    }
                    ::closedir(d);
                }
            }
        }
        
        long long parentSlash = -1;
        for (usz i = 0; i < dir.size(); ++i) {
            if (dir.data()[i] == '/') parentSlash = (long long)i;
        }
        if (parentSlash < 0) break;
        dir = dir.substring(0, (usz)parentSlash);
    }
    return "";
}

static String stripImports(const String& code) {
    Array<String> lines = code.split("\n");
    String result;
    for (usz i = 0; i < lines.size(); ++i) {
        String line = lines[i];
        String trimmed = line.trim();
        if (trimmed.startsWith("import ") || trimmed.startsWith("import{") || (trimmed.startsWith("import") && trimmed.indexOf("from") >= 0)) {
            continue;
        }
        result += line + "\n";
    }
    return result;
}

static bool containsPath(const Array<String>& paths, const String& path) {
    for (usz i = 0; i < paths.size(); ++i) {
        if (paths[i] == path) return true;
    }
    return false;
}

static String executableDir() {
    char path[1024];
    ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) return "";
    path[len] = '\0';
    char* slash = ::strrchr(path, '/');
    if (!slash) return "";
    *slash = '\0';
    return String(path);
}

static String parentDir(const String& path) {
    long long slash = -1;
    for (usz i = 0; i < path.size(); ++i) {
        if (path.data()[i] == '/') slash = (long long)i;
    }
    if (slash <= 0) return "";
    return path.substring(0, (usz)slash);
}

static String inferIncludeRoot(const String& path) {
    long long includePos = path.indexOf("/include/");
    if (includePos < 0) return "";
    return path.substring(0, (usz)includePos);
}

static void sweepIncludesInParent(const String& parentPath, Array<String>& includeDirs) {
    DIR* dir = ::opendir(parentPath.c_str());
    if (!dir) return;

    while (struct dirent* entry = ::readdir(dir)) {
        String name = entry->d_name;
        if (name == "." || name == "..") continue;

        String subPath = parentPath;
        if (!subPath.endsWith("/")) subPath += "/";
        subPath += name;

        struct stat st;
        if (::stat(subPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            String includePath = subPath + "/include";
            struct stat stInc;
            if (::stat(includePath.c_str(), &stInc) == 0 && S_ISDIR(stInc.st_mode)) {
                if (!containsPath(includeDirs, includePath)) {
                    includeDirs.push(includePath);
                }
            }
        }
    }
    ::closedir(dir);
}

static void sweepSiblingIncludes(Array<String>& includeDirs) {
    // 1. Sweep from current working directory parent
    char cwdBuf[1024];
    if (::getcwd(cwdBuf, sizeof(cwdBuf))) {
        String cwd(cwdBuf);
        String parent = parentDir(cwd);
        if (!parent.isEmpty()) {
            sweepIncludesInParent(parent, includeDirs);
        }
    }

    // 2. Sweep from executable parent's parent (e.g. /home/xi/Repo/sew/build -> /home/xi/Repo)
    String execDir = executableDir();
    if (!execDir.isEmpty()) {
        String sewRoot = parentDir(execDir);
        if (!sewRoot.isEmpty()) {
            String repoRoot = parentDir(sewRoot);
            if (!repoRoot.isEmpty()) {
                sweepIncludesInParent(repoRoot, includeDirs);
            }
        }
    }
}

static void listFilesRecursive(const String& dirPath, Array<String>& outFiles) {
    DIR* dir = ::opendir(dirPath.c_str());
    if (!dir) return;

    while (struct dirent* entry = ::readdir(dir)) {
        String name = entry->d_name;
        if (name == "." || name == "..") continue;

        String fullPath = dirPath;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += name;

        struct stat st;
        if (::stat(fullPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                listFilesRecursive(fullPath, outFiles);
            } else if (S_ISREG(st.st_mode)) {
                outFiles.push(fullPath);
            }
        }
    }
    ::closedir(dir);
}

static void expandGlob(const String& pattern, Array<String>& outFiles) {
    glob_t g;
    if (::glob(pattern.c_str(), 0, nullptr, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; ++i) {
            struct stat st;
            if (::stat(g.gl_pathv[i], &st) == 0 && S_ISREG(st.st_mode)) {
                outFiles.push(g.gl_pathv[i]);
            }
        }
        ::globfree(&g);
    }
}

static void seedXylemCoreSources(const String& xylemRoot, Array<String>& sources) {
    if (xylemRoot.isEmpty()) return;

    const char* names[] = {
        "Allocator", "BlobStore", "BlockDevice", "Cache", "Journal",
        "QueryParser", "TableStore", "XBDiff", "Xylem", "Watcher", nullptr
    };

    for (int i = 0; names[i]; ++i) {
        String src = xylemRoot + "/src/Xylem/";
        src += names[i];
        src += ".cpp";
        if (fileExists(src) && !containsPath(sources, src)) {
            sources.push(src);
        }
    }
}

static bool ensureWasiSdkInstalled() {
    const char* home = ::getenv("HOME");
    if (!home) return false;
    String sdkDir = String(home) + "/.cache/sew/wasi-sdk";
    String clangPath = sdkDir + "/bin/clang++";
    struct stat st;
    if (::stat(clangPath.c_str(), &st) == 0) {
        return true;
    }
    fprintf(stderr, "WASI SDK toolchain not found. Downloading WASI SDK to %s...\n", sdkDir.c_str());
    String mkdirCmd = "mkdir -p " + sdkDir;
    ::system(mkdirCmd.c_str());
    String downloadCmd = "curl -L https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-25/wasi-sdk-25.0-x86_64-linux.tar.gz | tar -xz -C " + sdkDir + " --strip-components=1";
    int ret = ::system(downloadCmd.c_str());
    if (ret != 0) {
        fprintf(stderr, "Failed to download/extract WASI SDK.\n");
        return false;
    }
    return ::stat(clangPath.c_str(), &st) == 0;
}

static bool matchSuffix(const String& file, const String& suffix) {
    if (suffix.isEmpty() || suffix == "/*" || suffix == "/") return true;
    String cleanSuffix = suffix;
    if (cleanSuffix.startsWith("/*")) {
        cleanSuffix = cleanSuffix.substring(2);
    } else if (cleanSuffix.startsWith("/")) {
        cleanSuffix = cleanSuffix.substring(1);
    }
    if (cleanSuffix.startsWith("*")) {
        cleanSuffix = cleanSuffix.substring(1);
    }
    return file.endsWith(cleanSuffix);
}

static Array<String> expandInputPatterns(const Array<String>& patterns) {
    Array<String> results;
    for (usz i = 0; i < patterns.size(); ++i) {
        String p = patterns[i];
        long long doubleStarPos = p.indexOf("**");
        if (doubleStarPos >= 0) {
            String baseDir = p.substring(0, (usz)doubleStarPos);
            String suffix = p.substring((usz)doubleStarPos + 2);
            if (baseDir.isEmpty()) baseDir = ".";
            if (baseDir.endsWith("/") && baseDir.length() > 1) {
                baseDir = baseDir.substring(0, baseDir.length() - 1);
            }
            Array<String> allFiles;
            listFilesRecursive(baseDir, allFiles);
            for (usz j = 0; j < allFiles.size(); ++j) {
                if (matchSuffix(allFiles[j], suffix)) {
                    results.push(allFiles[j]);
                }
            }
        } else if (p.indexOf('*') >= 0) {
            expandGlob(p, results);
        } else {
            results.push(p);
        }
    }
    return results;
}

// ─── Main ───────────────────────────────────────────────────────────────

namespace Sew { namespace Languages {
    extern Array<String> g_realArgs;
}}
using namespace Sew::Languages;

int main(int argc, char** argv) {
    // Populate g_realArgs from argv after '--' separator
    int separatorIndex = -1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) {
            separatorIndex = i;
            break;
        }
    }
    int parseArgc = argc;
    if (separatorIndex >= 0) {
        for (int i = separatorIndex + 1; i < argc; ++i) {
            g_realArgs.push(argv[i]);
        }
        parseArgc = separatorIndex;
    }

    Command args(parseArgc, argv);
    args.description("sew — polyglot build system").version("1.0.0");

    // --- Options ---
    String target = args.option("--target -t")
        .description("Build target (amd, arm, risc, wasm, js, py, ...)")
        .string();

    String output = args.option("--output -o")
        .description("Output file path")
        .string();

    String assets = args.option("--assets")
        .description("Assets output directory")
        .string();

    Array<String> includeDirs = args.option("--include -i")
        .description("Additional include directory; may be repeated")
        .value;

    const char* envXic = getenv("SEW_XIC_INCLUDE");
    if (envXic && !containsPath(includeDirs, envXic)) {
        includeDirs.push(envXic);
    }
    const char* envRho = getenv("SEW_RHO_INCLUDE");
    if (envRho && !containsPath(includeDirs, envRho)) {
        includeDirs.push(envRho);
    }

    sweepSiblingIncludes(includeDirs);

    String stdinLang = args.option("--stdin")
        .description("Language for stdin input / eval mode")
        .string();

    bool showHelp = args.flag("--help -h");
    bool showVersion = args.flag("--version -v");
    bool showProgress = args.flag("--progress -p").active;
    bool quiet = !showProgress;
    bool noCache = false;
    Command& cacheOpt = args.flag("--cache");
    if (cacheOpt.active && cacheOpt.value.size() > 0 && cacheOpt.value[0] == "false") {
        noCache = true;
    }
    // Also support querying --no-cache directly if the parser doesn't negate it in all versions
    if (args.flag("--no-cache").active) {
        noCache = true;
    }


    // --- Help / Version ---
    if (showHelp) {
        fprintf(stderr, "Sew v1\n\n");
        fprintf(stderr, "%s", args.help().c_str());
        fprintf(stderr, "\nTargets: amd, amd32, arm, arm32, risc, risc32, wasm, xtensa, xtensa32, bpf, js, py\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  sew main.cpp -t amd -o main\n");
        fprintf(stderr, "  sew app.cpp render.js -t js -o app\n");
        fprintf(stderr, "  sew --stdin js  (eval mode)\n");
        return 0;
    }

    if (showVersion) {
        fprintf(stdout, "sew 1.0.0\n");
        return 0;
    }

    // --- Collect source files ---
    Array<String> patterns = args.commands();
    Array<String> sources = expandInputPatterns(patterns);

    String scriptFile;
    if (target.length() == 0) {
        if (stdinLang.length() == 0 && sources.size() > 0) {
            for (usz i = 0; i < sources.size(); ++i) {
                if (sources[i].endsWith(".js") || sources[i].endsWith(".py")) {
                    scriptFile = sources[i];
                    stdinLang = sources[i].endsWith(".js") ? "js" : "py";
                    sources.splice(i, 1);
                    break;
                }
            }
        }
    }

    if (target == "js") {
        String xylemRoot;
        for (usz i = 0; i < includeDirs.size(); ++i) {
            if (includeDirs[i].endsWith("/xylem/include") || includeDirs[i].endsWith("../xylem/include") || includeDirs[i].endsWith("xylem/include")) {
                xylemRoot = includeDirs[i].substring(0, includeDirs[i].length() - 8);
                break;
            }
        }
        if (!xylemRoot.isEmpty()) {
            String watcherCpp = xylemRoot + "/src/Xylem/Watcher.cpp";
            if (fileExists(watcherCpp) && !containsPath(sources, watcherCpp)) {
                sources.push(watcherCpp);
            }
        }

        String sewRoot = parentDir(executableDir());
        if (!sewRoot.isEmpty()) {
            String reflectionCpp = sewRoot + "/src/Reflection/Reflection.cpp";
            if (fileExists(reflectionCpp) && !containsPath(sources, reflectionCpp)) {
                sources.push(reflectionCpp);
            }
        }

        seedXylemCoreSources(xylemRoot, sources);
    }

    if (sources.size() == 0 && stdinLang.length() == 0) {
        fprintf(stderr, "Sew v1\n\n");
        fprintf(stderr, "No input files. Use --help for usage.\n");
        return 1;
    }

    if (target == "js" || target == "wasm") {
        if (!ensureWasiSdkInstalled()) {
            return 1;
        }
    }



    i64 startTime = millis();

    // ─── Create Engine ──────────────────────────────────────────────────

    Engine sew;

    // Register languages
    CppLanguage cppLang;
    
    // Configure C++ preprocessor include paths
    cppLang.preprocessor().includePaths.push("include");
    for (usz i = 0; i < includeDirs.size(); ++i) {
        cppLang.preprocessor().includePaths.push(includeDirs[i]);
        sew.includePaths.push(includeDirs[i]);
    }


    const char* extraInclude = getenv("SEW_EXTRA_INCLUDE");
    if (extraInclude) {
        String extraStr(extraInclude);
        String current;
        for (usz i = 0; i < extraStr.length(); ++i) {
            if (extraStr.data()[i] == ':') {
                if (!current.isEmpty()) {
                    cppLang.preprocessor().includePaths.push(current);
                    current.clear();
                }
            } else {
                current.push(extraStr.data()[i]);
            }
        }
        if (!current.isEmpty()) {
            cppLang.preprocessor().includePaths.push(current);
        }
    }

    JsLanguage jsLang;
    PyLanguage pyLang;
    sew.registerLanguage(&cppLang);
    sew.registerLanguage(&jsLang);
    sew.registerLanguage(&pyLang);

    // Register all targets — helper to build alias arrays
    auto mkAliases = [](const char* a1 = nullptr, const char* a2 = nullptr) {
        Array<String> a;
        if (a1) a.push(a1);
        if (a2) a.push(a2);
        return a;
    };

    NativeTarget tAmd("amd", mkAliases("amd64", "x86_64"), "x86_64-unknown-linux-gnu");
    NativeTarget tAmd32("amd32", mkAliases("x86", "i386"), "i386-unknown-linux-gnu");
    NativeTarget tArm("arm", mkAliases("arm64", "aarch64"), "aarch64-unknown-linux-gnu");
    NativeTarget tArm32("arm32", mkAliases("armv7"), "armv7-unknown-linux-gnueabihf");
    NativeTarget tRisc("risc", mkAliases("riscv64"), "riscv64-unknown-linux-gnu");
    NativeTarget tRisc32("risc32", mkAliases("riscv32"), "riscv32-unknown-linux-gnu");
    NativeTarget tXtensa("xtensa", mkAliases(), "xtensa-esp32s3-elf");
    NativeTarget tXtensa32("xtensa32", mkAliases(), "xtensa-esp32-elf");
    NativeTarget tBpf("bpf", mkAliases("ebpf"), "bpf-unknown-none");
    NativeTarget tWasm("wasm", mkAliases("wasm32"), "wasm32-unknown-wasip1");
    JsTarget tJs;
    PyTarget tPy;

    sew.registerTarget(&tAmd);
    sew.registerTarget(&tAmd32);
    sew.registerTarget(&tArm);
    sew.registerTarget(&tArm32);
    sew.registerTarget(&tRisc);
    sew.registerTarget(&tRisc32);
    sew.registerTarget(&tXtensa);
    sew.registerTarget(&tXtensa32);
    sew.registerTarget(&tBpf);
    sew.registerTarget(&tWasm);
    sew.registerTarget(&tJs);
    sew.registerTarget(&tPy);

    // ─── Wire I/O ───────────────────────────────────────────────────────

    sew.onRead = [](String path) -> String {
        return readFile(path);
    };

    // Cache callbacks
    if (!noCache) {
        sew.onCacheGet = [](String key) -> String {
            return Cache::get(key);
        };
        sew.onCacheSet = [](String key, String local_path) {
            String cacheObjPath = Cache::cacheDir() + "/" + key + ".o";
            String bytes = readFile(local_path);
            if (bytes.size() > 0) {
                writeFile(cacheObjPath, bytes);
                Cache::set(key, cacheObjPath);
            }
        };
        sew.onCacheHas = [](String key) -> bool {
            if (!Cache::has(key)) return false;
            String path = Cache::get(key);
            if (path.isEmpty()) return false;
            return ::access(path.c_str(), 0) == 0;
        };
    }

    sew.onAsset = [&assets](String name, String content) {
        if (assets.length() > 0) {
            String path = assets;
            path += "/";
            path += name;
            writeFile(path, content);
        }
    };

    sew.onWarn = [](String msg) { Warn(msg); };
    sew.onError = [](String msg) { Error(msg); };

    sew.assetsDir = assets;
    sew.outputPath = output;

    // ─── Run Mode (Eval / REPL) ─────────────────────────────────────────

    if (stdinLang.length() > 0 && target.length() == 0) {
        quiet = true;
        bool runQuiet = true;

        sew.isRepl = true;
        ::setenv("SEW_NO_SIBLINGS", "1", 1);

        String incs;
        for (usz i = 0; i < sew.includePaths.size(); ++i) {
            if (i > 0) incs += ":";
            incs += sew.includePaths[i];
        }
        ::setenv("SEW_REPL_INCLUDES", incs.c_str(), 1);

        String exePath = argv[0];
        long long lastSlash = -1;
        for (usz i = 0; i < exePath.size(); ++i) {
            if (exePath.data()[i] == '/') lastSlash = (long long)i;
        }
        String exeDir = (lastSlash >= 0) ? exePath.substring(0, (usz)lastSlash) : ".";
        String qjsInclude = exeDir + "/_deps/quickjs_src-src";
        sew.includePaths.push(qjsInclude);

        // Read all inputs: piped stdin code (if not a TTY) and script file content
        String pipedCode;
        if (!::isatty(STDIN_FILENO)) {
            u8 buf[4096];
            for (;;) {
                ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
                if (n <= 0) break;
                for (ssize_t i = 0; i < n; ++i)
                    pipedCode.push(buf[i]);
            }
        }

        String scriptCode;
        if (!scriptFile.isEmpty()) {
            scriptCode = readFile(scriptFile);
            if (scriptCode.isEmpty()) {
                Error("Cannot read: " + scriptFile);
                return 1;
            }
        }

        // Input piped code or script code if they exist so dependency discovery works
        if (pipedCode.length() > 0) {
            String stdinFileName = "stdin." + stdinLang;
            sew.input(stdinFileName, pipedCode);
        } else if (scriptCode.length() > 0) {
            sew.input(scriptFile, scriptCode);
        }

        // Input explicit source files (C++ headers/sources remaining in sources)
        for (usz i = 0; i < sources.size(); ++i) {
            String content = readFile(sources[i]);
            if (content.isEmpty()) {
                Error("Cannot read: " + sources[i]);
                return 1;
            }
            sew.input(sources[i], content);
        }

        // Perform dependency discovery to construct the initial graph
        sew.find();

        // Scan the graph nodes for C++ headers and auto-discover libraries
        bool hasCppHeaders = false;
        Array<String> discoveredLibs;
        for (usz i = 0; i < sew.graph().nodes.size(); ++i) {
            String path = sew.graph().nodes[i].path;
            if (path.endsWith(".h") || path.endsWith(".hpp") || path.endsWith(".hxx")) {
                hasCppHeaders = true;
                String libPath = findPrecompiledLibrary(path);
                if (!libPath.isEmpty() && !containsPath(discoveredLibs, libPath)) {
                    discoveredLibs.push(libPath);
                }
            }
        }

        // Input discovered libraries and re-run find() to integrate them into the DAG
        if (discoveredLibs.size() > 0) {
            for (usz i = 0; i < discoveredLibs.size(); ++i) {
                sew.input(discoveredLibs[i], "");
            }
            sew.find();
        }

        // If there are C++ headers to bind, compile the bindings
        if (hasCppHeaders) {
            if (!runQuiet) Info("Compiling C++ sources for REPL environment...");
            if (sew.outputPath.isEmpty()) {
                sew.outputPath = "/tmp/sew_repl.so";
            }
            if (!sew.build("amd")) {
                Error("Failed to compile C++ library bindings for REPL.");
                return 1;
            }
        }

        if (!runQuiet) Info("Eval Mode: " + stdinLang);

        sew.eval(stdinLang);

        // Run the script file or piped code first
        if (stdinLang == "cpp") {
            String fullCode = scriptCode.length() > 0 ? scriptCode : pipedCode;
            Array<String> lines = fullCode.split("\n");
            for (usz idx = 0; idx < lines.size(); ++idx) {
                String line = lines[idx].trim();
                if (line.isEmpty()) continue;
                String res = sew.evalCode(line);
                if (res.length() > 0) {
                    printf("%s\n", res.c_str());
                }
            }
        } else {
            if (scriptCode.length() > 0) {
                sew.evalCode(stripImports(scriptCode));
            } else if (pipedCode.length() > 0) {
                sew.evalCode(stripImports(pipedCode));
            }
        }

        // Start the interactive prompt if input is a TTY and either --stdin was requested or no script was supplied
        bool startInteractive = ::isatty(STDIN_FILENO) && (args.flag("--stdin").active || scriptFile.isEmpty());

        if (startInteractive) {
            printf("\n\033[38;2;0;210;255m\033[1mSew REPL (%s)\033[0m\n", stdinLang.c_str());
            printf("Type expressions and press Enter. Press Ctrl+D to exit.\n\n");
            std::string line;
            for (;;) {
                printf("sew> ");
                fflush(stdout);
                if (!std::getline(std::cin, line)) {
                    printf("\n");
                    break;
                }
                if (line.empty()) continue;
                String result = sew.evalCode(stripImports(line.c_str()));
                if (result.length() > 0) {
                    printf("%s\n", result.c_str());
                }
            }
        }

        i64 elapsed = millis() - startTime;
        if (!quiet) {
            fprintf(stderr, "Eval session took %lld ms\n", (long long)elapsed);
        }

        goto cleanup;
    }

    // ─── Target Mode (Build) ────────────────────────────────────────────

    if (target == "ts") {
        target = "js";
    }
    if (target.length() == 0 && output.length() > 0) {
        if (output.endsWith(".ts") || output.endsWith(".js")) {
            target = "js";
        } else if (output.endsWith(".py")) {
            target = "py";
        }
    }

    if (target.length() == 0) {
        Error("No target specified. Use -t <target>");
        return 1;
    }

    if (output.length() == 0) {
        // Default output name: first source without extension
        if (sources.size() > 0) {
            output = sources[0];
            long long lastDot = -1;
            for (usz i = 0; i < output.size(); ++i) {
                if (output.data()[i] == '.') lastDot = (long long)i;
            }
            if (lastDot >= 0) output = output.substring(0, (usz)lastDot);
        } else {
            output = "a.out";
        }
        sew.outputPath = output;
    }

    {
        // Add source files
        for (usz i = 0; i < sources.size(); ++i) {
            String content = readFile(sources[i]);
            if (content.isEmpty()) {
                Error("Cannot read: " + sources[i]);
                return 1;
            }
            sew.input(sources[i], content);
        }

        // Build progress bar
        Progress progress;
        usz progressTask = 0;
        if (showProgress) {
            progressTask = progress.addLinearTask(1, "it", "Build");
            sew.onProgress = [&progress, progressTask](String msg, usz current, usz total) {
                if (progress.tasks.size() == 0) return;
                progress.message = msg;
                if (msg == "Discovering") {
                    progress.tasks[progressTask].totalRaw = (u64)current;
                    progress.tasks[progressTask].total = String((long long)current);
                    progress.updateLinearTask(progressTask, current, msg);
                } else {
                    progress.tasks[progressTask].totalRaw = (u64)total;
                    progress.tasks[progressTask].total = String((long long)total);
                    progress.updateLinearTask(progressTask, current, msg);
                }
                progress.update();
            };
        }

        // Discover dependencies
        sew.find();

        // Build C++ / JS files
        bool ok = sew.build(target);
        if (showProgress) {
            progress.destroy();
            ClearLine();
            fprintf(stderr, "\r");
        }
        if (!ok) return 1;
    }

cleanup:
    // ─── Cache Cleanup ──────────────────────────────────────────────────
    if (!noCache) {
        usz cleaned = Cache::cleanOld(30);
        if (cleaned > 0 && !quiet) {
            fprintf(stderr, "Cleaned %llu stale cache entries\n", (unsigned long long)cleaned);
        }
    }

    sew.destroy();
    return 0;
}
