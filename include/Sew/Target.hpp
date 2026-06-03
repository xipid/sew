/**
 * @file Target.hpp
 * @brief Base class for compilation targets in the Sew build system.
 */

#pragma once

#include <Sew/Language.hpp>

namespace Sew {

/**
 * @struct LinkRequest
 * @brief Everything a Target needs to link compiled units.
 */
struct LinkRequest {
    Array<CompileResult> units;
    String outputPath;
    Array<String> flags;
    String assetsDir;
};

/**
 * @struct LinkResult
 * @brief Output of the link step.
 */
struct LinkResult {
    String outputPath;
    bool success = false;
    String errors;
};

/**
 * @class Target
 * @brief Abstract base for all compilation targets.
 */
class Target {
public:
    virtual String name() const = 0;
    virtual Array<String> aliases() const = 0;
    virtual String triple() const = 0;

    /// What compilation form should this language use for this target?
    virtual CompileForm formFor(const String& langName) = 0;

    /// Final link step.
    virtual LinkResult link(const LinkRequest& req) = 0;

    virtual ~Target() = default;
};

} // namespace Sew
