/**
 * @file EvalContext.cpp
 * @brief Persistent eval context for REPL mode.
 */

#include <Sew/EvalContext.hpp>
#include <System/Process.hpp>
#include <Languages/JS/JS.hpp>
#include <dlfcn.h>

namespace Sew {

using namespace System;

void EvalContext::init(const String& language, const String& soPath, const String& jsGlue) {
    _language = language;
    _initialized = true;

    if (language == "cpp") {
        // C++ eval: start persistent clang++ process
        // (Compile to .so, dlopen pattern — future)
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
        // Write to temp file, compile as shared library, dlopen
        // For now: spawn clang++ and execute
        Process p;
        p.file = "clang++";
        p.arg.push("-x");
        p.arg.push("c++");
        p.arg.push("-std=c++17");
        p.arg.push("-o");
        p.arg.push("/tmp/sew_eval");
        p.arg.push("-");
        p.arg.push("-fuse-ld=mold");

        // Write source to stdin
        String wrapped = "#include <cstdio>\nint main() {\n";
        wrapped += code;
        wrapped += "\nreturn 0;\n}\n";
        p.stdin.push(wrapped);

        p.exec();
        // Close stdin to signal EOF
        p.stdin.destroy();
        p.wait();

        if (p.exitCode != 0) {
            String err = "Compilation failed:\n";
            while (p.stderr.size() > 0) err += p.stderr.shift();
            return err;
        }

        // Run the compiled binary
        Process run;
        run.file = "/tmp/sew_eval";
        run.wait();

        String output;
        while (run.stdout.size() > 0) output += run.stdout.shift();
        return output;
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
        Process* p = (Process*)_cppProcess;
        p->destroy();
        delete p;
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
