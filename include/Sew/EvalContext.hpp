/**
 * @file EvalContext.hpp
 * @brief Persistent evaluation context for REPL/eval mode.
 */

#pragma once

#include <Collection/String.hpp>

namespace Sew {

using namespace Collection;

class EvalContext {
public:
    /// Initialize the runtime for a given language ("js", "py", "cpp").
    void init(const String& language);

    /// Evaluate code and return output. Context persists between calls.
    String eval(const String& code);

    /// Check if a context has been initialized.
    bool hasContext() const;

    void destroy();
    ~EvalContext() { destroy(); }

private:
    String _language;
    void* _jsRuntime = nullptr;     ///< QuickJS JSRuntime*
    void* _jsContext = nullptr;     ///< QuickJS JSContext*
    void* _mpState = nullptr;       ///< MicroPython state
    void* _cppProcess = nullptr;    ///< Execution::Process* for C++ eval
    bool _initialized = false;
};

} // namespace Sew
