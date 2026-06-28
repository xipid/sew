/**
 * @file EvalContext.cpp
 * @brief Persistent eval context for REPL mode.
 */

#include <Sew/EvalContext.hpp>
#include <System/Process.hpp>
#include <Languages/JS/JS.hpp>
#include <dlfcn.h>
#include <cstdlib>
#include <string>

namespace Sew {

using namespace System;

struct CppReplContext {
    String headers;
    String types;
    String declarations;
    usz stepCount = 0;
};

void EvalContext::init(const String& language, const String& soPath, const String& jsGlue) {
    _language = language;
    _initialized = true;

    if (language == "cpp") {
        _cppProcess = new CppReplContext();
        if (soPath.length() > 0) {
            _soHandle = ::dlopen(soPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
        }
    }
    
    if (language == "js") {
        auto* js = new Languages::JS();
        _jsContext = js;

        if (soPath.length() > 0) {
            void* handle = ::dlopen(soPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
            _soHandle = handle;
            if (handle) {
                typedef void (*RegFn)(JSContext*, ::JSValue);
                RegFn reg = (RegFn)::dlsym(handle, "register_sew_native_bindings");
                if (reg) {
                    JSContext* ctx = js->getCtx();
                    ::JSValue global = JS_GetGlobalObject(ctx);
                    ::JSValue nativeObj = JS_NewObject(ctx);
                    
                    reg(ctx, nativeObj);
                    
                    JS_SetPropertyStr(ctx, global, "sew_native_bindings", nativeObj);
                    JS_SetPropertyStr(ctx, global, "__sew_native", JS_DupValue(ctx, nativeObj));
                    JS_FreeValue(ctx, global);
                }
            }
        }

        if (jsGlue.length() > 0) {
            // Strip export statements so standard evaluation succeeds
            String strippedGlue = jsGlue.replace("export ", "");
            js->eval(strippedGlue);
        }
    }
}

String EvalContext::eval(const String& code) {
    if (!_initialized) return "Error: eval context not initialized";

    if (_language == "cpp") {
        CppReplContext* ctx = (CppReplContext*)_cppProcess;
        if (!ctx) return "Error: C++ REPL context not allocated";

        String line = code.trim();
        if (line.isEmpty()) return "";

        if (line.startsWith("#include")) {
            ctx->headers += line + "\n";
            return "";
        }

        if (line.startsWith("struct ") || line.startsWith("class ") || line.startsWith("using ") || 
            line.startsWith("template ") || line.startsWith("inline ") ||
            (line.indexOf("(") >= 0 && line.indexOf(")") >= 0 && line.indexOf("{") >= 0)) {
            
            // Redefinition support: find and remove existing struct/class declaration of the same name
            String structName;
            bool isType = line.startsWith("struct ") || line.startsWith("class ");
            if (isType) {
                Array<String> parts = line.split(" ");
                if (parts.size() > 1) {
                    structName = parts[1];
                    usz idx = structName.indexOf("{");
                    if (idx != (usz)-1) {
                        structName = structName.substring(0, idx).trim();
                    }
                    idx = structName.indexOf(":");
                    if (idx != (usz)-1) {
                        structName = structName.substring(0, idx).trim();
                    }
                }
            }

            if (!structName.isEmpty()) {
                String searchStr1 = "struct " + structName;
                String searchStr2 = "class " + structName;
                long long foundIdx = ctx->types.indexOf(searchStr1);
                if (foundIdx == -1) {
                    foundIdx = ctx->types.indexOf(searchStr2);
                }
                if (foundIdx != -1) {
                    long long closeIdx = ctx->types.indexOf("};", (usz)foundIdx);
                    if (closeIdx != -1) {
                        ctx->types = ctx->types.substring(0, (usz)foundIdx) + 
                                     ctx->types.substring((usz)closeIdx + 2);
                    }
                }
            }

            if (isType) {
                ctx->types += code + "\n";
            } else {
                ctx->declarations += code + "\n";
            }
            return "";
        }

        ctx->stepCount++;
        String cppPath = "/tmp/sew_cpp_repl_step" + String(std::to_string(ctx->stepCount).c_str()) + ".cpp";
        String soPath = "/tmp/sew_cpp_repl_step" + String(std::to_string(ctx->stepCount).c_str()) + ".so";

        String src = "#include <cstdio>\n#include <iostream>\n";
        src += ctx->headers;
        src += "\n";
        src += ctx->types;
        src += "\n";
        src += ctx->declarations;
        src += "\n";
        src += "extern \"C\" void sew_eval_step() {\n";
        src += code;
        src += "\n}\n";

        FILE* f = fopen(cppPath.c_str(), "w");
        if (f) {
            fwrite(src.data(), 1, src.size(), f);
            fclose(f);
        }

        Process p;
        p.file = "clang++";
        p.arg.push("-shared");
        p.arg.push("-fPIC");
        p.arg.push("-std=c++17");
        p.arg.push("-o");
        p.arg.push(soPath);
        p.arg.push(cppPath);
        p.arg.push("-fuse-ld=mold");

        const char* envIncs = ::getenv("SEW_REPL_INCLUDES");
        if (envIncs) {
            String incsStr(envIncs);
            Array<String> parts = incsStr.split(":");
            for (usz k = 0; k < parts.size(); ++k) {
                if (!parts[k].isEmpty()) {
                    p.arg.push("-I" + parts[k]);
                }
            }
        }

        p.exec();
        p.wait();

        if (p.exitCode != 0) {
            String err = "Compilation failed:\n";
            while (p.stderr.size() > 0) err += p.stderr.shift();
            return err;
        }

        void* handle = ::dlopen(soPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            return "dlopen failed: " + String(::dlerror());
        }

        typedef void (*StepFn)();
        StepFn fn = (StepFn)::dlsym(handle, "sew_eval_step");
        if (!fn) {
            ::dlclose(handle);
            return "dlsym failed: " + String(::dlerror());
        }

        fn();
        return "";
    }

    if (_language == "js") {
        if (!_jsContext) return "Error: JS context not initialized";
        auto* js = (Languages::JS*)_jsContext;
        Languages::JsValue val = js->eval(code);
        if (val.isUndefined()) return "";
        return val.toString();
    }

    if (_language == "py") {
        // MicroPython eval — to be connected when statically linked
        return "Python eval not yet connected to MicroPython";
    }

    return "Unknown language: " + _language;
}

bool EvalContext::hasContext() const {
    return _initialized;
}

void EvalContext::destroy() {
    if (_cppProcess) {
        if (_language == "cpp") {
            delete (CppReplContext*)_cppProcess;
        } else {
            Process* p = (Process*)_cppProcess;
            p->destroy();
            delete p;
        }
        _cppProcess = nullptr;
    }
    if (_jsContext) {
        delete (Languages::JS*)_jsContext;
        _jsContext = nullptr;
    }
    if (_soHandle) {
        ::dlclose(_soHandle);
        _soHandle = nullptr;
    }
    _initialized = false;
}

} // namespace Sew
