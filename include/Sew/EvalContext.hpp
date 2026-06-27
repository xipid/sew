/**
 * @file EvalContext.hpp
 * @brief Persistent evaluation context for REPL/eval mode.
 *
 * All language evaluation is performed via external processes
 * (clang++, qjs/node, micropython/python3) using Execution::Process.
 */

#pragma once

#include <Collection/String.hpp>

namespace Sew {

using namespace Collection;

class EvalContext {
public:
    /// Initialize the runtime for a given language ("js", "py", "cpp").
    void init(const String& language);

    /// Evaluate code and return output. Each call is a fresh invocation.
    String eval(const String& code);

    /// Check if a context has been initialized.
    bool hasContext() const;

    void destroy();
    ~EvalContext() { destroy(); }

private:
    String _language;
    bool _initialized = false;
    void* _cppProcess = nullptr;
};

} // namespace Sew
