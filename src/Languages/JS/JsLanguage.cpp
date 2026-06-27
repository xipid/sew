/**
 * @file JsLanguage.cpp
 * @brief JavaScript/TypeScript language plugin implementation.
 */

#include <string>
#include <cstdio>
#include <cstring>
#include <Languages/JS/JsLanguage.hpp>
#include <quickjs.h>

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


static char *js_compile_module_normalize(JSContext *ctx, const char *module_base_name,
                                         const char *module_name, void *opaque) {
    if (module_name[0] == '.') {
        const char *last_slash = strrchr(module_base_name, '/');
        if (last_slash) {
            size_t dir_len = last_slash - module_base_name;
            size_t name_len = strlen(module_name);
            char *res = (char *)js_malloc(ctx, dir_len + 1 + name_len + 1);
            memcpy(res, module_base_name, dir_len);
            res[dir_len] = '/';
            memcpy(res + dir_len + 1, module_name, name_len + 1);
            return res;
        }
    }
    size_t len = strlen(module_name);
    char *res = (char *)js_malloc(ctx, len + 1);
    memcpy(res, module_name, len + 1);
    return res;
}

static JSModuleDef *js_compile_module_loader(JSContext *ctx, const char *module_name, void *opaque) {
    std::string path = module_name;
    bool is_header = (path.find(".h") != std::string::npos || path.find(".hpp") != std::string::npos || path.find("sew_bridge") != std::string::npos);
    
    std::string content;
    if (is_header) {
        FILE *f = fopen("sew_bridge.js", "r");
        if (f) {
            char buf[4096];
            while (size_t n = fread(buf, 1, sizeof(buf), f)) {
                content.append(buf, n);
            }
            fclose(f);
        } else {
            content = "export class XylemEngine {}";
        }
    } else {
        FILE *f = fopen(path.c_str(), "r");
        if (f) {
            char buf[4096];
            while (size_t n = fread(buf, 1, sizeof(buf), f)) {
                content.append(buf, n);
            }
            fclose(f);
        }
    }

    JSValue val = JS_Eval(ctx, content.c_str(), content.size(), module_name,
                          JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_TYPE_MODULE);
    if (JS_IsException(val)) {
        return nullptr;
    }
    JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(val);
    JS_FreeValue(ctx, val);
    return m;
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

        case CompileForm::Bytecode: {
            JSRuntime *rt = JS_NewRuntime();
            JS_SetModuleLoaderFunc(rt, js_compile_module_normalize, js_compile_module_loader, nullptr);
            JSContext *ctx = JS_NewContext(rt);
            
            JSValue obj = JS_Eval(ctx, req.sourceContent.c_str(), req.sourceContent.size(),
                                  req.sourcePath.c_str(), JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_TYPE_MODULE);
            if (JS_IsException(obj)) {
                JSValue exc = JS_GetException(ctx);
                JSValue line = JS_GetPropertyStr(ctx, exc, "lineNumber");
                const char *err = JS_ToCString(ctx, exc);
                result.errors = err;
                if (!JS_IsUndefined(line)) {
                    int lineNum = 0;
                    JS_ToInt32(ctx, &lineNum, line);
                    result.errors += " at line " + String((long long)lineNum);
                }
                result.success = false;
                JS_FreeCString(ctx, err);
                JS_FreeValue(ctx, line);
                JS_FreeValue(ctx, exc);
                JS_FreeContext(ctx);
                JS_FreeRuntime(rt);
                return result;
            }

            size_t out_len = 0;
            uint8_t *out_buf = JS_WriteObject(ctx, &out_len, obj, JS_WRITE_OBJ_BYTECODE);
            JS_FreeValue(ctx, obj);

            if (!out_buf) {
                result.errors = "Failed to serialize bytecode";
                result.success = false;
                JS_FreeContext(ctx);
                JS_FreeRuntime(rt);
                return result;
            }

            result.outputContent = String((const u8*)out_buf, out_len);
            js_free(ctx, out_buf);
            
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);

            result.outputPath = req.outputPath;
            result.success = true;
            break;
        }

        default:
            result.errors = "JsLanguage: unsupported compile form";
            result.success = false;
            break;
    }

    return result;
}

}} // namespace Sew::Languages
