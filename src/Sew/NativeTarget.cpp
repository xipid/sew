/**
 * @file NativeTarget.cpp
 * @brief Native target linker implementation using mold.
 */

#include <Sew/NativeTarget.hpp>

namespace Sew {

LinkResult NativeTarget::link(const LinkRequest& req) {
    LinkResult result;

    // Collect all .o files
    Array<String> objects;
    for (usz i = 0; i < req.units.size(); ++i) {
        if (req.units[i].success && req.units[i].outputPath.length() > 0) {
            objects.push(req.units[i].outputPath);
        }
    }

    if (objects.size() == 0) {
        result.errors = "No object files to link";
        return result;
    }

    // Use clang++ with mold linker
    Process p;
    p.file = "clang++";

    p.arg.push("-fuse-ld=mold");

    // Target triple
    if (_triple.length() > 0) {
        String targetFlag = "--target=";
        targetFlag += _triple;
        p.arg.push(targetFlag);
    }

    // Output
    p.arg.push("-o");
    p.arg.push(req.outputPath);

    // Extra flags
    for (usz i = 0; i < req.flags.size(); ++i) {
        p.arg.push(req.flags[i]);
    }

    // Object files
    for (usz i = 0; i < objects.size(); ++i) {
        p.arg.push(objects[i]);
    }

    p.wait();

    result.success = (p.exitCode == 0);
    result.outputPath = req.outputPath;

    while (p.stderr.size() > 0) {
        result.errors += p.stderr.shift();
    }

    return result;
}

} // namespace Sew
