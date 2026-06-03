/**
 * @file CppLanguage.hpp
 * @brief C++ language plugin for Sew.
 */

#pragma once

#include <Sew/Language.hpp>
#include <Languages/CPP/CppPreprocessor.hpp>

namespace Sew { namespace Languages {

class CppLanguage : public Language {
public:
    String name() const override { return "cpp"; }

    Array<String> extensions() const override {
        Array<String> e;
        e.push(".cpp"); e.push(".c"); e.push(".cc"); e.push(".cxx");
        e.push(".h"); e.push(".hpp"); e.push(".hxx");
        return e;
    }

    Array<ImportSpec> parseImports(
        const String& source, const String& filePath) override;

    CompileResult compile(const CompileRequest& req) override;

    /// Access the preprocessor for configuration.
    CppPreprocessor& preprocessor() { return _preprocessor; }

private:
    CppPreprocessor _preprocessor;

    /// Invoke clang++ via Execution::Process.
    CompileResult invokeClang(const CompileRequest& req,
                              const String& strippedSource);
};

}} // namespace Sew::Languages
