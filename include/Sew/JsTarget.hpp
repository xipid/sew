/**
 * @file JsTarget.hpp
 * @brief JavaScript output target — compiles C++→WASM, generates TS glue.
 */

#pragma once

#include <Sew/Target.hpp>

namespace Sew {

class JsTarget : public Target {
public:
    String name() const override { return "js"; }
    Array<String> aliases() const override { return Array<String>(); }
    String triple() const override { return "wasm32-unknown-wasi"; }

    CompileForm formFor(const String& langName) override {
        if (langName == "js") return CompileForm::Source;
        if (langName == "py") return CompileForm::Bytecode;
        return CompileForm::WASM;  // C++ → WASM
    }

    LinkResult link(const LinkRequest& req) override;

private:
    /// Parse WASM exports and generate TypeScript declarations.
    String generateTsGlue(const String& wasmContent);

    /// Infer TS types from C++ function signatures.
    String cppTypeToTs(const String& cppType);
};

} // namespace Sew
