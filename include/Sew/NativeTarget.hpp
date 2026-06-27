/**
 * @file NativeTarget.hpp
 * @brief Parameterized target for all native architectures.
 */

#pragma once

#include <Sew/Target.hpp>
#include <System/Process.hpp>

namespace Sew {

using namespace System;

/**
 * @class NativeTarget
 * @brief Covers amd, amd32, arm, arm32, risc, risc32, xtensa, xtensa32, bpf, wasm.
 */
class NativeTarget : public Target {
public:
    NativeTarget(const String& name, const Array<String>& aliases,
                 const String& triple)
        : _name(name), _aliases(aliases), _triple(triple) {}

    String name() const override { return _name; }
    Array<String> aliases() const override { return _aliases; }
    String triple() const override { return _triple; }

    CompileForm formFor(const String& langName) override {
        if (langName == "cpp") return CompileForm::Native;
        return CompileForm::Bytecode;  // JS/Python → bytecode embedded in C++
    }

    LinkResult link(const LinkRequest& req) override;

private:
    String _name, _triple;
    Array<String> _aliases;
};

} // namespace Sew
