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

int main(int argc, char** argv) {
    Command args(argc, argv);
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

    String stdinLang = args.option("--stdin")
        .description("Language for stdin input / eval mode")
        .string();

    bool showHelp = args.flag("--help -h");
    bool showVersion = args.flag("--version -v");
    bool quiet = args.flag("--quiet -q");
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

    if (target == "js") {
        String xylemRoot;
        for (usz i = 0; i < sources.size(); ++i) {
            xylemRoot = inferIncludeRoot(sources[i]);
            if (!xylemRoot.isEmpty()) {
                break;
            }
        }
        if (!xylemRoot.isEmpty()) {
            String xylemInclude = xylemRoot + "/include";
            if (!containsPath(includeDirs, xylemInclude)) {
                includeDirs.push(xylemInclude);
            }
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

    if (!quiet) fprintf(stderr, "Sew v1\n\n");

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

    const char* xicPath = getenv("SEW_XIC_INCLUDE");
    if (xicPath) {
        cppLang.preprocessor().includePaths.push(xicPath);
        sew.includePaths.push(xicPath);
    } else {
        const char* tryPaths[] = {
            "../xic/include",
            "/home/xi/Repo/xic/include",
            nullptr
        };
        for (int i = 0; tryPaths[i]; ++i) {
            struct stat st;
            if (stat(tryPaths[i], &st) == 0) {
                cppLang.preprocessor().includePaths.push(tryPaths[i]);
                sew.includePaths.push(tryPaths[i]);
                break;
            }
        }
    }

    const char* rhoPath = getenv("SEW_RHO_INCLUDE");
    if (rhoPath) {
        cppLang.preprocessor().includePaths.push(rhoPath);
        sew.includePaths.push(rhoPath);
    } else {
        const char* tryRhoPaths[] = {
            "../rho/include",
            "/home/xi/Repo/rho/include",
            nullptr
        };
        for (int i = 0; tryRhoPaths[i]; ++i) {
            struct stat st;
            if (stat(tryRhoPaths[i], &st) == 0) {
                cppLang.preprocessor().includePaths.push(tryRhoPaths[i]);
                sew.includePaths.push(tryRhoPaths[i]);
                break;
            }
        }
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
        sew.onCacheSet = [](String key, String content) {
            Cache::set(key, content);
        };
        sew.onCacheHas = [](String key) -> bool {
            return Cache::has(key);
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

    sew.onFinish = [&quiet](String outPath) {
        if (!quiet) Success("Output: " + outPath);
    };

    if (!quiet) {
        sew.onInfo = [](String msg) { Info(msg); };
        sew.onWarn = [](String msg) { Warn(msg); };
        sew.onError = [](String msg) { Error(msg); };
    }

    sew.assetsDir = assets;
    sew.outputPath = output;

    // ─── Run Mode (Eval) ────────────────────────────────────────────────

    if (stdinLang.length() > 0 && target.length() == 0) {
        if (!quiet) Info("Eval Mode: " + stdinLang);

        sew.eval(stdinLang);

        // Read from stdin
        String code;
        u8 buf[4096];
        for (;;) {
            ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; ++i)
                code.push(buf[i]);
        }

        if (code.length() > 0) {
            String result = sew.evalCode(code);
            if (result.length() > 0) {
                ::write(STDOUT_FILENO, result.data(), result.size());
            }
        }

        i64 elapsed = millis() - startTime;
        if (!quiet) {
            fprintf(stderr, "Eval took %lld ms\n", (long long)elapsed);
        }

        goto cleanup;
    }

    // ─── Target Mode (Build) ────────────────────────────────────────────

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

    if (!quiet) {
        Info("Target: " + target);
        Info("Output: " + output);

        String sourceList;
        for (usz i = 0; i < sources.size(); ++i) {
            if (i > 0) sourceList += ", ";
            sourceList += sources[i];
        }
        Info("Sources: " + sourceList);
        fprintf(stderr, "\n");
    }

    {
        // Add source files
        i64 findStart = millis();
        for (usz i = 0; i < sources.size(); ++i) {
            String content = readFile(sources[i]);
            if (content.isEmpty()) {
                Error("Cannot read: " + sources[i]);
                return 1;
            }
            sew.input(sources[i], content);
        }

        // Discover dependencies
        sew.find();
        i64 findElapsed = millis() - findStart;
        if (!quiet) {
            fprintf(stderr, "Discovery took %lld ms\n", (long long)findElapsed);
        }

        // Build
        Progress progress;
        usz progressTask = 0;
        if (!quiet) {
            progressTask = progress.addLinearTask((u64)sew.nodeCount(), "it", "Build");
            sew.onProgress = [&progress, progressTask](String msg, usz current, usz total) {
                if (progress.tasks.size() == 0) return;
                progress.message = msg;
                progress.updateLinearTask(progressTask, current, msg);
                progress.update();
                (void)total;
            };
        }
        i64 buildStart = millis();
        bool ok = sew.build(target);
        i64 buildElapsed = millis() - buildStart;
        if (!quiet) {
            progress.destroy();
            fprintf(stderr, "Build took %lld ms\n", (long long)buildElapsed);
        }
        if (!ok) return 1;
    }

    {
        i64 totalElapsed = millis() - startTime;
        if (!quiet) {
            fprintf(stderr, "Total took %lld ms\n", (long long)totalElapsed);
        }
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
