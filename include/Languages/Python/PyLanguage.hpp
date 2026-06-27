/**
 * @file PyLanguage.hpp
 * @brief Python language plugin for Sew.
 */

#pragma once

#include <Sew/Language.hpp>

namespace Sew { namespace Languages {

class PyLanguage : public Language {
public:
    String name() const override { return "py"; }

    Array<String> extensions() const override {
        Array<String> e;
        e.push(".py");
        return e;
    }

    /// Extract import/from...import statements from Python source.
    Array<ImportSpec> parseImports(
        const String& source, const String& filePath) override;

    /// Compile: for Bytecode form, compiles to .mpy bytecode.
    CompileResult compile(const CompileRequest& req) override;
};

}} // namespace Sew::Languages
