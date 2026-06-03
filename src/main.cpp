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
#include <Xi/Primitives.hpp>
#include <Xi/Time.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

using namespace Sew;
using namespace Sew::Languages;
using namespace Terminal;
using namespace Xi;
using namespace Collection;

// ─── ANSI Color Codes ───────────────────────────────────────────────────

namespace Color {
    static const char* Reset    = "\033[0m";
    static const char* Bold     = "\033[1m";
    static const char* Dim      = "\033[2m";
    static const char* Italic   = "\033[3m";

    // Brand colors
    static const char* Cyan     = "\033[38;2;0;210;255m";
    static const char* Green    = "\033[38;2;80;250;123m";
    static const char* Yellow   = "\033[38;2;241;196;15m";
    static const char* Red      = "\033[38;2;255;85;85m";
    static const char* Magenta  = "\033[38;2;189;147;249m";
    static const char* Orange   = "\033[38;2;255;165;0m";
    static const char* Blue     = "\033[38;2;98;114;164m";
    static const char* White    = "\033[38;2;248;248;242m";
    static const char* Gray     = "\033[38;2;108;108;128m";

    // Background
    static const char* BgBar    = "\033[48;2;40;42;54m";
    static const char* BgFill   = "\033[48;2;0;210;255m";
}

// ─── Terminal Utilities ─────────────────────────────────────────────────

static int termWidth() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return w.ws_col;
    return 80;
}

static void clearLine() {
    fprintf(stderr, "\r\033[K");
}

// ─── Progress Bar ───────────────────────────────────────────────────────

static void printProgress(const char* label, usz current, usz total) {
    if (total == 0) return;
    int width = termWidth();
    int barWidth = width - 40;
    if (barWidth < 10) barWidth = 10;
    if (barWidth > 60) barWidth = 60;

    float ratio = (float)current / (float)total;
    int filled = (int)(ratio * barWidth);

    clearLine();
    fprintf(stderr, "  %s%s%s %s", Color::Cyan, Color::Bold, label, Color::Reset);

    // Bar
    fprintf(stderr, " %s", Color::BgBar);
    for (int i = 0; i < barWidth; ++i) {
        if (i < filled)
            fprintf(stderr, "%s ", Color::BgFill);
        else
            fprintf(stderr, " ");
    }
    fprintf(stderr, "%s", Color::Reset);

    // Percentage and count
    int pct = (int)(ratio * 100.0f);
    fprintf(stderr, " %s%3d%%%s %s(%zu/%zu)%s",
            Color::White, pct, Color::Reset,
            Color::Gray, current, total, Color::Reset);
    fflush(stderr);
}

// ─── Logging ────────────────────────────────────────────────────────────

static void printBanner() {
    fprintf(stderr, "\n");
    fprintf(stderr, "  %s%s╔═══════════════════════════════════╗%s\n",
            Color::Cyan, Color::Bold, Color::Reset);
    fprintf(stderr, "  %s%s║          ✦  S E W  ✦             ║%s\n",
            Color::Cyan, Color::Bold, Color::Reset);
    fprintf(stderr, "  %s%s║     Polyglot Build System         ║%s\n",
            Color::Cyan, Color::Bold, Color::Reset);
    fprintf(stderr, "  %s%s╚═══════════════════════════════════╝%s\n",
            Color::Cyan, Color::Bold, Color::Reset);
    fprintf(stderr, "\n");
}

static void printInfo(const String& msg) {
    clearLine();
    fprintf(stderr, "  %s%s●%s %s%s%s\n",
            Color::Cyan, Color::Bold, Color::Reset,
            Color::White, msg.c_str(), Color::Reset);
}

static void printWarn(const String& msg) {
    clearLine();
    fprintf(stderr, "  %s%s⚠%s %s%s%s\n",
            Color::Yellow, Color::Bold, Color::Reset,
            Color::Yellow, msg.c_str(), Color::Reset);
}

static void printError(const String& msg) {
    clearLine();
    fprintf(stderr, "  %s%s✗%s %s%s%s\n",
            Color::Red, Color::Bold, Color::Reset,
            Color::Red, msg.c_str(), Color::Reset);
}

static void printSuccess(const String& msg) {
    clearLine();
    fprintf(stderr, "  %s%s✓%s %s%s%s\n",
            Color::Green, Color::Bold, Color::Reset,
            Color::Green, msg.c_str(), Color::Reset);
}

static void printStep(const String& label, const String& detail) {
    clearLine();
    fprintf(stderr, "  %s%s▸%s %s%s%s %s%s%s\n",
            Color::Magenta, Color::Bold, Color::Reset,
            Color::White, Color::Bold, label.c_str(),
            Color::Gray, detail.c_str(), Color::Reset);
}

static void printTiming(const char* label, i64 elapsedMs) {
    if (elapsedMs < 1000) {
        fprintf(stderr, "  %s%s⏱%s  %s%s%s took %s%s%lld ms%s\n",
                Color::Blue, Color::Bold, Color::Reset,
                Color::White, label, Color::Reset,
                Color::Green, Color::Bold, (long long)elapsedMs, Color::Reset);
    } else {
        double secs = (double)elapsedMs / 1000.0;
        fprintf(stderr, "  %s%s⏱%s  %s%s%s took %s%s%.2f s%s\n",
                Color::Blue, Color::Bold, Color::Reset,
                Color::White, label, Color::Reset,
                Color::Green, Color::Bold, secs, Color::Reset);
    }
}

// ─── File I/O ───────────────────────────────────────────────────────────

