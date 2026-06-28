/**
 * @file NativeTarget.cpp
 * @brief Native target linker implementation using mold.
 */

#include <Sew/NativeTarget.hpp>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <sys/stat.h>

namespace Sew {

static String getExecutableDir() {
    char path[1024];
    ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        char* lastSlash = strrchr(path, '/');
        if (lastSlash) {
            *lastSlash = '\0';
            return String(path);
        }
    }
    return ".";
}

static String sanitizeIdentifier(const String& name) {
    String out;
    for (usz i = 0; i < name.length(); ++i) {
        char c = name.data()[i];
        if (std::isalnum(c)) {
            out.push(c);
        } else {
            out.push('_');
        }
    }
    return out;
}

static String toHexArray(const String& bytes) {
    String out;
    for (usz i = 0; i < bytes.length(); ++i) {
        if (i > 0) out += ", ";
        char hex[16];
        sprintf(hex, "0x%02X", (unsigned char)bytes.data()[i]);
        out += hex;
    }
    return out;
}

LinkResult NativeTarget::link(const LinkRequest& req) {
    LinkResult result;

    bool isShared = req.outputPath.endsWith(".so");
    const char* extraFlagsEnv = getenv("SEW_EXTRA_FLAGS");
    if (extraFlagsEnv && strstr(extraFlagsEnv, "-shared")) {
        isShared = true;
    }

    // Collect all .o files
    Array<String> objects;
    for (usz i = 0; i < req.units.size(); ++i) {
        if (req.units[i].success && req.units[i].outputPath.length() > 0 && req.units[i].outputContent.isEmpty()) {
            objects.push(req.units[i].outputPath);
        }
    }

    // Deduplicate object files to avoid duplicate symbol linker errors
    Array<String> uniqueObjects;
    for (usz i = 0; i < objects.size(); ++i) {
        bool exists = false;
        for (usz j = 0; j < uniqueObjects.size(); ++j) {
            if (uniqueObjects[j] == objects[i]) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            uniqueObjects.push(objects[i]);
        }
    }
    objects = Xi::Move(uniqueObjects);

    // Check if we have JS bytecode (only for executables, skip for shared libraries)
    bool hasJs = false;
    Array<usz> jsUnitIndices;
    if (!isShared) {
        for (usz i = 0; i < req.units.size(); ++i) {
            if (!req.units[i].outputContent.isEmpty()) {
                hasJs = true;
                jsUnitIndices.push(i);
            }
        }
    }

    if (objects.size() == 0 && !hasJs) {
        result.errors = "No object files or JS bytecode to link";
        return result;
    }

    // Generate bootloader if we have JS
    if (hasJs) {
        String sewBridgeIdent;
        for (usz idx : jsUnitIndices) {
            String name = req.units[idx].outputPath;
            if (name.indexOf("sew_bridge") >= 0) {
                sewBridgeIdent = sanitizeIdentifier(name);
                break;
            }
        }

        String boot;
        boot += "#include <quickjs.h>\n";
        boot += "#include <cstdio>\n";
        boot += "#include <cstdlib>\n";
        boot += "#include <cstring>\n";
        boot += "#include <cstdint>\n\n";

        // Declare QuickJS C bindings
        boot += "__attribute__((weak)) extern \"C\" void register_sew_native_bindings(JSContext *ctx, JSValue native_obj);\n\n";

        // Embed bytecodes for each JS unit
        for (usz idx : jsUnitIndices) {
            String ident = sanitizeIdentifier(req.units[idx].outputPath);
            boot += "static const uint8_t " + ident + "_bytecode[] = {\n  ";
            boot += toHexArray(req.units[idx].outputContent);
            boot += "\n};\n";
            boot += "static const size_t " + ident + "_bytecode_len = " + String((long long)req.units[idx].outputContent.length()) + ";\n\n";
        }

        // Console helper
        boot += "static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
        boot += "  for (int i = 0; i < argc; ++i) {\n";
        boot += "    const char *str = JS_ToCString(ctx, argv[i]);\n";
        boot += "    if (str) {\n";
        boot += "      printf(\"%s%s\", i > 0 ? \" \" : \"\", str);\n";
        boot += "      JS_FreeCString(ctx, str);\n";
        boot += "    }\n";
        boot += "  }\n";
        boot += "  printf(\"\\n\");\n";
        boot += "  return JS_UNDEFINED;\n";
        boot += "}\n\n";

        // Module normalize & loader
        boot += "static char *js_module_normalize(JSContext *ctx, const char *module_base_name,\n"
                "                                 const char *module_name, void *opaque) {\n"
                "  size_t len = strlen(module_name);\n"
                "  char *res = (char *)js_malloc(ctx, len + 1);\n"
                "  memcpy(res, module_name, len + 1);\n"
                "  return res;\n"
                "}\n\n";

        boot += "static JSModuleDef *js_module_loader(JSContext *ctx, const char *module_name, void *opaque) {\n";
        
        // Check for sew_bridge
        if (!sewBridgeIdent.isEmpty()) {
            boot += "  if (strstr(module_name, \"sew_bridge\") != nullptr || strstr(module_name, \"Xylem.hpp\") != nullptr) {\n";
            boot += "    JSValue val = JS_ReadObject(ctx, " + sewBridgeIdent + "_bytecode, " + sewBridgeIdent + "_bytecode_len, JS_READ_OBJ_BYTECODE);\n";
            boot += "    if (JS_IsException(val)) return nullptr;\n";
            boot += "    JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(val);\n";
            boot += "    JSValue res = JS_EvalFunction(ctx, val);\n";
            boot += "    if (JS_IsException(res)) {\n";
            boot += "      JSValue exc = JS_GetException(ctx);\n";
            boot += "      const char *err = JS_ToCString(ctx, exc);\n";
            boot += "      fprintf(stderr, \"[MODULE_LOADER ERROR] %s\\n\", err);\n";
            boot += "      JS_FreeCString(ctx, err);\n";
            boot += "      JS_FreeValue(ctx, exc);\n";
            boot += "    }\n";
            boot += "    JS_FreeValue(ctx, res);\n";
            boot += "    return m;\n";
            boot += "  }\n";
        }

        // Check for other modules
        for (usz idx : jsUnitIndices) {
            String baseName = req.units[idx].outputPath;
            long long slash = -1;
            for (usz k = 0; k < baseName.size(); ++k) if (baseName.data()[k] == '/') slash = k;
            String name = (slash >= 0) ? baseName.substring(slash + 1) : baseName;
            if (name.endsWith(".o")) name = name.substring(0, name.length() - 2);

            String ident = sanitizeIdentifier(req.units[idx].outputPath);
            if (name.indexOf("sew_bridge") < 0) {
                boot += "  if (strstr(module_name, \"" + name + "\") != nullptr) {\n";
                boot += "    JSValue val = JS_ReadObject(ctx, " + ident + "_bytecode, " + ident + "_bytecode_len, JS_READ_OBJ_BYTECODE);\n";
                boot += "    if (JS_IsException(val)) return nullptr;\n";
                boot += "    JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(val);\n";
                boot += "    JSValue res = JS_EvalFunction(ctx, val);\n";
                boot += "    JS_FreeValue(ctx, res);\n";
                boot += "    return m;\n";
                boot += "  }\n";
            }
        }
        boot += "  return nullptr;\n";
        boot += "}\n\n";

        // Main function
        boot += "extern \"C\" JSContext *sew_global_ctx;\n";
        boot += "JSContext *global_ctx = nullptr;\n\n";
        boot += "int main(int argc, char **argv) {\n";
        boot += "  JSRuntime *rt = JS_NewRuntime();\n";
        boot += "  JSContext *ctx = JS_NewContext(rt);\n";
        boot += "  sew_global_ctx = ctx;\n";
        boot += "  global_ctx = ctx;\n\n";

        boot += "  JS_SetModuleLoaderFunc(rt, js_module_normalize, js_module_loader, nullptr);\n\n";

        boot += "  JSValue global_obj = JS_GetGlobalObject(ctx);\n";
        boot += "  JSValue console = JS_NewObject(ctx);\n";
        boot += "  JS_SetPropertyStr(ctx, console, \"log\", JS_NewCFunction(ctx, js_console_log, \"log\", 1));\n";
        boot += "  JS_SetPropertyStr(ctx, global_obj, \"console\", console);\n";
        boot += "  if (register_sew_native_bindings) {\n";
        boot += "    JSValue native_obj = JS_NewObject(ctx);\n";
        boot += "    register_sew_native_bindings(ctx, native_obj);\n";
        boot += "    JS_SetPropertyStr(ctx, global_obj, \"__sew_native\", native_obj);\n";
        boot += "  }\n";
        boot += "  {\n";
        boot += "    JSValue args = JS_NewArray(ctx);\n";
        boot += "    for (int i = 0; i < argc; ++i) {\n";
        boot += "      JS_SetPropertyUint32(ctx, args, i, JS_NewString(ctx, argv[i]));\n";
        boot += "    }\n";
        boot += "    JS_SetPropertyStr(ctx, global_obj, \"scriptArgs\", JS_DupValue(ctx, args));\n";
        boot += "    JSValue process = JS_NewObject(ctx);\n";
        boot += "    JS_SetPropertyStr(ctx, process, \"argv\", args);\n";
        boot += "    JS_SetPropertyStr(ctx, global_obj, \"process\", process);\n";
        boot += "  }\n";
        boot += "  JS_FreeValue(ctx, global_obj);\n\n";



        // Run user JS modules
        for (usz idx : jsUnitIndices) {
            String baseName = req.units[idx].outputPath;
            long long slash = -1;
            for (usz k = 0; k < baseName.size(); ++k) if (baseName.data()[k] == '/') slash = k;
            String name = (slash >= 0) ? baseName.substring(slash + 1) : baseName;
            if (name.endsWith(".o")) name = name.substring(0, name.length() - 2);

            String ident = sanitizeIdentifier(req.units[idx].outputPath);
            if (name.indexOf("sew_bridge") < 0) {
                boot += "  {\n";
                boot += "    JSValue val = JS_ReadObject(ctx, " + ident + "_bytecode, " + ident + "_bytecode_len, JS_READ_OBJ_BYTECODE);\n";
                boot += "    if (JS_IsException(val)) {\n";
                boot += "      JSValue exc = JS_GetException(ctx);\n";
                boot += "      const char *err = JS_ToCString(ctx, exc);\n";
                boot += "      fprintf(stderr, \"[JS Error] %s\\n\", err);\n";
                boot += "      JS_FreeCString(ctx, err);\n";
                boot += "      JS_FreeValue(ctx, exc);\n";
                boot += "    } else {\n";
                boot += "      if (JS_ResolveModule(ctx, val) < 0) {\n";
                boot += "        JSValue exc = JS_GetException(ctx);\n";
                boot += "        const char *err = JS_ToCString(ctx, exc);\n";
                boot += "        fprintf(stderr, \"[JS Error] Resolve failed: %s\\n\", err);\n";
                boot += "        JS_FreeCString(ctx, err);\n";
                boot += "        JS_FreeValue(ctx, exc);\n";
                boot += "      } else {\n";
                boot += "        JSValue res = JS_EvalFunction(ctx, val);\n";
                boot += "        JSValue global_obj2 = JS_GetGlobalObject(ctx);\n";
                boot += "        JS_SetPropertyStr(ctx, global_obj2, \"__main_promise\", JS_DupValue(ctx, res));\n";
                boot += "        JS_FreeValue(ctx, global_obj2);\n";
                boot += "        JSValue catch_res = JS_Eval(ctx, \"if (globalThis.__main_promise && typeof globalThis.__main_promise.catch === 'function') { globalThis.__main_promise.catch(err => { console.log('RUNTIME ERROR:', err, err.stack); }); }\", 168, \"<catch>\", JS_EVAL_TYPE_GLOBAL);\n";
                boot += "        JS_FreeValue(ctx, catch_res);\n";
                boot += "        if (JS_IsException(res)) {\n";
                boot += "          JSValue exc = JS_GetException(ctx);\n";
                boot += "          const char *err = JS_ToCString(ctx, exc);\n";
                boot += "          fprintf(stderr, \"[JS Error] %s\\n\", err);\n";
                boot += "          JS_FreeCString(ctx, err);\n";
                boot += "          JS_FreeValue(ctx, exc);\n";
                boot += "        }\n";
                boot += "        JS_FreeValue(ctx, res);\n";
                boot += "      }\n";
                boot += "    }\n";
                boot += "  }\n\n";
            }
        }

        boot += "  {\n";
        boot += "    JSContext *ctx1;\n";
        boot += "    int pending_err;\n";
        boot += "    for (;;) {\n";
        boot += "      pending_err = JS_ExecutePendingJob(rt, &ctx1);\n";
        boot += "      if (pending_err <= 0) {\n";
        boot += "        if (pending_err < 0) {\n";
        boot += "          JSValue exc = JS_GetException(ctx1);\n";
        boot += "          if (!JS_IsNull(exc) && !JS_IsUndefined(exc)) {\n";
        boot += "            const char *err = JS_ToCString(ctx1, exc);\n";
        boot += "            fprintf(stderr, \"[Pending Job Error] %s\\n\", err);\n";
        boot += "            JS_FreeCString(ctx1, err);\n";
        boot += "          }\n";
        boot += "          JS_FreeValue(ctx1, exc);\n";
        boot += "        }\n";
        boot += "        break;\n";
        boot += "      }\n";
        boot += "    }\n";
        boot += "  }\n\n";

        boot += "  JS_FreeContext(ctx);\n";
        boot += "  JS_FreeRuntime(rt);\n";
        boot += "  return 0;\n";
        boot += "}\n";

        // Write bootloader to temporary file
        String bootPath = "/tmp/sew_bootloader.cpp";
        FILE* f_boot = fopen(bootPath.c_str(), "w");
        if (f_boot) {
            fwrite(boot.data(), 1, boot.size(), f_boot);
            fclose(f_boot);
        }

        // Compile bootloader
        Process p_boot;
        p_boot.file = "clang++";
        p_boot.arg.push("-c");
        p_boot.arg.push("-std=c++17");
        p_boot.arg.push("-I");
        
        String qjsPath = getExecutableDir() + "/_deps/quickjs_src-src";
        struct stat st;
        if (stat(qjsPath.c_str(), &st) != 0) {
            qjsPath = getExecutableDir() + "/../thirdparty/quickjs";
        }
        p_boot.arg.push(qjsPath);
        
        p_boot.arg.push("-o");
        p_boot.arg.push("/tmp/sew_bootloader.o");
        p_boot.arg.push(bootPath);
        p_boot.wait();

        if (p_boot.exitCode != 0) {
            result.errors = "Failed to compile sew_bootloader.cpp: ";
            while (p_boot.stderr.size() > 0) {
                result.errors += p_boot.stderr.shift();
            }
            return result;
        }

        objects.push("/tmp/sew_bootloader.o");
    }

    // Use clang++ with mold linker (unless compiling to wasm)
    Process p;
    bool isWasm = (_triple.indexOf("wasm32") >= 0);
    if (isWasm) {
        const char* home = ::getenv("HOME");
        String sdkDir = home ? String(home) + "/.cache/sew/wasi-sdk" : "";
        p.file = sdkDir.isEmpty() ? "clang++" : sdkDir + "/bin/clang++";
    } else {
        p.file = "clang++";
        p.arg.push("-fuse-ld=mold");
    }
    if (isShared) {
        p.arg.push("-shared");
        p.arg.push("-fPIC");
    }

    // Target triple
    if (_triple.length() > 0) {
        String targetFlag = "--target=";
        targetFlag += _triple;
        p.arg.push(targetFlag);
    }

    // Output
    p.arg.push("-o");
    p.arg.push(req.outputPath);

    // Extra flags
    for (usz i = 0; i < req.flags.size(); ++i) {
        p.arg.push(req.flags[i]);
    }

    const char* envFlags = getenv("SEW_EXTRA_FLAGS");
    if (envFlags) {
        String envFlagsStr(envFlags);
        String current;
        for (usz i = 0; i < envFlagsStr.length(); ++i) {
            if (envFlagsStr.data()[i] == ' ') {
                if (!current.isEmpty()) {
                    p.arg.push(current);
                    current.clear();
                }
            } else {
                current.push(envFlagsStr.data()[i]);
            }
        }
        if (!current.isEmpty()) {
            p.arg.push(current);
        }
    }

    // Object files
    for (usz i = 0; i < objects.size(); ++i) {
        p.arg.push(objects[i]);
    }

    // Link SewLib and quickjs statically to resolve C++ runtime/reflection symbols
    // unless we are compiling a shared library (.so)
    if (!isShared) {
        p.arg.push("-rdynamic");
        p.arg.push(getExecutableDir() + "/libquickjs.a");
        p.arg.push(getExecutableDir() + "/libSewLib.a");
        p.arg.push("-lm");
        p.arg.push("-ldl");
        p.arg.push("-lX11");
        p.arg.push("-lXrandr");
        p.arg.push("-lXinerama");
        p.arg.push("-lXcursor");
        p.arg.push("-lXi");
        p.arg.push("-lGL");
        p.arg.push("-lvulkan");
    } else {
        p.arg.push("-Wl,--unresolved-symbols=ignore-all");
    }


    p.wait();

    result.success = (p.exitCode == 0);
    result.outputPath = req.outputPath;

    while (p.stderr.size() > 0) {
        result.errors += p.stderr.shift();
    }

    return result;
}

} // namespace Sew
