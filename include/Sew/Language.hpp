/**
 * @file Language.hpp
 * @brief Base class for language plugins in the Sew build system.
 */

#pragma once

#include <Collection/String.hpp>
#include <Collection/Array.hpp>
#include <Collection/Map.hpp>

namespace Sew {

using namespace Xi;
using namespace Collection;

/**
 * @struct ImportSpec
 * @brief Describes a single import/include found in source code.
 */
struct ImportSpec {
    String specifier;   ///< Raw import string ("utils.hpp", "./bar.js")
    String fromFile;    ///< File containing this import
    usz line;           ///< Line number
    bool isSystem;      ///< <system> vs "local" (C++ only)
};

/**
 * @enum CompileForm
 * @brief The form a source file should be compiled into.
 */
enum class CompileForm {
    Native,     ///< Compile to native .o
    WASM,       ///< Compile to .wasm object
    Bytecode,   ///< Compile to embedded bytecode (QuickJS/MicroPython)
    Source,     ///< Keep as source (e.g. JS→JS target)
    Object,     ///< Already a compiled .o — just link
};

/**
 * @struct CompileRequest
 * @brief Everything a Language needs to compile one file.
 */
struct CompileRequest {
    String sourcePath;
    String sourceContent;
    CompileForm form;
    String targetTriple;    ///< e.g. "x86_64-unknown-linux-gnu"
    Array<String> flags;
    String outputPath;
    String assetsDir;
};

/**
 * @struct CompileResult
 * @brief Output of a single compilation.
 */
struct CompileResult {
    String outputPath;
    String outputContent;   ///< For in-memory outputs (bytecode)
    bool success = false;
    String errors;
    String warnings;
};

/**
 * @class Language
 * @brief Abstract base for all language implementations.
 */
class Language {
public:
    virtual String name() const = 0;
    virtual Array<String> extensions() const = 0;

    /// Extract imports from source code.
    virtual Array<ImportSpec> parseImports(
        const String& source, const String& filePath) = 0;

    /// Compile a single source file.
    virtual CompileResult compile(const CompileRequest& req) = 0;

    /// Generate bridge code for cross-language calls (optional).
    virtual String generateBridge(
        const String& targetLang, const Array<String>& exportedSymbols) {
        return "";
    }

    virtual ~Language() = default;
};

} // namespace Sew
