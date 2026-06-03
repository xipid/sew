/**
 * @file PyLanguage.cpp
 * @brief Python language plugin implementation.
 */

#include <Languages/Python/PyLanguage.hpp>

namespace Sew { namespace Languages {

static bool isSpace(u8 c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static bool isAlpha(u8 c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isAlnum(u8 c) {
    return isAlpha(c) || (c >= '0' && c <= '9');
}

static String readWord(const u8*& p, const u8* end) {
    while (p < end && isSpace(*p)) p++;
    const u8* start = p;
    while (p < end && (isAlnum(*p) || *p == '.')) p++;
    return String(start, (usz)(p - start));
}

Array<ImportSpec> PyLanguage::parseImports(
    const String& source, const String& filePath)
{
    Array<ImportSpec> imports;
    Array<String> lines = source.split("\n");

    for (usz lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        String trimmed = lines[lineIdx].trim();
        if (trimmed.isEmpty()) continue;

        // Skip comments
        if (trimmed.data()[0] == '#') continue;

        const u8* p = trimmed.data();
        const u8* end = p + trimmed.size();

        // "import foo" or "import foo.bar"
        if (trimmed.startsWith("import ") && !trimmed.startsWith("import(")) {
            p += 7; // skip "import "
            String module = readWord(p, end);
            if (module.length() > 0) {
                // Convert module.path to module/path
                String spec = module.replace(".", "/");
                ImportSpec imp;
                imp.specifier = spec;
                imp.fromFile = filePath;
                imp.line = lineIdx + 1;
                imp.isSystem = false;
                imports.push(Xi::Move(imp));
            }
            continue;
        }

        // "from foo import bar" or "from .foo import bar"
        if (trimmed.startsWith("from ")) {
            p += 5; // skip "from "
            while (p < end && isSpace(*p)) p++;

            String module;
            bool isRelative = false;

            // Handle relative imports: from . import, from .foo import
            if (p < end && *p == '.') {
                isRelative = true;
                while (p < end && *p == '.') { module.push(*p); p++; }
                // Read rest of module name if any
                String rest = readWord(p, end);
                module += rest;
            } else {
                module = readWord(p, end);
            }

            if (module.length() > 0) {
                String spec;
                if (isRelative) {
                    spec = module.replace(".", "/");
                    if (spec.startsWith("/")) {
                        spec = "." + spec;
                    }
                } else {
                    spec = module.replace(".", "/");
                }

                ImportSpec imp;
                imp.specifier = spec;
                imp.fromFile = filePath;
                imp.line = lineIdx + 1;
                imp.isSystem = false;
                imports.push(Xi::Move(imp));
            }
            continue;
        }
    }

    return imports;
}

CompileResult PyLanguage::compile(const CompileRequest& req) {
    CompileResult result;

    switch (req.form) {
        case CompileForm::Source:
            // Python→Python target: keep as-is
            result.outputContent = req.sourceContent;
            result.outputPath = req.outputPath;
            result.success = true;
            break;

        case CompileForm::Bytecode:
            // TODO: MicroPython bytecode compilation
            result.outputContent = req.sourceContent;
            result.outputPath = req.outputPath;
            result.success = true;
            break;

        default:
            result.errors = "PyLanguage: unsupported compile form";
            result.success = false;
            break;
    }

    return result;
}

}} // namespace Sew::Languages
