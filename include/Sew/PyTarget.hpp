/**
 * @file PyTarget.hpp
 * @brief Python output target.
 */

#pragma once

#include <Sew/Target.hpp>

namespace Sew {

class PyTarget : public Target {
public:
    String name() const override { return "py"; }
    Array<String> aliases() const override { return Array<String>(); }
    String triple() const override { return ""; }

    CompileForm formFor(const String& langName) override {
        if (langName == "py") return CompileForm::Source;
        return CompileForm::Bytecode;
    }

    LinkResult link(const LinkRequest& req) override;
};

} // namespace Sew
