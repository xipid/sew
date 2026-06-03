/**
 * @file JsLanguage.cpp
 * @brief JavaScript/TypeScript language plugin implementation.
 */

#include <Languages/JS/JsLanguage.hpp>

namespace Sew { namespace Languages {

static bool isSpace(u8 c) {
    return c == ' ' || c == '\t' || c == '\r';
}

void JsLanguage::skipWS(const u8*& p, const u8* end) const {
    while (p < end && isSpace(*p)) p++;
}

String JsLanguage::extractSpecifier(const String& line, usz& pos) const {
    const u8* data = line.data();
    usz len = line.size();

    // Find opening quote (' or ")
    while (pos < len && data[pos] != '\'' && data[pos] != '"' && data[pos] != '`') {
        pos++;
    }
    if (pos >= len) return "";

    u8 quote = data[pos];
    pos++; // skip opening quote

    String spec;
    while (pos < len && data[pos] != quote) {
        spec.push(data[pos]);
        pos++;
    }
    if (pos < len) pos++; // skip closing quote
    return spec;
}

Array<ImportSpec> JsLanguage::parseImports(
    const String& source, const String& filePath)
{
    Array<ImportSpec> imports;
    Array<String> lines = source.split("\n");

    for (usz lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        String trimmed = lines[lineIdx].trim();
        if (trimmed.isEmpty()) continue;

        // import ... from 'specifier'
        // import 'specifier'
        // export ... from 'specifier'
        if (trimmed.startsWith("import ") || trimmed.startsWith("export ")) {
            // Check for "from" keyword
            long long fromPos = trimmed.indexOf("from ");
            usz extractPos;
            if (fromPos >= 0) {
                extractPos = (usz)fromPos + 5;
            } else if (trimmed.startsWith("import ")) {
                // Direct import: import 'foo'
                extractPos = 7;
            } else {
                continue;
            }

            String spec = extractSpecifier(trimmed, extractPos);
            if (spec.length() > 0) {
                ImportSpec imp;
                imp.specifier = spec;
                imp.fromFile = filePath;
                imp.line = lineIdx + 1;
                imp.isSystem = false;
                imports.push(Xi::Move(imp));
            }
            continue;
        }

        // require('specifier')
        long long reqPos = trimmed.indexOf("require(");
        if (reqPos >= 0) {
            usz extractPos = (usz)reqPos + 8;
            String spec = extractSpecifier(trimmed, extractPos);
            if (spec.length() > 0) {
                ImportSpec imp;
                imp.specifier = spec;
                imp.fromFile = filePath;
                imp.line = lineIdx + 1;
                imp.isSystem = false;
                imports.push(Xi::Move(imp));
            }
        }
    }

    return imports;
}

CompileResult JsLanguage::compile(const CompileRequest& req) {
    CompileResult result;

    switch (req.form) {
        case CompileForm::Source:
            // JS→JS target: keep as-is
            result.outputContent = req.sourceContent;
            result.outputPath = req.outputPath;
            result.success = true;
            break;

        case CompileForm::Bytecode:
            // TODO: QuickJS bytecode compilation
            // JS_Eval with JS_EVAL_FLAG_COMPILE_ONLY → JS_WriteObject
            result.outputContent = req.sourceContent;
            result.outputPath = req.outputPath;
            result.success = true;
            break;

        default:
            result.errors = "JsLanguage: unsupported compile form";
            result.success = false;
            break;
    }

    return result;
}

}} // namespace Sew::Languages
