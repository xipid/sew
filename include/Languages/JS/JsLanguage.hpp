/**
 * @file JsLanguage.hpp
 * @brief JavaScript/TypeScript language plugin for Sew.
 */

#pragma once

#include <Sew/Language.hpp>

namespace Sew { namespace Languages {

class JsLanguage : public Language {
public:
    String name() const override { return "js"; }

    Array<String> extensions() const override {
        Array<String> e;
        e.push(".js"); e.push(".ts"); e.push(".mjs"); e.push(".mts");
        return e;
    }

    /// Extract static imports from JS/TS source.
    Array<ImportSpec> parseImports(
        const String& source, const String& filePath) override;

    /// Compile: for Bytecode form, uses QuickJS to produce bytecode.
    CompileResult compile(const CompileRequest& req) override;

private:
    /// Extract import specifier from an import/require statement.
    String extractSpecifier(const String& line, usz& pos) const;

    /// Skip whitespace and comments.
    void skipWS(const u8*& p, const u8* end) const;
};

}} // namespace Sew::Languages
