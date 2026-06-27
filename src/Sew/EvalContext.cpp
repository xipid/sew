/**
 * @file EvalContext.cpp
 * @brief Persistent eval context for REPL mode.
 */

#include <Sew/EvalContext.hpp>
#include <System/Process.hpp>

namespace Sew {

using namespace System;

void EvalContext::init(const String& language) {
    _language = language;
    _initialized = true;

    if (language == "cpp") {
        // C++ eval: start persistent clang++ process
        // (Compile to .so, dlopen pattern — future)
    }
    // JS/Python: QuickJS/MicroPython initialization deferred to when
    // those libraries are statically linked
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
        // QuickJS eval — to be connected when statically linked
        return "JS eval not yet connected to QuickJS";
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
    _initialized = false;
}

} // namespace Sew
