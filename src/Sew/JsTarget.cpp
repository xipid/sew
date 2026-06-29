/**
 * @file JsTarget.cpp
 * @brief JavaScript target implementation: C++→WASM + TS glue generation.
 */

#include <Sew/JsTarget.hpp>
#include <System/Process.hpp>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>

namespace Sew {

using namespace System;

String JsTarget::cppTypeToTs(const String& cppType) {
    if (cppType == "int" || cppType == "long" || cppType == "short" ||
        cppType == "float" || cppType == "double" ||
        cppType == "i32" || cppType == "i64" || cppType == "u32" || cppType == "u64" ||
        cppType == "f32" || cppType == "f64" || cppType == "usz" ||
        cppType == "unsigned" || cppType == "size_t")
        return "number";
    if (cppType == "bool") return "boolean";
    if (cppType == "void") return "void";
    if (cppType == "char*" || cppType == "const char*" ||
        cppType == "String" || cppType == "string")
        return "string";
    return "any";
}

static String getWasiSdkDir() {
    const char* home = ::getenv("HOME");
    if (!home) return String();
    return String(home) + "/.cache/wasi-sdk";
}

static bool ensureWasiSdk() {
    String sdkDir = getWasiSdkDir();
    if (sdkDir.isEmpty()) return false;

    String clangPath = sdkDir + "/bin/clang++";
    struct stat st;
    if (::stat(clangPath.c_str(), &st) == 0) {
        return true;
    }

    fprintf(stderr, "WASI SDK toolchain not found at %s. Please run sew CLI to install it.\n", sdkDir.c_str());
    return false;
}

static String getWasmClangPath() {
    if (ensureWasiSdk()) {
        String sdkDir = getWasiSdkDir();
        return sdkDir + "/bin/clang++";
    }
    return "clang++";
}

static String getWasiSysrootPath() {
    String sdkDir = getWasiSdkDir();
    if (sdkDir.isEmpty()) return "";
    return sdkDir + "/share/wasi-sysroot";
}

static void ensureParentDir(const String& path) {
    long long lastSlash = -1;
    for (usz i = 0; i < path.size(); ++i) {
        if (path.data()[i] == '/') lastSlash = (long long)i;
    }
    if (lastSlash < 0) return;
    String dir = path.substring(0, (usz)lastSlash);
    if (dir.isEmpty()) return;
    String cmd = "mkdir -p " + dir;
    ::system(cmd.c_str());
}

LinkResult JsTarget::link(const LinkRequest& req) {
    LinkResult result;

    // Collect WASM objects and JS sources
    Array<String> wasmObjects;
    Array<String> jsSources;

    for (usz i = 0; i < req.units.size(); ++i) {
        if (!req.units[i].success) continue;
        if (req.units[i].outputPath.endsWith(".o") ||
            req.units[i].outputPath.endsWith(".wasm")) {
            wasmObjects.push(req.units[i].outputPath);
        } else {
            jsSources.push(req.units[i].outputPath);
        }
    }

    if (::getenv("SEW_DEBUG_LINK")) {
        fprintf(stderr, "JS link objects (%llu):\n", (unsigned long long)wasmObjects.size());
        for (usz i = 0; i < wasmObjects.size(); ++i) {
            fprintf(stderr, "  %s\n", wasmObjects[i].c_str());
        }
    }

    // Link WASM objects with wasm-ld
    if (wasmObjects.size() > 0) {

        String wasmOutput = req.outputPath;
        // Replace extension with .wasm
        long long lastSlash = -1;
        for (usz i = 0; i < wasmOutput.size(); ++i) {
            if (wasmOutput.data()[i] == '/' || wasmOutput.data()[i] == '\\') lastSlash = (long long)i;
        }
        long long dotPos = -1;
        for (usz i = (lastSlash >= 0 ? (usz)lastSlash : 0); i < wasmOutput.size(); ++i) {
            if (wasmOutput.data()[i] == '.') dotPos = (long long)i;
        }
        if (dotPos >= 0) {
            wasmOutput = wasmOutput.substring(0, (usz)dotPos);
        }
        wasmOutput += ".wasm";
        ensureParentDir(wasmOutput);

        Process ld;
        String clangPath = getWasmClangPath();
        ld.file = clangPath;
        ld.arg.push("--target=wasm32-unknown-wasi");
        ld.arg.push("-fno-exceptions");

        String wasiSysroot = getWasiSysrootPath();
        if (!wasiSysroot.isEmpty()) {
            ld.arg.push("--sysroot=" + wasiSysroot);
        } else {
            struct stat st;
            if (::stat("/usr/wasm32-wasi", &st) == 0 && S_ISDIR(st.st_mode)) {
                ld.arg.push("--sysroot=/usr/wasm32-wasi");
            }
        }

        ld.arg.push("-o");
        ld.arg.push(wasmOutput);
        ld.arg.push("-Wl,--no-entry");
        ld.arg.push("-Wl,--export-dynamic");
        ld.arg.push("-Wl,--allow-undefined");
        ld.arg.push("-Wl,--export-table");
        ld.arg.push("-Wl,-z,stack-size=2097152");
        ld.arg.push("-Wl,--stack-first");

        for (usz i = 0; i < wasmObjects.size(); ++i) {
            ld.arg.push(wasmObjects[i]);
        }

        ld.wait();

        if (ld.exitCode != 0) {
            result.errors = "link failed: ";
            while (ld.stderr.size() > 0)
                result.errors += ld.stderr.shift();
            return result;
        }

        // Generate TS glue
        String tsGlue = req.tsGlue;
        String tsPath = req.outputPath;
        if (dotPos >= 0) {
            tsPath = req.outputPath.substring(0, (usz)dotPos);
        }
        tsPath += ".ts";
        ensureParentDir(tsPath);

        // Write glue file
        FILE* f = fopen(tsPath.c_str(), "w");
        if (f) {
            if (tsGlue.size() > 0) {
                fwrite(tsGlue.data(), 1, tsGlue.size(), f);
            }
            fclose(f);
        }

        result.outputPath = tsPath;
    }

    result.success = true;
    result.outputPath = req.outputPath;
    return result;
}

} // namespace Sew