static String readFile(const String& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return "";

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

    String stdinLang = args.option("--stdin")
        .description("Language for stdin input / eval mode")
        .string();

    bool showHelp = args.flag("--help -h");
    bool showVersion = args.flag("--version -v");
    bool quiet = args.flag("--quiet -q");
    bool noCache = args.flag("--no-cache");

    // --- Help / Version ---
    if (showHelp) {
        printBanner();
        fprintf(stderr, "%s", args.help().c_str());
        fprintf(stderr, "\n  %s%sTargets:%s\n", Color::White, Color::Bold, Color::Reset);
        fprintf(stderr, "    %samd%s, %samd32%s, %sarm%s, %sarm32%s, "
                "%srisc%s, %srisc32%s, %swasm%s,\n",
                Color::Cyan, Color::Reset, Color::Cyan, Color::Reset,
                Color::Cyan, Color::Reset, Color::Cyan, Color::Reset,
                Color::Cyan, Color::Reset, Color::Cyan, Color::Reset,
                Color::Cyan, Color::Reset);
        fprintf(stderr, "    %sxtensa%s, %sxtensa32%s, %sbpf%s, "
                "%sjs%s, %spy%s\n\n",
                Color::Cyan, Color::Reset, Color::Cyan, Color::Reset,
                Color::Cyan, Color::Reset,
                Color::Cyan, Color::Reset, Color::Cyan, Color::Reset);

        fprintf(stderr, "  %s%sExamples:%s\n", Color::White, Color::Bold, Color::Reset);
        fprintf(stderr, "    %ssew main.cpp -t amd -o main%s\n", Color::Gray, Color::Reset);
        fprintf(stderr, "    %ssew app.cpp render.js -t js -o app%s\n", Color::Gray, Color::Reset);
        fprintf(stderr, "    %ssew --stdin js%s  %s(eval mode)%s\n\n",
                Color::Gray, Color::Reset, Color::Dim, Color::Reset);
        return 0;
    }

    if (showVersion) {
        fprintf(stdout, "sew 1.0.0\n");
        return 0;
    }

    // --- Collect source files ---
    Array<String> sources = args.commands();

    if (sources.size() == 0 && stdinLang.length() == 0) {
        printBanner();
        fprintf(stderr, "  %sNo input files. Use %s--help%s%s for usage.%s\n\n",
                Color::Yellow, Color::Bold, Color::Reset, Color::Yellow, Color::Reset);
        return 1;
    }

    if (!quiet) printBanner();

    i64 startTime = millis();

    // ─── Create Engine ──────────────────────────────────────────────────

    Engine sew;

    // Register languages
    CppLanguage cppLang;
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
    NativeTarget tWasm("wasm", mkAliases("wasm32"), "wasm32-unknown-wasi");
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
        if (!quiet) printSuccess("Output: " + outPath);
    };

    // Progress and logging callbacks
    if (!quiet) {
        sew.onProgress = [](String msg, usz current, usz total) {
            printProgress(msg.c_str(), current, total);
        };
        sew.onInfo = [](String msg) { printInfo(msg); };
        sew.onWarn = [](String msg) { printWarn(msg); };
        sew.onError = [](String msg) { printError(msg); };
    }

    sew.assetsDir = assets;
    sew.outputPath = output;

    // ─── Run Mode (Eval) ────────────────────────────────────────────────

    if (stdinLang.length() > 0 && target.length() == 0) {
        if (!quiet) printStep("Eval Mode", stdinLang);

        sew.eval(stdinLang);

        // Read from stdin and eval
        String code;
        u8 buf[4096];
        for (;;) {
            ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; ++i)
                code.push(buf[i]);
        }

        if (code.length() > 0) {
            // Add as input and eval...
            for (usz i = 0; i < sources.size(); ++i) {
                sew.input(sources[i], readFile(sources[i]));
            }
        }

        i64 elapsed = millis() - startTime;
        if (!quiet) {
            fprintf(stderr, "\n");
            printTiming("Eval", elapsed);
            fprintf(stderr, "\n");
        }

        goto cleanup;
    }

    // ─── Target Mode (Build) ────────────────────────────────────────────

    if (target.length() == 0) {
        printError("No target specified. Use -t <target>");
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
        printStep("Target", target);
        printStep("Output", output);

        String sourceList;
        for (usz i = 0; i < sources.size(); ++i) {
            if (i > 0) sourceList += ", ";
            sourceList += sources[i];
        }
        printStep("Sources", sourceList);
        fprintf(stderr, "\n");
    }

    {
        // Add source files
        i64 findStart = millis();
        for (usz i = 0; i < sources.size(); ++i) {
            String content = readFile(sources[i]);
            if (content.isEmpty()) {
                printError("Cannot read: " + sources[i]);
                return 1;
            }
            sew.input(sources[i], content);
        }

        // Discover dependencies
        sew.find();
        i64 findElapsed = millis() - findStart;
        if (!quiet) {
            clearLine();
            printTiming("Discovery", findElapsed);
        }

        // Build
        i64 buildStart = millis();
        sew.build(target);
        i64 buildElapsed = millis() - buildStart;
        if (!quiet) {
            clearLine();
            printTiming("Build", buildElapsed);
        }
    }

    {
        i64 totalElapsed = millis() - startTime;
        if (!quiet) {
            fprintf(stderr, "\n");
            printTiming("Total", totalElapsed);
            fprintf(stderr, "\n");
        }
    }

cleanup:
    // ─── Cache Cleanup ──────────────────────────────────────────────────
    if (!noCache) {
        usz cleaned = Cache::cleanOld(30);
        if (cleaned > 0 && !quiet) {
            String cleanMsg = "Cleaned ";
            cleanMsg += String((long long)cleaned);
            cleanMsg += " stale cache entries";
            fprintf(stderr, "  %s%s%s%s\n\n",
                    Color::Gray, Color::Dim, cleanMsg.c_str(), Color::Reset);
        }
    }

    sew.destroy();
    return 0;
}
