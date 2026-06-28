/**
 * @file CppPreprocessor.hpp
 * @brief C++ preprocessor for Sew — handles #include, #define, #ifdef, etc.
 *
 * This preprocessor resolves imports and evaluates conditionals, then
 * STRIPS handled directives before passing to clang's own preprocessor.
 * This avoids double-processing while giving Sew full import knowledge.
 *
 * Handles:
 *   #include "path" / <path>
 *   #define NAME [VALUE]
 *   #define NAME(args) VALUE
 *   #undef NAME
 *   #ifdef NAME / #ifndef NAME
 *   #if expr / #elif expr
 *   #else / #endif
 *   #pragma once
 *   #error "message"
 *   #warning "message"
 */

#pragma once

#include <Collection/String.hpp>
#include <Collection/Map.hpp>
#include <Collection/Array.hpp>
#include <Xi/Func.hpp>
#include <mutex>

namespace Sew { namespace Languages {

using namespace Xi;
using namespace Collection;

/**
 * @struct MacroDef
 * @brief A preprocessor macro definition.
 */
struct MacroDef {
    String name;
    String value;
    Array<String> params;
    bool isFunctionLike = false;
};

/**
 * @struct PreprocessorResult
 * @brief Result of preprocessing a single file.
 */
struct PreprocessorResult {
    Array<String> localIncludes;        ///< Resolved "local" includes
    Array<String> systemIncludes;       ///< <system> includes (passed to clang)
    Array<String> siblingSourceFiles;   ///< Auto-discovered .cpp/.c siblings
    Map<String, MacroDef> defines;      ///< Active defines after processing
    String strippedSource;              ///< Source with handled directives removed
    Array<String> errors;               ///< #error messages
    Array<String> warnings;             ///< #warning messages
};

/**
 * @class CppPreprocessor
 * @brief Preprocessor that resolves includes and conditionals,
 *        then strips handled directives for clang.
 */
class CppPreprocessor {
public:
    /// Pre-seeded defines (e.g., __linux__, target-specific).
    Map<String, MacroDef> predefined;

    /// Tracks #pragma once files.
    Map<String, bool> pragmaOnceFiles;
    std::mutex pragmaMutex;

    /// Include search paths.
    Array<String> includePaths;



    /// Process a single file.
    PreprocessorResult process(const String& source, const String& filePath);

    /// Find sibling .cpp/.c/.o for a header.
    Array<String> findSiblingSourceFiles(const String& headerPath);

    /// Get include search paths.
    Array<String> getSearchPaths(const String& currentFile);

private:
    // --- Conditional stack ---
    struct CondFrame {
        bool active;        ///< Is this branch being processed?
        bool branchTaken;   ///< Has any branch in this #if..#endif been taken?
        bool wasElse;       ///< Have we seen #else?
    };

    /// Check if all conditional frames are active.
    bool allActive(const Array<CondFrame>& stack) const;

    // --- Directive parsing ---
    struct Directive {
        String type;    ///< "include", "define", "ifdef", etc.
        String args;    ///< Everything after the directive keyword.
    };

    Directive parseLine(const String& line) const;

    // --- Directive handlers ---
    void handleDefine(const String& args, Map<String, MacroDef>& defines);
    void handleUndef(const String& args, Map<String, MacroDef>& defines);
    bool handleIfdef(const String& args, const Map<String, MacroDef>& defines);
    bool handleIfndef(const String& args, const Map<String, MacroDef>& defines);
    bool handleIf(const String& expr, const Map<String, MacroDef>& defines);

    // --- Expression evaluator for #if/#elif ---
    long long evalExpr(const String& expr, const Map<String, MacroDef>& defines);
    long long evalExprOr(const u8*& p, const u8* end,
                         const Map<String, MacroDef>& defines);
    long long evalExprAnd(const u8*& p, const u8* end,
                          const Map<String, MacroDef>& defines);
    long long evalExprCmp(const u8*& p, const u8* end,
                          const Map<String, MacroDef>& defines);
    long long evalExprAtom(const u8*& p, const u8* end,
                           const Map<String, MacroDef>& defines);

    void skipWhitespace(const u8*& p, const u8* end) const;
    String readIdentifier(const u8*& p, const u8* end) const;

    // --- Path resolution ---
    /// Resolve include path: relative to current file, include/→src/ rewrite.
    String resolveIncludePath(const String& specifier, const String& currentFile);

    /// Extract directory from a file path.
    String dirOf(const String& path) const;

    /// Get the file extension.
    String extOf(const String& path) const;

    /// Replace first occurrence of 'from' with 'to' in path.
    String replaceInPath(const String& path, const String& from, const String& to) const;
};

}} // namespace Sew::Languages
