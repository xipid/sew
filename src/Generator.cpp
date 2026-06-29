
#include <Sew/Generator.hpp>
#include <cstdio>
#include <cctype>
#include <Collection/Map.hpp>
#include <Collection/Array.hpp>

namespace Sew {

static String removeSpaces(const String& s) {
    String res;
    for (usz i = 0; i < s.length(); ++i) {
        char c = (char)s.data()[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            res.push(c);
        }
    }
    return res;
}

static String cleanType(const String& type) {
    String res = type.trim();
    bool changed = true;
    while (changed) {
        changed = false;
        res = res.trim();
        if (res.startsWith("const ")) {
            res = res.substring(6);
            changed = true;
        } else if (res.startsWith("volatile ")) {
            res = res.substring(9);
            changed = true;
        } else if (res.endsWith(" const")) {
            res = res.substring(0, res.length() - 6);
            changed = true;
        } else if (res.endsWith("volatile")) {
            if (res.endsWith(" volatile")) {
                res = res.substring(0, res.length() - 9);
                changed = true;
            }
        } else if (res.endsWith("*") || res.endsWith("&")) {
            res = res.substring(0, res.length() - 1);
            changed = true;
        }
    }
    if (res.indexOf("Func<") >= 0 || res.indexOf("Func <") >= 0) {
        return res;
    }
    return removeSpaces(res);
}

static String cleanTypeKeepPointer(const String& type) {
    String res = type.trim();
    bool changed = true;
    while (changed) {
        changed = false;
        res = res.trim();
        if (res.startsWith("const ")) {
            res = res.substring(6);
            changed = true;
        } else if (res.startsWith("volatile ")) {
            res = res.substring(9);
            changed = true;
        } else if (res.endsWith(" const")) {
            res = res.substring(0, res.length() - 6);
            changed = true;
        } else if (res.endsWith("&")) {
            res = res.substring(0, res.length() - 1);
            changed = true;
        }
    }
    if (res.indexOf("Func<") >= 0 || res.indexOf("Func <") >= 0) {
        return res;
    }
    return removeSpaces(res);
}

struct TemplateInst {
    String base; // "Array", "Map", "Func"
    String arg1;
    String arg2;
    String cleanName;
    String rawType;
};

static String replaceColons(const String& s);
static String getJsName(const String& fullName);
static bool parseTemplate(const String& rawType, TemplateInst& out);
static bool isClassType(const String& typeStr, const Array<ParsedClass>& classes, String& outClassName);
static String getLambdaArgs(const String& funcSig);

static bool isValidTemplateArg(const String& arg, const Array<ParsedClass>& classes) {
    String clean = cleanType(arg);
    if (clean == "int" || clean == "long" || clean == "short" || clean == "float" || clean == "double" ||
        clean == "i8" || clean == "i16" || clean == "i32" || clean == "i64" ||
        clean == "u8" || clean == "u16" || clean == "u32" || clean == "u64" ||
        clean == "usz" || clean == "size_t" || clean == "bool" || clean == "char" ||
        clean == "String" || clean == "Collection::String" || clean == "Xi::String" || clean == "string" || clean == "void") {
        return true;
    }
    if (arg.indexOf('*') >= 0 || arg.indexOf('&') >= 0) {
        if (arg.indexOf('<') >= 0) return false;
        if (clean == arg) return false;
        return isValidTemplateArg(clean, classes);
    }
    String dummy;
    if (isClassType(clean, classes, dummy)) {
        return true;
    }
    return false;
}

static bool isValidType(const String& typeStr, const Array<ParsedClass>& classes) {
    String clean = cleanType(typeStr);
    if (clean.indexOf('<') >= 0) {
        TemplateInst inst;
        if (parseTemplate(clean, inst)) {
            if (inst.base == "Func") return true;
            if (!isValidTemplateArg(inst.arg1, classes)) return false;
            if (inst.base == "Map" && !isValidTemplateArg(inst.arg2, classes)) return false;
            return true;
        }
        return false;
    }
    return isValidTemplateArg(typeStr, classes);
}

static bool isValidMethod(const ParsedMethod& m, const Array<ParsedClass>& classes) {
    if (!m.isConstructor && !m.isDestructor) {
        if (!isValidType(m.returnType, classes)) return false;
    }
    for (usz k = 0; k < m.params.size(); ++k) {
        if (!isValidType(m.params[k].type, classes)) return false;
    }
    return true;
}

static bool parseTemplate(const String& rawType, TemplateInst& out) {
    String type = rawType.trim();
    long long anglePos = type.indexOf("<");
    if (anglePos < 0) return false;
    
    String base = type.substring(0, (usz)anglePos).trim();
    if (base.endsWith("Array") || base.endsWith("Collection::Array")) {
        out.base = "Array";
    } else if (base.endsWith("Map") || base.endsWith("Collection::Map")) {
        out.base = "Map";
    } else if (base.endsWith("Func") || base.endsWith("Xi::Func")) {
        out.base = "Func";
    } else {
        return false;
    }
    
    long long closeAngle = -1;
    int level = 0;
    for (usz i = (usz)anglePos; i < type.size(); ++i) {
        if (type.data()[i] == '<') level++;
        else if (type.data()[i] == '>') {
            level--;
            if (level == 0) {
                closeAngle = (long long)i;
                break;
            }
        }
    }
    if (closeAngle < 0) return false;
    
    String argsStr = type.substring((usz)anglePos + 1, (usz)closeAngle);
    out.rawType = cleanType(rawType);
    
    if (out.base == "Array") {
        out.arg1 = cleanTypeKeepPointer(argsStr);
        out.cleanName = "Array_" + replaceColons(out.arg1);
    } else if (out.base == "Map") {
        long long commaPos = -1;
        int angleL = 0;
        for (usz i = 0; i < argsStr.size(); ++i) {
            if (argsStr.data()[i] == '<') angleL++;
            else if (argsStr.data()[i] == '>') angleL--;
            else if (argsStr.data()[i] == ',' && angleL == 0) {
                commaPos = (long long)i;
                break;
            }
        }
        if (commaPos < 0) return false;
        out.arg1 = cleanTypeKeepPointer(argsStr.substring(0, (usz)commaPos));
        out.arg2 = cleanTypeKeepPointer(argsStr.substring((usz)commaPos + 1));
        out.cleanName = "Map_" + replaceColons(out.arg1) + "_" + replaceColons(out.arg2);
    } else if (out.base == "Func") {
        out.arg1 = argsStr.trim();
        String cleanArg = replaceColons(out.arg1);
        String cleanName;
        for (usz i = 0; i < cleanArg.size(); ++i) {
            u8 c = cleanArg.data()[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
                cleanName.push(c);
            } else {
                cleanName.push('_');
            }
        }
        out.cleanName = "Func_" + cleanName;
    }
    return true;
}

static bool isStringType(const String& type) {
    if (type.indexOf('*') >= 0) return false;
    String clean = cleanType(type);
    return clean == "String" || clean == "Collection::String" || clean == "Xi::String";
}

static bool isNonConstRefStringType(const String& type) {
    if (!isStringType(type)) return false;
    return type.indexOf('&') >= 0 && type.indexOf("const") < 0;
}

static bool isAbstractClass(const ParsedClass& cls) {
    for (usz i = 0; i < cls.methods.size(); ++i) {
        if (cls.methods[i].isPureVirtual) return true;
    }
    return false;
}

static bool isClassType(const String& typeStr, const Array<ParsedClass>& classes, String& outClassName) {
    if (isStringType(typeStr)) return false;
    String clean = cleanType(typeStr);
    if (clean.indexOf('<') >= 0 || clean.indexOf('>') >= 0) {
        return false;
    }
    for (usz i = 0; i < classes.size(); ++i) {
        String fullName = classes[i].name;
        String shortName = getJsName(fullName);

        const String* names[] = { &fullName, &shortName };
        for (int n = 0; n < 2; ++n) {
            const String& name = *names[n];
            long long pos = clean.indexOf(name);
            if (pos >= 0) {
                bool left = (pos == 0 || !( (clean.data()[pos-1] >= 'a' && clean.data()[pos-1] <= 'z') || (clean.data()[pos-1] >= 'A' && clean.data()[pos-1] <= 'Z') || (clean.data()[pos-1] >= '0' && clean.data()[pos-1] <= '9') || clean.data()[pos-1] == '_' || clean.data()[pos-1] == ':' ));
                usz endPos = (usz)pos + name.length();
                bool right = (endPos == clean.length() || !( (clean.data()[endPos] >= 'a' && clean.data()[endPos] <= 'z') || (clean.data()[endPos] >= 'A' && clean.data()[endPos] <= 'Z') || (clean.data()[endPos] >= '0' && clean.data()[endPos] <= '9') || clean.data()[endPos] == '_' || clean.data()[endPos] == ':' ));
                if (left && right) {
                    outClassName = fullName;
                    return true;
                }
            }
        }
    }
    return false;
}

static String replaceColons(const String& s) {
    String res;
    for (usz i = 0; i < s.length(); ++i) {
        if (i + 1 < s.length() && s.data()[i] == ':' && s.data()[i+1] == ':') {
            res += "_";
            i++;
        } else {
            char c = (char)s.data()[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                // skip whitespace
            } else if (c == '<' || c == '>' || c == ',' || c == '(' || c == ')' || c == '*' || c == '&') {
                res += "_";
            } else {
                res += c;
            }
        }
    }
    String collapsed;
    for (usz i = 0; i < res.length(); ++i) {
        if (res.data()[i] == '_') {
            if (collapsed.isEmpty() || collapsed.data()[collapsed.length()-1] != '_') {
                collapsed.push('_');
            }
        } else {
            collapsed.push(res.data()[i]);
        }
    }
    if (collapsed.endsWith("_") && collapsed.length() > 1) {
        collapsed = collapsed.substring(0, collapsed.length() - 1);
    }
    if (collapsed.startsWith("_") && collapsed.length() > 1) {
        collapsed = collapsed.substring(1);
    }
    return collapsed;
}

static String getJsName(const String& fullName) {
    long long lastColon = -1;
    for (usz i = 0; i + 1 < fullName.length(); ++i) {
        if (fullName.data()[i] == ':' && fullName.data()[i+1] == ':') {
            lastColon = (long long)i;
        }
    }
    String name = (lastColon >= 0) ? fullName.substring((usz)lastColon + 2) : fullName;
    long long anglePos = name.indexOf('<');
    if (anglePos >= 0) name = name.substring(0, (usz)anglePos);
    return name;
}

static String getBridgeType(const String& typeStr, const Array<ParsedClass>& classes) {
    if (isStringType(typeStr)) {
        return "const char*";
    }
    String clean = cleanType(typeStr);
    TemplateInst inst;
    if (parseTemplate(clean, inst)) {
        return inst.rawType + "*";
    }
    String className;
    if (isClassType(clean, classes, className)) {
        return className + "*";
    }
    return typeStr;
}

static String getPassValue(const ParsedParam& param, const Array<ParsedClass>& classes) {
    if (isStringType(param.type)) {
        if (isNonConstRefStringType(param.type)) {
            return "sew_local_" + param.name;
        }
        return "String(" + param.name + ")";
    }
    String clean = cleanType(param.type);
    TemplateInst inst;
    if (parseTemplate(clean, inst)) {
        if (inst.base == "Func") {
            return param.name;
        }
        if (param.type.indexOf('*') < 0) {
            return "*" + param.name;
        }
    }
    String className;
    if (isClassType(clean, classes, className)) {
        if (param.type.indexOf('*') < 0) {
            return "*" + param.name;
        }
    }
    return param.name;
}

static bool isStringAndLengthPattern(const ParsedParam& p1, const ParsedParam& p2) {
    bool p1Str = (p1.type == "const char *" || p1.type == "const char*" || p1.type == "char *" || p1.type == "char*") ||
                 (p1.type == "const u8 *" || p1.type == "const u8*" || p1.type == "u8 *" || p1.type == "u8*");
    bool p2Len = (p2.type == "usz" || p2.type == "size_t" || p2.type == "int" || p2.type == "unsigned" || p2.type == "u32" || p2.type == "u8");
    if (p1Str && p2Len) {
        String name = p2.name.toLowerCase();
        return (name == "len" || name == "length" || name == "c" || name == "count" || name == "l");
    }
    return false;
}

static String getJsType(const String& cppType, const Array<ParsedClass>& classes) {
    String clean = cleanType(cppType);
    if (isStringType(clean)) return "string";
    TemplateInst inst;
    if (parseTemplate(clean, inst)) {
        if (inst.base == "Func") return inst.cleanName;
        return inst.cleanName;
    }
    if (clean == "void") return "void";
    if (clean == "bool") return "boolean";
    if (clean == "const char*" || clean == "const char *" || clean == "char*" || clean == "char *") return "string";
    String className;
    if (isClassType(clean, classes, className)) {
        return getJsName(className);
    }
    return "number";
}

static String getPublicJsType(const String& cppType, const Array<ParsedClass>& classes) {
    String clean = cleanType(cppType);
    if (isStringType(clean)) return "string";
    TemplateInst inst;
    if (parseTemplate(clean, inst)) {
        if (inst.base == "Func") {
            long long openParen = inst.arg1.indexOf("(");
            String retType = openParen >= 0 ? inst.arg1.substring(0, (usz)openParen).trim() : "void";
            String cbArgs = getLambdaArgs(inst.arg1);
            
            String jsRet = "void";
            if (retType == "bool") jsRet = "boolean";
            else if (isStringType(retType)) jsRet = "string";
            else if (retType == "u64" || retType == "i64" || retType == "long long" || retType == "unsigned long long") jsRet = "number";
            
            String jsParams;
            if (!cbArgs.isEmpty()) {
                Array<String> parts = cbArgs.split(",");
                for (usz p = 0; p < parts.size(); ++p) {
                    if (p > 0) jsParams += ", ";
                    jsParams += "arg" + String((long long)p) + ": ";
                    String cleanPart = parts[p].trim();
                    if (cleanPart == "bool") jsParams += "boolean";
                    else if (isStringType(cleanPart)) jsParams += "string";
                    else jsParams += "number";
                }
            }
            return inst.cleanName + " | ((" + jsParams + ") => " + jsRet + ")";
        }
        if (inst.base == "Array") {
            return getPublicJsType(inst.arg1, classes) + "[]";
        }
        if (inst.base == "Map") {
            String kType = getPublicJsType(inst.arg1, classes);
            String vType = getPublicJsType(inst.arg2, classes);
            return "Record<" + kType + ", " + vType + ">";
        }
        return inst.cleanName;
    }
    if (clean == "void") return "void";
    if (clean == "bool") return "boolean";
    if (clean == "const char*" || clean == "const char *" || clean == "char*" || clean == "char *") return "string";
    String className;
    if (isClassType(clean, classes, className)) {
        return getJsName(className);
    }
    return "number";
}

static String getLambdaArgs(const String& funcSig) {
    long long openParen = funcSig.indexOf("(");
    if (openParen < 0) return "";
    long long closeParen = -1;
    int level = 0;
    for (usz i = (usz)openParen; i < funcSig.size(); ++i) {
        if (funcSig.data()[i] == '(') level++;
        else if (funcSig.data()[i] == ')') {
            level--;
            if (level == 0) {
                closeParen = (long long)i;
                break;
            }
        }
    }
    if (closeParen < 0) return "";
    return funcSig.substring((usz)openParen + 1, (usz)closeParen);
}

static String sanitizeParamName(const String& name) {
    static const char* reserved[] = {
        "in", "if", "do", "for", "let", "new", "try", "var", "case", "else",
        "enum", "null", "this", "true", "void", "with", "break", "catch",
        "class", "const", "false", "super", "throw", "while", "yield",
        "async", "await", "delete", "export", "import", "return", "static",
        "switch", "typeof", "default", "extends", "finally", "continue",
        "debugger", "function", "arguments", "implements", "instanceof",
        "interface", "package", "private", "protected", "public", "abstract",
        "declare", "from", "of", "type", "namespace", "module", nullptr
    };
    for (int i = 0; reserved[i] != nullptr; ++i) {
        if (name == reserved[i]) {
            return name + "_";
        }
    }
    return name;
}

static Array<TemplateInst> discoverTemplates(const Array<ParsedClass>& classes, const Array<ParsedFunction>& functions) {
    Array<TemplateInst> list;
    auto addType = [&list, &classes](const String& typeStr) {
        TemplateInst inst;
        if (parseTemplate(typeStr, inst)) {
            if (!isValidType(typeStr, classes)) return;
            bool exists = false;
            for (usz i = 0; i < list.size(); ++i) {
                if (list[i].cleanName == inst.cleanName) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                list.push(inst);
            }
        }
    };

    for (usz i = 0; i < classes.size(); ++i) {
        const ParsedClass& cls = classes[i];
        for (usz j = 0; j < cls.parentClasses.size(); ++j) {
            addType(cls.parentClasses[j]);
        }
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            addType(m.returnType);
            for (usz k = 0; k < m.params.size(); ++k) {
                addType(m.params[k].type);
            }
        }
        for (usz j = 0; j < cls.fields.size(); ++j) {
            addType(cls.fields[j].type);
        }
    }
    for (usz i = 0; i < functions.size(); ++i) {
        const ParsedFunction& fn = functions[i];
        addType(fn.returnType);
        for (usz k = 0; k < fn.params.size(); ++k) {
            addType(fn.params[k].type);
        }
    }
    return list;
}

String BindingGenerator::generateCppBridge(const Array<ParsedClass>& classes,
                                           const Array<ParsedFunction>& functions,
                                           const Array<String>& namespaces,
                                           const Array<String>& headerIncludePaths) {
    String out;
    out += "// Auto-generated C++ bridge by Sew\n\n";
    out += "#include <Collection/String.hpp>\n";
    out += "#include <Xi/Primitives.hpp>\n";
    for (usz i = 0; i < headerIncludePaths.size(); ++i) {
        out += "#include \"" + headerIncludePaths[i] + "\"\n";
    }
    out += "#include <cstdlib>\n";
    out += "#include <cstdint>\n";
    out += "#include <stdint.h>\n";
    out += "#include <type_traits>\n\n";
    out += "using namespace Collection;\n";
    out += "using namespace Xi;\n";

    out += "template<typename T>\n";
    out += "typename std::enable_if<std::is_default_constructible<T>::value && !std::is_abstract<T>::value, T*>::type\n";
    out += "sew_new_default_helper() {\n";
    out += "    return new T();\n";
    out += "}\n\n";
    out += "template<typename T>\n";
    out += "typename std::enable_if<!std::is_default_constructible<T>::value || std::is_abstract<T>::value, T*>::type\n";
    out += "sew_new_default_helper() {\n";
    out += "    return nullptr;\n";
    out += "}\n\n";
    
    out += "template<typename T, typename... Args>\n";
    out += "typename std::enable_if<std::is_constructible<T, Args...>::value && !std::is_abstract<T>::value, T*>::type\n";
    out += "sew_new_args_helper(const Args&... args) {\n";
    out += "    return new T(args...);\n";
    out += "}\n\n";
    out += "template<typename T, typename... Args>\n";
    out += "typename std::enable_if<!std::is_constructible<T, Args...>::value || std::is_abstract<T>::value, T*>::type\n";
    out += "sew_new_args_helper(const Args&...) {\n";
    out += "    return nullptr;\n";
    out += "}\n\n";

    for (usz i = 0; i < namespaces.size(); ++i) {
        if (namespaces[i] != "Collection" && namespaces[i] != "Xi") {
            out += "using namespace " + namespaces[i] + ";\n";
        }
    }
    out += "\n";
    out += "extern \"C\" {\n\n";

    // Callback export declaration
    out += "    __attribute__((weak)) void call_js_callback(int cbId, void* argPtr) {}\n";
    out += "    __attribute__((weak)) bool call_js_callback_bool(int cbId, void* argPtr) { return false; }\n";
    out += "    __attribute__((weak)) void* call_js_callback_ptr(int cbId, void* argPtr) { return nullptr; }\n\n";

    // Allocator helpers
    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void* alloc_buf(int size) {\n";
    out += "    return malloc(size);\n";
    out += "}\n\n";
    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void free_buf(void* ptr) {\n";
    out += "    free(ptr);\n";
    out += "}\n\n";

    // String helpers
    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) const char* export_String_c_str(void* s) {\n";
    out += "    return static_cast<String*>(s)->c_str();\n";
    out += "}\n\n";
    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void export_String_delete(void* s) {\n";
    out += "    delete static_cast<String*>(s);\n";
    out += "}\n\n";
    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) int export_String_size(void* s) {\n";
    out += "    return static_cast<String*>(s)->size();\n";
    out += "}\n\n";
    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void* export_String_new(const char* s) {\n";
    out += "    return new String(s);\n";
    out += "}\n\n";
    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void* export_String_new_with_len(const char* s, int len) {\n";
    out += "    return new String((const u8*)s, len);\n";
    out += "}\n\n";

    // Discover and generate template wrappers
    Array<TemplateInst> templates = discoverTemplates(classes, functions);
    for (usz i = 0; i < templates.size(); ++i) {
        const TemplateInst& inst = templates[i];
        
        if (inst.base == "Array") {
            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += inst.rawType + "* export_" + inst.cleanName + "_new() {\n";
            out += "    return new " + inst.rawType + "();\n";
            out += "}\n\n";

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += "void export_" + inst.cleanName + "_push(" + inst.rawType + "* self, ";
            out += getBridgeType(inst.arg1, classes) + " val) {\n";
            out += "    self->push(" + getPassValue(ParsedParam{inst.arg1, "val", ""}, classes) + ");\n";
            out += "}\n\n";

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += "int export_" + inst.cleanName + "_size(" + inst.rawType + "* self) {\n";
            out += "    return self->size();\n";
            out += "}\n\n";

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += getBridgeType(inst.arg1, classes) + " export_" + inst.cleanName + "_get(" + inst.rawType + "* self, int index) {\n";
            out += "    ";
            String className;
            TemplateInst arg1Inst;
            if (isStringType(inst.arg1)) {
                out += "return self->operator[](index).c_str();\n";
            } else if (isClassType(inst.arg1, classes, className) && inst.arg1.indexOf('*') < 0 && inst.arg1.indexOf('&') < 0) {
                out += "return new " + className + "(self->operator[](index));\n";
            } else if (parseTemplate(inst.arg1, arg1Inst) && inst.arg1.indexOf('*') < 0 && inst.arg1.indexOf('&') < 0) {
                out += "return new " + inst.arg1 + "(self->operator[](index));\n";
            } else {
                out += "return self->operator[](index);\n";
            }
            out += "}\n\n";

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += "void export_" + inst.cleanName + "_delete(" + inst.rawType + "* self) {\n";
            out += "    delete self;\n";
            out += "}\n\n";
        }
        else if (inst.base == "Map") {
            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += inst.rawType + "* export_" + inst.cleanName + "_new() {\n";
            out += "    return new " + inst.rawType + "();\n";
            out += "}\n\n";

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += "void export_" + inst.cleanName + "_set(" + inst.rawType + "* self, ";
            out += getBridgeType(inst.arg1, classes) + " key, " + getBridgeType(inst.arg2, classes) + " val) {\n";
            out += "    self->set(" + getPassValue(ParsedParam{inst.arg1, "key", ""}, classes) + ", " + getPassValue(ParsedParam{inst.arg2, "val", ""}, classes) + ");\n";
            out += "}\n\n";

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += "int export_" + inst.cleanName + "_size(" + inst.rawType + "* self) {\n";
            out += "    return self->size();\n";
            out += "}\n\n";

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += "void* export_" + inst.cleanName + "_keys(" + inst.rawType + "* self) {\n";
            out += "    auto* keys = new Array<" + inst.arg1 + ">();\n";
            out += "    for (auto it = self->begin(); it != self->end(); ++it) {\n";
            out += "        keys->push(it->key);\n";
            out += "    }\n";
            out += "    return keys;\n";
            out += "}\n\n";

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += getBridgeType(inst.arg2, classes) + " export_" + inst.cleanName + "_get(" + inst.rawType + "* self, ";
            out += getBridgeType(inst.arg1, classes) + " key) {\n";
            out += "    auto* val = self->get(" + getPassValue(ParsedParam{inst.arg1, "key", ""}, classes) + ");\n";
            out += "    if (!val) return {};\n";
            String className;
            TemplateInst arg2Inst;
            if (isStringType(inst.arg2)) {
                out += "    return val->c_str();\n";
            } else if (isClassType(inst.arg2, classes, className) && inst.arg2.indexOf('*') < 0 && inst.arg2.indexOf('&') < 0) {
                out += "    return new " + className + "(*val);\n";
            } else if (parseTemplate(inst.arg2, arg2Inst) && inst.arg2.indexOf('*') < 0 && inst.arg2.indexOf('&') < 0) {
                out += "    return new " + inst.arg2 + "(*val);\n";
            } else {
                out += "    return *val;\n";
            }
            out += "}\n\n";

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += "void export_" + inst.cleanName + "_delete(" + inst.rawType + "* self) {\n";
            out += "    delete self;\n";
            out += "}\n\n";
        }
        else if (inst.base == "Func") {
            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += "void export_" + inst.cleanName + "_delete(" + inst.rawType + "* self) {\n";
            out += "    delete self;\n";
            out += "}\n\n";

            if (inst.cleanName == "Func_bool_u64_u64") {
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void* export_Func_bool_u64_u64_new(int cbId) {\n";
                out += "    return new Func<bool(u64, u64)>([=](u64 offset, u64 size) -> bool {\n";
                out += "        struct Args { u64 offset; u64 size; } args = { offset, size };\n";
                out += "        return call_js_callback_bool(cbId, (void*)&args);\n";
                out += "    });\n";
                out += "}\n\n";
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) bool export_Func_bool_u64_u64_call(Func<bool(u64, u64)>* self, u64 offset, u64 size) {\n";
                out += "    return (*self)(offset, size);\n";
                out += "}\n\n";
            }
            else if (inst.cleanName == "Func_bool_u64_String") {
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void* export_Func_bool_u64_String_new(int cbId) {\n";
                out += "    return new Func<bool(u64, String)>([=](u64 offset, String data) -> bool {\n";
                out += "        struct Args { u64 offset; u64 dataPtr; u64 dataLen; } args = { offset, (u64)(uintptr_t)data.c_str(), (u64)data.size() };\n";
                out += "        return call_js_callback_bool(cbId, (void*)&args);\n";
                out += "    });\n";
                out += "}\n\n";
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) bool export_Func_bool_u64_String_call(Func<bool(u64, String)>* self, u64 offset, const char* data) {\n";
                out += "    return (*self)(offset, String(data));\n";
                out += "}\n\n";
            }
            else if (inst.cleanName == "Func_String_u64_u64") {
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void* export_Func_String_u64_u64_new(int cbId) {\n";
                out += "    return new Func<String(u64, u64)>([=](u64 offset, u64 size) -> String {\n";
                out += "        struct Args { u64 offset; u64 size; } args = { offset, size };\n";
                out += "        void* resPtr = call_js_callback_ptr(cbId, (void*)&args);\n";
                out += "        if (resPtr) {\n";
                out += "            String s(*static_cast<String*>(resPtr));\n";
                out += "            delete static_cast<String*>(resPtr);\n";
                out += "            return s;\n";
                out += "        }\n";
                out += "        return String();\n";
                out += "    });\n";
                out += "}\n\n";
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) void* export_Func_String_u64_u64_call(Func<String(u64, u64)>* self, u64 offset, u64 size) {\n";
                out += "    return new String((*self)(offset, size));\n";
                out += "}\n\n";
            }
        }
    }

    // For each class
    for (usz i = 0; i < classes.size(); ++i) {
        const ParsedClass& cls = classes[i];
        String bridgeClsName = replaceColons(cls.name);

        // Constructors
        if (!isAbstractClass(cls)) {
            bool hasConstructor = false;
            for (usz j = 0; j < cls.methods.size(); ++j) {
                if (cls.methods[j].isConstructor) {
                    hasConstructor = true;
                    break;
                }
            }
            if (!hasConstructor) {
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
                out += cls.name + "* export_" + bridgeClsName + "_new_default() {\n";
                out += "    return sew_new_default_helper<" + cls.name + ">();\n";
                out += "}\n\n";
            }
            for (usz j = 0; j < cls.methods.size(); ++j) {
                const ParsedMethod& m = cls.methods[j];
                if (m.isConstructor) {
                    if (!isValidMethod(m, classes)) continue;
                    out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
                    out += cls.name + "* export_" + bridgeClsName + "_new_" + String((long long)j) + "(";
                    for (usz k = 0; k < m.params.size(); ++k) {
                        if (k > 0) out += ", ";
                        out += getBridgeType(m.params[k].type, classes) + " " + m.params[k].name;
                    }
                    out += ") {\n";
                    for (usz k = 0; k < m.params.size(); ++k) {
                        if (isNonConstRefStringType(m.params[k].type)) {
                            out += "    String sew_local_" + m.params[k].name + "(" + m.params[k].name + ");\n";
                        }
                    }
                    out += "    return sew_new_args_helper<" + cls.name;
                    for (usz k = 0; k < m.params.size(); ++k) {
                        out += ", " + m.params[k].type;
                    }
                    out += ">(";
                    for (usz k = 0; k < m.params.size(); ++k) {
                        if (k > 0) out += ", ";
                        out += getPassValue(m.params[k], classes);
                    }
                    out += ");\n";
                    out += "}\n\n";
                }
            }
        }

        // Destructor
        out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
        out += "void export_" + bridgeClsName + "_delete(" + cls.name + "* self) {\n";
        out += "    delete self;\n";
        out += "}\n\n";

        // Methods
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (!m.isConstructor && !m.isDestructor) {
                if (!isValidMethod(m, classes)) continue;
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
                String retBridgeType = isStringType(m.returnType) ? "void*" : getBridgeType(m.returnType, classes);
                out += retBridgeType + " export_" + bridgeClsName + "_" + m.name + "_" + String((long long)j) + "(";
                if (!m.isStatic) {
                    out += cls.name + "* self";
                    if (m.params.size() > 0) out += ", ";
                }
                for (usz k = 0; k < m.params.size(); ++k) {
                    if (k > 0) out += ", ";
                    TemplateInst pInst;
                    if (parseTemplate(m.params[k].type, pInst) && pInst.base == "Func") {
                        out += "int " + m.params[k].name + "_cbId";
                    } else {
                        out += getBridgeType(m.params[k].type, classes) + " " + m.params[k].name;
                    }
                }
                out += ") {\n";
                for (usz k = 0; k < m.params.size(); ++k) {
                    if (isNonConstRefStringType(m.params[k].type)) {
                        out += "    String sew_local_" + m.params[k].name + "(" + m.params[k].name + ");\n";
                    }
                }

                // Setup callback lambdas
                for (usz k = 0; k < m.params.size(); ++k) {
                    TemplateInst pInst;
                    if (parseTemplate(m.params[k].type, pInst) && pInst.base == "Func") {
                        String argsList = getLambdaArgs(pInst.arg1);
                        out += "    auto " + m.params[k].name + " = [=](" + argsList + " arg) {\n";
                        out += "        call_js_callback(" + m.params[k].name + "_cbId, (void*)&arg);\n";
                        out += "    };\n";
                    }
                }

                String className;
                TemplateInst retInst;
                if (m.returnType != "void") {
                    if (isStringType(m.returnType)) {
                        out += "return new String(";
                    } else if (isClassType(m.returnType, classes, className)) {
                        out += "return ";
                        if (m.returnType.indexOf('*') >= 0) {
                            if (m.returnType.indexOf("const") >= 0) {
                                out += "const_cast<" + className + "*>(";
                            }
                        } else if (m.returnType.indexOf('&') >= 0) {
                            if (m.returnType.indexOf("const") >= 0) {
                                out += "const_cast<" + className + "*>(&";
                            } else {
                                out += "&";
                            }
                        } else {
                            out += "new " + className + "(";
                        }
                    } else if (parseTemplate(m.returnType, retInst)) {
                        out += "return ";
                        String retClean = cleanTypeKeepPointer(m.returnType);
                        if (retClean.endsWith("*")) {
                            if (m.returnType.indexOf("const") >= 0) {
                                out += "const_cast<" + retBridgeType + ">(";
                            }
                        } else if (m.returnType.indexOf('&') >= 0) {
                            if (m.returnType.indexOf("const") >= 0) {
                                out += "const_cast<" + retBridgeType + ">(&";
                            } else {
                                out += "&";
                            }
                        } else {
                            out += "new " + m.returnType + "(";
                        }
                    } else {
                        out += "return ";
                    }
                }

                if (m.isStatic) {
                    out += cls.name + "::" + m.name + "(";
                } else {
                    out += "self->" + m.name + "(";
                }

                for (usz k = 0; k < m.params.size(); ++k) {
                    if (k > 0) out += ", ";
                    out += getPassValue(m.params[k], classes);
                }
                out += ")";

                if (m.returnType != "void") {
                    if (isStringType(m.returnType)) {
                        out += ")";
                    } else if (isClassType(m.returnType, classes, className)) {
                        if (m.returnType.indexOf('*') < 0 && m.returnType.indexOf('&') < 0) {
                            out += ")";
                        } else if (m.returnType.indexOf("const") >= 0) {
                            out += ")";
                        }
                    } else if (parseTemplate(m.returnType, retInst)) {
                        String retClean2 = cleanTypeKeepPointer(m.returnType);
                        if (!retClean2.endsWith("*") && m.returnType.indexOf('&') < 0) {
                            out += ")";
                        } else if (m.returnType.indexOf("const") >= 0) {
                            out += ")";
                        }
                    }
                }
                out += ";\n";
                out += "}\n\n";
            }
        }

        // Fields (Getters / Setters)
        for (usz j = 0; j < cls.fields.size(); ++j) {
            const ParsedField& f = cls.fields[j];
            if (!isValidType(f.type, classes)) continue;
            String fBridgeType = getBridgeType(f.type, classes);

            out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
            out += fBridgeType + " export_" + bridgeClsName + "_get_" + f.name + "(" + cls.name + "* self) {\n";
            if (isStringType(f.type)) {
                if (f.isStatic) {
                    out += "    return " + cls.name + "::" + f.name + ".c_str();\n";
                } else {
                    out += "    return self->" + f.name + ".c_str();\n";
                }
            } else {
                if (f.isStatic) {
                    if (fBridgeType.endsWith("*") && !f.type.trim().endsWith("*")) {
                        out += "    return &" + cls.name + "::" + f.name + ";\n";
                    } else {
                        out += "    return " + cls.name + "::" + f.name + ";\n";
                    }
                } else {
                    if (fBridgeType.endsWith("*") && !f.type.trim().endsWith("*")) {
                        out += "    return &self->" + f.name + ";\n";
                    } else {
                        out += "    return self->" + f.name + ";\n";
                    }
                }
            }
            out += "}\n\n";

            if (!f.isConst) {
                out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
                out += "void export_" + bridgeClsName + "_set_" + f.name + "(" + cls.name + "* self, " + fBridgeType + " value) {\n";
                if (isStringType(f.type)) {
                    if (f.isStatic) {
                        out += "    " + cls.name + "::" + f.name + " = String(value);\n";
                    } else {
                        out += "    self->" + f.name + " = String(value);\n";
                    }
                } else {
                    if (f.isStatic) {
                        if (fBridgeType.endsWith("*") && !f.type.trim().endsWith("*")) {
                            out += "    " + cls.name + "::" + f.name + " = *value;\n";
                        } else {
                            out += "    " + cls.name + "::" + f.name + " = value;\n";
                        }
                    } else {
                        if (fBridgeType.endsWith("*") && !f.type.trim().endsWith("*")) {
                            out += "    self->" + f.name + " = *value;\n";
                        } else {
                            out += "    self->" + f.name + " = value;\n";
                        }
                    }
                }
                out += "}\n\n";
            }
        }
    }

    // Global Functions
    for (usz i = 0; i < functions.size(); ++i) {
        const ParsedFunction& fn = functions[i];
        bool fnValid = isValidType(fn.returnType, classes);
        for (usz k = 0; k < fn.params.size(); ++k) {
            if (!isValidType(fn.params[k].type, classes)) {
                fnValid = false;
                break;
            }
        }
        if (!fnValid) continue;
        out += "__attribute__((visibility(\"default\"))) __attribute__((used)) ";
        String retBridgeType = isStringType(fn.returnType) ? "void*" : getBridgeType(fn.returnType, classes);
        out += retBridgeType + " export_" + replaceColons(fn.name) + "_" + String((long long)i) + "(";
        for (usz j = 0; j < fn.params.size(); ++j) {
            if (j > 0) out += ", ";
            TemplateInst pInst;
            if (parseTemplate(fn.params[j].type, pInst) && pInst.base == "Func") {
                out += "int " + fn.params[j].name + "_cbId";
            } else {
                out += getBridgeType(fn.params[j].type, classes) + " " + fn.params[j].name;
            }
        }
        out += ") {\n";
        for (usz j = 0; j < fn.params.size(); ++j) {
            if (isNonConstRefStringType(fn.params[j].type)) {
                out += "    String sew_local_" + fn.params[j].name + "(" + fn.params[j].name + ");\n";
            }
        }

        for (usz k = 0; k < fn.params.size(); ++k) {
            TemplateInst pInst;
            if (parseTemplate(fn.params[k].type, pInst) && pInst.base == "Func") {
                String argsList = getLambdaArgs(pInst.arg1);
                out += "    auto " + fn.params[k].name + " = [=](" + argsList + " arg) {\n";
                out += "        call_js_callback(" + fn.params[k].name + "_cbId, (void*)&arg);\n";
                out += "    };\n";
            }
        }

        String className;
        TemplateInst retInst;
        if (fn.returnType != "void") {
            if (isStringType(fn.returnType)) {
                out += "return new String(";
            } else if (isClassType(fn.returnType, classes, className)) {
                out += "return ";
                if (fn.returnType.indexOf('*') >= 0) {
                    // Returns pointer
                } else if (fn.returnType.indexOf('&') >= 0) {
                    out += "&";
                } else {
                    out += "new " + className + "(";
                }
            } else if (parseTemplate(fn.returnType, retInst)) {
                out += "return ";
                String fnRetClean = cleanTypeKeepPointer(fn.returnType);
                if (fnRetClean.endsWith("*")) {
                    // Returns pointer
                } else if (fn.returnType.indexOf('&') >= 0) {
                    out += "&";
                } else {
                    out += "new " + fn.returnType + "(";
                }
            } else {
                out += "return ";
            }
        }

        out += fn.name + "(";
        for (usz j = 0; j < fn.params.size(); ++j) {
            if (j > 0) out += ", ";
            out += getPassValue(fn.params[j], classes);
        }
        out += ")";

        if (fn.returnType != "void") {
            if (isStringType(fn.returnType)) {
                out += ")";
            } else if (isClassType(fn.returnType, classes, className) && fn.returnType.indexOf('*') < 0 && fn.returnType.indexOf('&') < 0) {
                out += ")";
            } else if (parseTemplate(fn.returnType, retInst)) {
                String fnRetClean2 = cleanTypeKeepPointer(fn.returnType);
                if (!fnRetClean2.endsWith("*") && fn.returnType.indexOf('&') < 0) {
                    out += ")";
                }
            }
        }
        out += ";\n";
        out += "}\n\n";
    }

    out += "}\n";
    return out;
}


String BindingGenerator::generateTsGlue(const Array<ParsedClass>& classes,
                                        const Array<ParsedFunction>& functions,
                                        const String& wasmFileName) {
    String out = R"js(// Auto-generated by Sew — TypeScript WASM bindings

let wasmInstance: any;
let wasmMemory: any;
let exports: any;

const files: Record<string, { type: string; contents: Uint8Array }> = {
  "/": { type: "dir", contents: new Uint8Array(0) }
};

const glfwWindows = new Map();
let glfwNextWindowId = 1;
const glfwHints = new Map();
let glfwCurrentWindow = 0;

function mapJsKeyToGlfw(code: string): number {
  if (!code) return 0;
  if (code.startsWith("Key")) {
    return code.charCodeAt(3); // 'KeyA' -> 65
  }
  if (code.startsWith("Digit")) {
    return code.charCodeAt(5); // 'Digit0' -> 48
  }
  const special: Record<string, number> = {
    "Space": 32, "Enter": 257, "Tab": 258, "Escape": 256,
    "Backspace": 259, "Insert": 260, "Delete": 261,
    "ArrowRight": 262, "ArrowLeft": 263, "ArrowDown": 264, "ArrowUp": 265,
    "PageUp": 266, "PageDown": 267, "Home": 268, "End": 269,
    "CapsLock": 280, "ScrollLock": 281, "NumLock": 282, "PrintScreen": 283, "Pause": 284,
    "F1": 290, "F2": 291, "F3": 292, "F4": 293, "F5": 294, "F6": 295,
    "F7": 296, "F8": 297, "F9": 298, "F10": 299, "F11": 300, "F12": 301,
    "ShiftLeft": 340, "ControlLeft": 341, "AltLeft": 342, "MetaLeft": 343,
    "ShiftRight": 344, "ControlRight": 345, "AltRight": 346, "MetaRight": 347,
  };
  return special[code] || 0;
}

function resolvePath(dirPath: string, path: string): string {
  if (path.startsWith("/")) return path;
  const parts = (dirPath + "/" + path).split("/");
  const stack: string[] = [];
  for (const part of parts) {
    if (!part || part === ".") continue;
    if (part === "..") { stack.pop(); }
    else { stack.push(part); }
  }
  return "/" + stack.join("/");
}

const fds = new Map<number, { path: string; pos: number; flags: number }>([
  [0, { path: "/dev/stdin", pos: 0, flags: 0 }],
  [1, { path: "/dev/stdout", pos: 0, flags: 0 }],
  [2, { path: "/dev/stderr", pos: 0, flags: 0 }],
  [3, { path: "/", pos: 0, flags: 0 }]
]);
let nextFd = 4;

const gpuRegistry = new Map<number, any>();
let nextGpuId = 1;
function regGpu(obj: any): number {
  const id = nextGpuId++;
  gpuRegistry.set(id, obj);
  return id;
}

let cmdArgs: string[] = [];

const callbackRegistry = new Map<number, Function>();
export function call_js_callback(cbId: number, argPtr: number) {
  const cb = callbackRegistry.get(cbId);
  if (cb) {
    cb(argPtr);
  }
}
export function call_js_callback_bool(cbId: number, argPtr: number): number {
  const cb = callbackRegistry.get(cbId);
  if (cb) {
    return cb(argPtr) ? 1 : 0;
  }
  return 0;
}
export function call_js_callback_ptr(cbId: number, argPtr: number): number {
  const cb = callbackRegistry.get(cbId);
  if (cb) {
    return cb(argPtr) || 0;
  }
  return 0;
}

export async function init(wasmUrl: string, args: string[] = []): Promise<void> {
  cmdArgs = [wasmUrl, ...args];
  const response = await fetch(wasmUrl);
  const buffer = await response.arrayBuffer();
  wasmMemory = new WebAssembly.Memory({ initial: 256 });
  
  const imports = {
    env: {
      memory: wasmMemory,
      call_js_callback: call_js_callback,
      call_js_callback_bool: call_js_callback_bool,
      call_js_callback_ptr: call_js_callback_ptr,

      _ZN2Xi4Time5sleepEd: () => { },
      _ZN2Xi16secureRandomFillERN10Collection6StringEm: (strPtr: number, len: number) => {
        if (typeof crypto !== 'undefined' && crypto.getRandomValues && len > 0) {
          const memory = exports || wasmMemory;
          const dataView = new Uint32Array(memory.buffer, strPtr, 1);
          const char_data_ptr = dataView[0];
          const view = new Uint8Array(memory.buffer, char_data_ptr, len);
          crypto.getRandomValues(view);
        }
      },
      _ZN2Xi10randomSeedEN10Collection6StringE: () => { },
      __syscall_unlinkat: () => 0,
      __syscall_bind: () => 0,
      __syscall_getsockname: () => 0,
      __syscall_recvfrom: () => 0,
      __syscall_sendto: () => 0,
      __syscall_socket: () => 0,

      // ─── GLFW 3 Host Bridge ─────────────────────────────────────────────
      glfwInit: () => {
        return 1; // GLFW_TRUE
      },
      glfwTerminate: () => {
        glfwWindows.clear();
      },
      glfwWindowHint: (hint: number, value: number) => {
        glfwHints.set(hint, value);
      },
      glfwDefaultWindowHints: () => {
        glfwHints.clear();
      },
      glfwCreateWindow: (width: number, height: number, titlePtr: number, monitor: number, share: number) => {
        const id = glfwNextWindowId++;
        let canvas: any = null;
        if (typeof document !== 'undefined') {
          canvas = document.querySelector('canvas') || document.createElement('canvas');
          if (!canvas.parentNode) {
            document.body.appendChild(canvas);
          }
          canvas.width = width;
          canvas.height = height;
        }
        
        const winState = {
          id,
          canvas,
          width,
          height,
          shouldClose: 0,
          callbacks: {
            key: 0,
            cursorPos: 0,
            mouseButton: 0,
            scroll: 0,
            windowSize: 0,
            framebufferSize: 0,
          },
          keys: new Uint8Array(512),
          mouseButtons: new Uint8Array(8),
          mouseX: 0,
          mouseY: 0,
        };

        if (canvas) {
          const handleKey = (e: KeyboardEvent, action: number) => {
            const glfwKey = mapJsKeyToGlfw(e.code);
            if (glfwKey > 0) {
              winState.keys[glfwKey] = action;
              if (winState.callbacks.key) {
                const func = wasmInstance.exports.__indirect_function_table.get(winState.callbacks.key);
                func(id, glfwKey, e.keyCode, action, 0);
              }
            }
          };
          window.addEventListener('keydown', (e) => handleKey(e, 1)); // GLFW_PRESS
          window.addEventListener('keyup', (e) => handleKey(e, 0));   // GLFW_RELEASE

          canvas.addEventListener('mousedown', (e: MouseEvent) => {
            const btn = e.button;
            winState.mouseButtons[btn] = 1;
            if (winState.callbacks.mouseButton) {
              const func = wasmInstance.exports.__indirect_function_table.get(winState.callbacks.mouseButton);
              func(id, btn, 1, 0); // GLFW_PRESS
            }
          });
          canvas.addEventListener('mouseup', (e: MouseEvent) => {
            const btn = e.button;
            winState.mouseButtons[btn] = 0;
            if (winState.callbacks.mouseButton) {
              const func = wasmInstance.exports.__indirect_function_table.get(winState.callbacks.mouseButton);
              func(id, btn, 0, 0); // GLFW_RELEASE
            }
          });
          canvas.addEventListener('mousemove', (e: MouseEvent) => {
            const rect = canvas.getBoundingClientRect();
            winState.mouseX = e.clientX - rect.left;
            winState.mouseY = e.clientY - rect.top;
            if (winState.callbacks.cursorPos) {
              const func = wasmInstance.exports.__indirect_function_table.get(winState.callbacks.cursorPos);
              func(id, winState.mouseX, winState.mouseY);
            }
          });
          canvas.addEventListener('wheel', (e: WheelEvent) => {
            if (winState.callbacks.scroll) {
              const func = wasmInstance.exports.__indirect_function_table.get(winState.callbacks.scroll);
              const dx = e.deltaX ? (e.deltaX > 0 ? 1 : -1) : 0;
              const dy = e.deltaY ? (e.deltaY > 0 ? -1 : 1) : 0;
              func(id, dx, dy);
            }
          });
        }
        
        glfwWindows.set(id, winState);
        return id;
      },
      glfwWindowShouldClose: (winId: number) => {
        const win = glfwWindows.get(winId);
        return win ? win.shouldClose : 1;
      },
      glfwSetWindowShouldClose: (winId: number, value: number) => {
        const win = glfwWindows.get(winId);
        if (win) win.shouldClose = value;
      },
      glfwDestroyWindow: (winId: number) => {
        glfwWindows.delete(winId);
      },
      glfwPollEvents: () => {},
      glfwWaitEvents: () => {},
      glfwSetKeyCallback: (winId: number, cbPtr: number) => {
        const win = glfwWindows.get(winId);
        if (win) win.callbacks.key = cbPtr;
      },
      glfwSetCursorPosCallback: (winId: number, cbPtr: number) => {
        const win = glfwWindows.get(winId);
        if (win) win.callbacks.cursorPos = cbPtr;
      },
      glfwSetMouseButtonCallback: (winId: number, cbPtr: number) => {
        const win = glfwWindows.get(winId);
        if (win) win.callbacks.mouseButton = cbPtr;
      },
      glfwSetScrollCallback: (winId: number, cbPtr: number) => {
        const win = glfwWindows.get(winId);
        if (win) win.callbacks.scroll = cbPtr;
      },
      glfwSetWindowSizeCallback: (winId: number, cbPtr: number) => {
        const win = glfwWindows.get(winId);
        if (win) win.callbacks.windowSize = cbPtr;
      },
      glfwSetFramebufferSizeCallback: (winId: number, cbPtr: number) => {
        const win = glfwWindows.get(winId);
        if (win) win.callbacks.framebufferSize = cbPtr;
      },
      glfwGetWindowSize: (winId: number, wPtr: number, hPtr: number) => {
        const win = glfwWindows.get(winId);
        if (win && exports) {
          const view = new DataView(exports.memory.buffer);
          view.setInt32(wPtr, win.width, true);
          view.setInt32(hPtr, win.height, true);
        }
      },
      glfwGetFramebufferSize: (winId: number, wPtr: number, hPtr: number) => {
        const win = glfwWindows.get(winId);
        if (win && exports) {
          const view = new DataView(exports.memory.buffer);
          view.setInt32(wPtr, win.width, true);
          view.setInt32(hPtr, win.height, true);
        }
      },
      glfwGetKey: (winId: number, key: number) => {
        const win = glfwWindows.get(winId);
        return win ? win.keys[key] : 0;
      },
      glfwGetMouseButton: (winId: number, button: number) => {
        const win = glfwWindows.get(winId);
        return win ? win.mouseButtons[button] : 0;
      },
      glfwGetCursorPos: (winId: number, xPtr: number, yPtr: number) => {
        const win = glfwWindows.get(winId);
        if (win && exports) {
          const view = new DataView(exports.memory.buffer);
          view.setFloat64(xPtr, win.mouseX, true);
          view.setFloat64(yPtr, win.mouseY, true);
        }
      },
      glfwMakeContextCurrent: (winId: number) => {
        glfwCurrentWindow = winId;
      },
      glfwGetCurrentContext: () => {
        return glfwCurrentWindow;
      },
      glfwSwapBuffers: (winId: number) => {},
      glfwGetTime: () => {
        return performance.now() / 1000.0;
      },
      glfwSetTime: (time: number) => {},

      // ─── xic fetch API ──────────────────────────────────────────────────
      xic_fetch_get: (urlPtr: number, onSuccessPtr: number, onErrorPtr: number) => {
        const memory = exports || wasmMemory;
        let len = 0;
        while (new Uint8Array(memory.buffer, urlPtr + len, 1)[0] !== 0) len++;
        const url = new TextDecoder().decode(new Uint8Array(memory.buffer, urlPtr, len));

        fetch(url)
          .then(res => res.arrayBuffer())
          .then(buf => {
            const bytes = new Uint8Array(buf);
            const mallocPtr = exports.malloc(bytes.length + 1);
            const dest = new Uint8Array(memory.buffer, mallocPtr, bytes.length + 1);
            dest.set(bytes);
            dest[bytes.length] = 0;
            exports.__indirect_function_table.get(onSuccessPtr)(mallocPtr, bytes.length);
            exports.free(mallocPtr);
          })
          .catch(err => {
            const errBytes = new TextEncoder().encode(err.message || String(err));
            const mallocPtr = exports.malloc(errBytes.length + 1);
            const dest = new Uint8Array(memory.buffer, mallocPtr, errBytes.length + 1);
            dest.set(errBytes);
            dest[errBytes.length] = 0;
            exports.__indirect_function_table.get(onErrorPtr)(mallocPtr);
            exports.free(mallocPtr);
          });
      },

      xic_fetch_post: (urlPtr: number, bodyPtr: number, onSuccessPtr: number, onErrorPtr: number) => {
        const memory = exports || wasmMemory;
        let len = 0;
        while (new Uint8Array(memory.buffer, urlPtr + len, 1)[0] !== 0) len++;
        const url = new TextDecoder().decode(new Uint8Array(memory.buffer, urlPtr, len));

        let bodyLen = 0;
        while (new Uint8Array(memory.buffer, bodyPtr + bodyLen, 1)[0] !== 0) bodyLen++;
        const body = new TextDecoder().decode(new Uint8Array(memory.buffer, bodyPtr, bodyLen));

        fetch(url, { method: "POST", body })
          .then(res => res.arrayBuffer())
          .then(buf => {
            const bytes = new Uint8Array(buf);
            const mallocPtr = exports.malloc(bytes.length + 1);
            const dest = new Uint8Array(memory.buffer, mallocPtr, bytes.length + 1);
            dest.set(bytes);
            dest[bytes.length] = 0;
            exports.__indirect_function_table.get(onSuccessPtr)(mallocPtr, bytes.length);
            exports.free(mallocPtr);
          })
          .catch(err => {
            const errBytes = new TextEncoder().encode(err.message || String(err));
            const mallocPtr = exports.malloc(errBytes.length + 1);
            const dest = new Uint8Array(memory.buffer, mallocPtr, errBytes.length + 1);
            dest.set(errBytes);
            dest[errBytes.length] = 0;
            exports.__indirect_function_table.get(onErrorPtr)(mallocPtr);
            exports.free(mallocPtr);
          });
      },

      // ─── Standard WebGPU C API to JS Host Bridge (4-Arg Callback Standard compliant) ──────────────────────
      wgpuRequestAdapter: (canvasIdPtr: number, callbackPtr: number, userdata: number) => {
        if (!navigator.gpu) {
          console.error("WebGPU not supported on this browser.");
          wasmInstance.exports.__indirect_function_table.get(callbackPtr)(4, 0, 0, userdata); // status = 4 (Error)
          return;
        }
        const canvasId = readString(canvasIdPtr);
        navigator.gpu.requestAdapter().then(adapter => {
          if (!adapter) {
            wasmInstance.exports.__indirect_function_table.get(callbackPtr)(3, 0, 0, userdata); // status = 3 (Unavailable)
            return;
          }
          const id = regGpu(adapter);
          const canvas = (typeof document !== 'undefined') ? document.getElementById(canvasId) : null;
          if (canvas) {
            (adapter as any)._canvas = canvas;
          }
          // Pass 4 arguments: (WGPURequestAdapterStatus status, WGPUAdapter adapter, char const * message, void * userdata)
          const func = wasmInstance.exports.__indirect_function_table.get(callbackPtr);
          if (func.length === 4) {
             func(1, id, 0, userdata); // status = 1 (Success) [1.1.8]
          } else {
             func(id, userdata); // Fallback to 2-args
          }
        }).catch((err) => {
          console.error("Adapter request failed:", err);
          const func = wasmInstance.exports.__indirect_function_table.get(callbackPtr);
          if (func.length === 4) {
             func(4, 0, 0, userdata); // status = 4 (Error) [1.1.8]
          } else {
             func(0, userdata); // Fallback to 2-args
          }
        });
      },

      wgpuAdapterRequestDevice: (adapterId: number, callbackPtr: number, userdata: number) => {
        const adapter = gpuRegistry.get(adapterId);
        adapter.requestDevice().then((device: any) => {
          const id = regGpu(device);
          if (adapter._canvas) {
            const context = adapter._canvas.getContext("webgpu");
            const format = navigator.gpu.getPreferredCanvasFormat();
            context.configure({ device, format, alphaMode: "opaque" });
            device._contextId = regGpu(context);
          }
          // Pass 4 arguments: (WGPURequestDeviceStatus status, WGPUDevice device, char const * message, void * userdata)
          const func = wasmInstance.exports.__indirect_function_table.get(callbackPtr);
          if (func.length === 4) {
             func(1, id, 0, userdata); // status = 1 (Success) [1.1.8]
          } else {
             func(id, userdata); // Fallback to 2-args
          }
        }).catch((err) => {
          console.error("Device request failed:", err);
          const func = wasmInstance.exports.__indirect_function_table.get(callbackPtr);
          if (func.length === 4) {
             func(3, 0, 0, userdata); // status = 3 (Error) [1.1.8]
          } else {
             func(0, userdata); // Fallback to 2-args
          }
        });
      },
      wgpuDeviceGetQueue: (deviceId: number) => {
        const device = gpuRegistry.get(deviceId);
        return regGpu(device.queue);
      },

      wgpuDeviceCreateShaderModule: (deviceId: number, descriptorPtr: number) => {
        const device = gpuRegistry.get(deviceId);
        const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;
        const view = new DataView(activeMemory);
        const chainPtr = view.getUint32(descriptorPtr, true);
        const codePtr = view.getUint32(chainPtr, true);
        const code = readString(codePtr);
        const sm = device.createShaderModule({ code });
        return regGpu(sm);
      },

      wgpuDeviceCreateBuffer: (deviceId: number, descriptorPtr: number) => {
        const device = gpuRegistry.get(deviceId);
        const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;
        const view = new DataView(activeMemory);
        const labelPtr = view.getUint32(descriptorPtr, true);
        const usage = view.getUint32(descriptorPtr + 4, true);
        const size = Number(view.getBigUint64(descriptorPtr + 8, true));
        const mappedAtCreation = view.getUint8(descriptorPtr + 16) !== 0;
        const buffer = device.createBuffer({
          label: labelPtr ? readString(labelPtr) : undefined,
          size,
          usage,
          mappedAtCreation
        });
        return regGpu(buffer);
      },

      wgpuBufferWrite: (queueId: number, bufferId: number, bufferOffset: number, dataPtr: number, size: number) => {
        const queue = gpuRegistry.get(queueId);
        const buffer = gpuRegistry.get(bufferId);
        const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;
        const view = new Uint8Array(activeMemory, dataPtr, size);
        queue.writeBuffer(buffer, bufferOffset, view, 0, size);
      },

      wgpuBufferDestroy: (bufferId: number) => {
        const buffer = gpuRegistry.get(bufferId);
        if (buffer) {
          buffer.destroy();
          gpuRegistry.delete(bufferId);
        }
      },

      wgpuDeviceCreateBindGroupLayout: (deviceId: number, descriptorPtr: number) => {
        const device = gpuRegistry.get(deviceId);
        const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;
        const view = new DataView(activeMemory);
        const labelPtr = view.getUint32(descriptorPtr, true);
        const entryCount = view.getUint32(descriptorPtr + 4, true);
        const entriesPtr = view.getUint32(descriptorPtr + 8, true);
        const entries: any[] = [];
        for (let i = 0; i < entryCount; i++) {
          const entryOffset = entriesPtr + i * 8;
          entries.push({
            binding: view.getUint32(entryOffset, true),
            visibility: view.getUint32(entryOffset + 4, true),
            buffer: {}
          });
        }
        const bgl = device.createBindGroupLayout({
          label: labelPtr ? readString(labelPtr) : undefined,
          entries
        });
        return regGpu(bgl);
      },

      wgpuDeviceCreateBindGroup: (deviceId: number, descriptorPtr: number) => {
        const device = gpuRegistry.get(deviceId);
        const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;
        const view = new DataView(activeMemory);
        const labelPtr = view.getUint32(descriptorPtr, true);
        const layoutId = view.getUint32(descriptorPtr + 4, true);
        const entryCount = view.getUint32(descriptorPtr + 8, true);
        const entriesPtr = view.getUint32(descriptorPtr + 12, true);
        const layout = gpuRegistry.get(layoutId);
        const entries: any[] = [];
        for (let i = 0; i < entryCount; i++) {
          const entryOffset = entriesPtr + i * 28;
          const binding = view.getUint32(entryOffset, true);
          const bufferId = view.getUint32(entryOffset + 4, true);
          const offset = Number(view.getBigUint64(entryOffset + 8, true));
          const size = Number(view.getBigUint64(entryOffset + 16, true));
          const resource: any = {
            buffer: gpuRegistry.get(bufferId),
            offset,
            size
          };
          entries.push({ binding, resource });
        }
        const bg = device.createBindGroup({
          label: labelPtr ? readString(labelPtr) : undefined,
          layout,
          entries
        });
        return regGpu(bg);
      },

      wgpuDeviceCreateCommandEncoder: (deviceId: number, labelPtr: number) => {
        const device = gpuRegistry.get(deviceId);
        const encoder = device.createCommandEncoder({
          label: labelPtr ? readString(labelPtr) : undefined
        });

        encoder._contextId = device._contextId;
        encoder.device = device;

        return regGpu(encoder);
      },

      wgpuCommandEncoderBeginComputePass: (encoderId: number, labelPtr: number) => {
        const encoder = gpuRegistry.get(encoderId);
        const pass = encoder.beginComputePass({
          label: labelPtr ? readString(labelPtr) : undefined
        });
        return regGpu(pass);
      },

      wgpuComputePassSetPipeline: (passId: number, pipelineId: number) => {
        const pass = gpuRegistry.get(passId);
        const pipeline = gpuRegistry.get(pipelineId);
        pass.setPipeline(pipeline);
      },

      wgpuComputePassSetBindGroup: (passId: number, groupIndex: number, bindGroupId: number, dynamicOffsetCount: number, dynamicOffsetsPtr: number) => {
        const pass = gpuRegistry.get(passId);
        const bg = gpuRegistry.get(bindGroupId);
        let offsets: number[] | undefined = undefined;
        if (dynamicOffsetCount > 0 && dynamicOffsetsPtr) {
          const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;
          offsets = Array.from(new Uint32Array(activeMemory, dynamicOffsetsPtr, dynamicOffsetCount));
        }
        pass.setBindGroup(groupIndex, bg, offsets);
      },

      wgpuComputePassDispatchWorkgroups: (passId: number, x: number, y: number, z: number) => {
        const pass = gpuRegistry.get(passId);
        pass.dispatchWorkgroups(x, y, z);
      },

      wgpuComputePassEnd: (passId: number) => {
        const pass = gpuRegistry.get(passId);
        pass.end();
        gpuRegistry.delete(passId);
      },

      wgpuCommandEncoderFinish: (encoderId: number, labelPtr: number) => {
        const encoder = gpuRegistry.get(encoderId);
        const cb = encoder.finish({
          label: labelPtr ? readString(labelPtr) : undefined
        });
        gpuRegistry.delete(encoderId);
        return regGpu(cb);
      },

      wgpuQueueSubmit: (queueId: number, commandCount: number, commandsPtr: number) => {
        const queue = gpuRegistry.get(queueId);
        const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;
        const view = new Uint32Array(activeMemory, commandsPtr, commandCount);
        const commandBuffers: any[] = [];
        for (let i = 0; i < commandCount; i++) {
          const cb = gpuRegistry.get(view[i]);
          commandBuffers.push(cb);
          gpuRegistry.delete(view[i]);
        }
        queue.submit(commandBuffers);
      },

      jsRequestAnimationFrame: (callbackPtr: number) => {
        requestAnimationFrame(() => {
          wasmInstance.exports.__indirect_function_table.get(callbackPtr)();
        });
      },

      wgpuCommandEncoderBeginRenderPass: (encoderId: number, descriptorPtr: number) => {
        const encoder = gpuRegistry.get(encoderId);
        const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;
        const view = new DataView(activeMemory);
        const colorAttachmentCount = view.getUint32(descriptorPtr, true);
        const colorAttachmentsPtr = view.getUint32(descriptorPtr + 4, true);
        const colorAttachments: any[] = [];
        for (let i = 0; i < colorAttachmentCount; i++) {
          const offset = colorAttachmentsPtr + i * 48;
          const texViewId = view.getUint32(offset, true);
          const resolveTargetId = view.getUint32(offset + 4, true);
          const loadOp = view.getUint32(offset + 8, true);
          const storeOp = view.getUint32(offset + 12, true);
          const r = view.getFloat64(offset + 16, true);
          const g = view.getFloat64(offset + 24, true);
          const b = view.getFloat64(offset + 32, true);
          const a = view.getFloat64(offset + 40, true);
          let textureView;
          if (texViewId === 0) {
            const context = gpuRegistry.get(encoder._contextId || encoder.device?._contextId);
            textureView = context.getCurrentTexture().createView();
          } else {
            textureView = gpuRegistry.get(texViewId);
          }
          colorAttachments.push({
            view: textureView,
            resolveTarget: resolveTargetId ? gpuRegistry.get(resolveTargetId) : undefined,
            clearValue: { r, g, b, a },
            loadOp: loadOp === 1 ? 'clear' : 'load',
            storeOp: storeOp === 1 ? 'store' : 'discard'
          });
        }
        const pass = encoder.beginRenderPass({ colorAttachments });
        return regGpu(pass);
      },

      wgpuRenderPassEncoderEnd: (passId: number) => {
        const pass = gpuRegistry.get(passId);
        pass.end();
        gpuRegistry.delete(passId);
      },
    },
    
    wasi_snapshot_preview1: {
      proc_exit: (code: number) => {
        throw new Error(`Exit code: ${code}`);
      },
      
      environ_sizes_get: (environ_count_ptr: number, environ_size_ptr: number) => {
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        view.setUint32(environ_count_ptr, 0, true);
        view.setUint32(environ_size_ptr, 0, true);
        return 0;
      },
      
      environ_get: () => 0,
      
      args_sizes_get: (argc_ptr: number, argv_len_ptr: number) => {
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        view.setUint32(argc_ptr, cmdArgs.length, true);
        
        let totalSize = 0;
        const encoder = new TextEncoder();
        for (const arg of cmdArgs) {
          totalSize += encoder.encode(arg).length + 1;
        }
        view.setUint32(argv_len_ptr, totalSize, true);
        return 0;
      },
      
      args_get: (argv_ptr: number, argv_buf_ptr: number) => {
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        const bytes = new Uint8Array(memory.buffer);
        const encoder = new TextEncoder();
        
        let currentOffset = argv_buf_ptr;
        for (let i = 0; i < cmdArgs.length; i++) {
          view.setUint32(argv_ptr + i * 4, currentOffset, true);
          const encoded = encoder.encode(cmdArgs[i]);
          bytes.set(encoded, currentOffset);
          bytes[currentOffset + encoded.length] = 0;
          currentOffset += encoded.length + 1;
        }
        return 0;
      },
      
      fd_prestat_get: (fd: number, prestat_ptr: number) => {
        if (fd === 3) {
          const memory = wasmInstance.exports.memory || wasmMemory;
          const view = new DataView(memory.buffer);
          view.setUint8(prestat_ptr, 0); // pr_type = preopen_dir (0)
          view.setUint32(prestat_ptr + 4, 1, true); // "/" length = 1
          return 0;
        }
        return 8; // EBADF
      },
      
      fd_prestat_dir_name: (fd: number, path_ptr: number, path_len: number) => {
        if (fd === 3) {
          const memory = wasmInstance.exports.memory || wasmMemory;
          const bytes = new Uint8Array(memory.buffer, path_ptr, path_len);
          bytes.set(new TextEncoder().encode("/"));
          return 0;
        }
        return 8; // EBADF
      },
      
      fd_fdstat_get: (fd: number, fdstat_ptr: number) => {
        const desc = fds.get(fd);
        if (!desc) return 8; // EBADF
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        view.setUint8(fdstat_ptr, desc.path === "/" ? 3 : 4);
        view.setUint16(fdstat_ptr + 2, 0, true);
        return 0;
      },
      
      fd_write: (fd: number, iovs: number, iovs_len: number, nwrittenOutPtr: number) => {
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        let totalWritten = 0;
        let str = "";
        
        for (let i = 0; i < iovs_len; i++) {
          const bufPtr = view.getUint32(iovs + i * 8, true);
          const bufLen = view.getUint32(iovs + i * 8 + 4, true);
          const bytes = new Uint8Array(memory.buffer, bufPtr, bufLen);
          
          if (fd === 1 || fd === 2) {
            str += new TextDecoder().decode(bytes);
          } else {
            const desc = fds.get(fd);
            if (!desc) return 8; // EBADF
            let file = files[desc.path];
            if (!file) {
              file = { type: "file", contents: new Uint8Array(0) };
              files[desc.path] = file;
            }
            const newContents = new Uint8Array(desc.pos + bufLen);
            newContents.set(file.contents);
            newContents.set(bytes, desc.pos);
            file.contents = newContents;
            desc.pos += bufLen;
          }
          totalWritten += bufLen;
        }
        
        if (fd === 1 || fd === 2) {
          if (typeof process !== "undefined" && process.stdout && process.stdout.write) {
            process.stdout.write(str);
          } else {
            console.log(str);
          }
        }
        
        view.setUint32(nwrittenOutPtr, totalWritten, true);
        return 0;
      },
      
      fd_read: (fd: number, iovs: number, iovs_len: number, nreadOutPtr: number) => {
        const desc = fds.get(fd);
        if (!desc) return 8; // EBADF
        const file = files[desc.path];
        if (!file || file.type !== "file") return 44; // ENOENT
        
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        let totalRead = 0;
        
        for (let i = 0; i < iovs_len; i++) {
          const bufPtr = view.getUint32(iovs + i * 8, true);
          const bufLen = view.getUint32(iovs + i * 8 + 4, true);
          
          if (desc.pos >= file.contents.length) break;
          const remaining = file.contents.length - desc.pos;
          const toRead = Math.min(bufLen, remaining);
          const dest = new Uint8Array(memory.buffer, bufPtr, toRead);
          dest.set(file.contents.subarray(desc.pos, desc.pos + toRead));
          desc.pos += toRead;
          totalRead += toRead;
        }
        
        view.setUint32(nreadOutPtr, totalRead, true);
        return 0;
      },
      
      fd_seek: (fd: number, offset_low: number, offset_high: number, whence: number, newoffset_ptr: number) => {
        const desc = fds.get(fd);
        if (!desc) return 8;
        const file = files[desc.path];
        if (!file) return 8;
        
        const size = file.contents.length;
        if (whence === 0) desc.pos = offset_low;
        else if (whence === 1) desc.pos += offset_low;
        else if (whence === 2) desc.pos = size + offset_low;
        
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        view.setUint32(newoffset_ptr, desc.pos, true);
        view.setUint32(newoffset_ptr + 4, 0, true);
        return 0;
      },
      
      fd_close: (fd: number) => {
        if (!fds.has(fd)) return 8;
        fds.delete(fd);
        return 0;
      },
      
      path_open: (dirfd: number, dirflags: number, path_ptr: number, path_len: number, oflags: number, fs_rights_base: number, fs_rights_inheriting: number, fdflags: number, fd_ptr: number) => {
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        const pathBytes = new Uint8Array(memory.buffer, path_ptr, path_len);
        const relPath = new TextDecoder().decode(pathBytes);
        const dirDesc = fds.get(dirfd);
        if (!dirDesc) return 8;
        const fullPath = resolvePath(dirDesc.path, relPath);
        
        const O_CREAT = (oflags & 1) !== 0;
        const O_TRUNC = (oflags & 8) !== 0;
        
        let file = files[fullPath];
        if (!file) {
          if (O_CREAT) {
            file = { type: "file", contents: new Uint8Array(0) };
            files[fullPath] = file;
          } else {
            return 44; // ENOENT
          }
        } else if (O_TRUNC) {
          file.contents = new Uint8Array(0);
        }
        
        const newFd = nextFd++;
        fds.set(newFd, { path: fullPath, pos: 0, flags: fdflags });
        view.setUint32(fd_ptr, newFd, true);
        return 0;
      },
      
      path_filestat_get: (dirfd: number, flags: number, path_ptr: number, path_len: number, stat_ptr: number) => {
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        const pathBytes = new Uint8Array(memory.buffer, path_ptr, path_len);
        const relPath = new TextDecoder().decode(pathBytes);
        const dirDesc = fds.get(dirfd);
        if (!dirDesc) return 8;
        const fullPath = resolvePath(dirDesc.path, relPath);
        
        const file = files[fullPath];
        if (!file) return 44;
        
        view.setUint8(stat_ptr + 16, file.type === "dir" ? 3 : 4);
        view.setBigUint64(stat_ptr + 24, BigInt(file.contents ? file.contents.length : 0), true);
        return 0;
      },
      
      random_get: (buf_ptr: number, buf_len: number) => {
        const memory = wasmInstance.exports.memory || wasmMemory;
        const bytes = new Uint8Array(memory.buffer, buf_ptr, buf_len);
        if (typeof crypto !== "undefined" && crypto.getRandomValues) {
          crypto.getRandomValues(bytes);
        } else {
          for (let i = 0; i < buf_len; i++) bytes[i] = Math.floor(Math.random() * 256);
        }
        return 0;
      },
      
      clock_time_get: (id: number, precision: number, time_ptr: number) => {
        const memory = wasmInstance.exports.memory || wasmMemory;
        const view = new DataView(memory.buffer);
        view.setBigUint64(time_ptr, BigInt(Math.floor(performance.now() * 1e6)), true);
        return 0;
      }
    }
  };
  
  const { instance } = await WebAssembly.instantiate(buffer, imports);
  wasmInstance = instance;
  exports = instance.exports;
}

export function getExports(): WebAssembly.Exports {
  return exports;
}
)js";

    // Discover templates
    Array<TemplateInst> templates = discoverTemplates(classes, functions);

    // Finalization Registry
    out += "const registry = new FinalizationRegistry((info: { ptr: number, type: string }) => {\n";
    out += "  if (!exports) return;\n";
    for (usz i = 0; i < classes.size(); ++i) {
        out += "  if (info.type === '" + classes[i].name + "') exports.export_" + replaceColons(classes[i].name) + "_delete(info.ptr);\n";
    }
    for (usz i = 0; i < templates.size(); ++i) {
        out += "  if (info.type === '" + templates[i].cleanName + "') exports.export_" + templates[i].cleanName + "_delete(info.ptr);\n";
    }
    out += "});\n\n";

    // Dynamic Memory Helpers for Standard WASM Memory Growth
    out += "function writeString(str: string): number {\n";
    out += "  const encoder = new TextEncoder();\n";
    out += "  const bytes = encoder.encode(str);\n";
    out += "  const ptr = exports.alloc_buf(bytes.length + 1);\n";
    out += "  const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;\n";
    out += "  const view = new Uint8Array(activeMemory, ptr, bytes.length + 1);\n";
    out += "  view.set(bytes);\n";
    out += "  view[bytes.length] = 0;\n";
    out += "  return ptr;\n";
    out += "}\n\n";

    out += "function writeBuffer(buf: Uint8Array | string): { ptr: number, len: number } {\n";
    out += "  const bytes = typeof buf === 'string' ? new TextEncoder().encode(buf) : buf;\n";
    out += "  const ptr = exports.alloc_buf(bytes.length);\n";
    out += "  const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;\n";
    out += "  const view = new Uint8Array(activeMemory, ptr, bytes.length);\n";
    out += "  view.set(bytes);\n";
    out += "  return { ptr, len: bytes.length };\n";
    out += "}\n\n";

    out += "function readString(ptr: number): string {\n";
    out += "  const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;\n";
    out += "  const view = new Uint8Array(activeMemory, ptr);\n";
    out += "  let len = 0;\n";
    out += "  while (view[len] !== 0) len++;\n";
    out += "  const bytes = new Uint8Array(activeMemory, ptr, len);\n";
    out += "  return new TextDecoder().decode(bytes);\n";
    out += "}\n\n";

    out += "function readBinaryString(ptr: number, size: number): string {\n";
    out += "  const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;\n";
    out += "  const bytes = new Uint8Array(activeMemory, ptr, size);\n";
    out += "  let res = '';\n";
    out += "  for (let i = 0; i < bytes.length; ++i) res += String.fromCharCode(bytes[i]);\n";
    out += "  return res;\n";
    out += "}\n\n";

    // Output templates wrappers
    for (usz i = 0; i < templates.size(); ++i) {
        const TemplateInst& inst = templates[i];
        
        if (inst.base == "Array") {
            String elemPubType = getPublicJsType(inst.arg1, classes);
            String jsT = getJsType(inst.arg1, classes);

            out += "export class " + inst.cleanName + " {\n";
            if (wasmFileName.length() > 0) {
                out += "  ptr: number;\n";
                out += "  constructor(ptr: number, internal: symbol, owner?: boolean);\n";
                out += "  constructor();\n";
            }
            out += "  constructor(...args: any[]) {\n";
            out += "    if (args.length >= 2 && args[1] === INTERNAL) {\n";
            out += "      this.ptr = args[0];\n";
            out += "      const owned = args[2] !== false;\n";
            out += "      if (owned) {\n";
            out += "        registry.register(this, { ptr: this.ptr, type: '" + inst.cleanName + "' }, this);\n";
            out += "      }\n";
            out += "    } else {\n";
            out += "      this.ptr = exports.export_" + inst.cleanName + "_new();\n";
            out += "      registry.register(this, { ptr: this.ptr, type: '" + inst.cleanName + "' }, this);\n";
            out += "    }\n";
            out += "  }\n\n";

            out += "  push(val: " + elemPubType + "): void {\n";
            if (jsT == "string") {
                out += "    const p = writeString(val);\n";
                out += "    exports.export_" + inst.cleanName + "_push(this.ptr, p);\n";
                out += "    exports.free_buf(p);\n";
            } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                out += "    exports.export_" + inst.cleanName + "_push(this.ptr, val.ptr);\n";
            } else {
                out += "    exports.export_" + inst.cleanName + "_push(this.ptr, val);\n";
            }
            out += "  }\n\n";

            out += "  size(): number {\n";
            out += "    return exports.export_" + inst.cleanName + "_size(this.ptr);\n";
            out += "  }\n\n";

            out += "  get(index: number): " + elemPubType + " {\n";
            String className;
            TemplateInst arg1Inst;
            bool isCls = isClassType(inst.arg1, classes, className);
            bool isTmpl = parseTemplate(inst.arg1, arg1Inst);
            if (jsT == "string") {
                out += "    return readString(exports.export_" + inst.cleanName + "_get(this.ptr, index));\n";
            } else if (isCls) {
                bool owned = (inst.arg1.indexOf('*') < 0 && inst.arg1.indexOf('&') < 0);
                out += "    return new " + jsT + "(exports.export_" + inst.cleanName + "_get(this.ptr, index), INTERNAL, " + (owned ? "true" : "false") + ");\n";
            } else if (isTmpl) {
                bool owned = (inst.arg1.indexOf('*') < 0 && inst.arg1.indexOf('&') < 0);
                out += "    const resPtr = exports.export_" + inst.cleanName + "_get(this.ptr, index);\n";
                out += "    if (resPtr === 0) return undefined;\n";
                out += "    const wrapper = new " + arg1Inst.cleanName + "(resPtr, INTERNAL, " + (owned ? "true" : "false") + ");\n";
                out += "    const jsVal = wrapper.toJS();\n";
                out += String("    if (") + (owned ? "true" : "false") + ") wrapper.delete();\n";
                out += "    return jsVal;\n";
            } else {
                out += "    return exports.export_" + inst.cleanName + "_get(this.ptr, index);\n";
            }
            out += "  }\n\n";

            out += "  delete(): void {\n";
            out += "    if (this.ptr) {\n";
            out += "      registry.unregister(this);\n";
            out += "      exports.export_" + inst.cleanName + "_delete(this.ptr);\n";
            out += "      this.ptr = 0;\n";
            out += "    }\n";
            out += "  }\n\n";

            out += "  toJS(): " + elemPubType + "[] {\n";
            out += "    const arr: " + elemPubType + "[] = [];\n";
            out += "    const sz = this.size();\n";
            out += "    for (let i = 0; i < sz; ++i) {\n";
            out += "      arr.push(this.get(i));\n";
            out += "    }\n";
            out += "    return arr;\n";
            out += "  }\n\n";

            out += "  static fromJS(arr: " + elemPubType + "[]): " + inst.cleanName + " {\n";
            out += "   const res = new " + inst.cleanName + "();";
            out += R"( for (const item of arr) {
                res.push(item);
            }
            return res;)";
            out += "  }}\n\n";
        }
        else if (inst.base == "Map") {
            String kPubType = getPublicJsType(inst.arg1, classes);
            String vPubType = getPublicJsType(inst.arg2, classes);
            String jsT1 = getJsType(inst.arg1, classes);
            String jsT2 = getJsType(inst.arg2, classes);

            out += "export class " + inst.cleanName + " {\n";
            if (wasmFileName.length() > 0) {
                out += "  ptr: number;\n";
                out += "  constructor(ptr: number, internal: symbol, owner?: boolean);\n";
                out += "  constructor();\n";
            }
            out += "  constructor(...args: any[]) {\n";
            out += "    if (args.length >= 2 && args[1] === INTERNAL) {\n";
            out += "      this.ptr = args[0];\n";
            out += "      const owned = args[2] !== false;\n";
            out += "      if (owned) {\n";
            out += "        registry.register(this, { ptr: this.ptr, type: '" + inst.cleanName + "' }, this);\n";
            out += "      }\n";
            out += "    } else {\n";
            out += "      this.ptr = exports.export_" + inst.cleanName + "_new();\n";
            out += "      registry.register(this, { ptr: this.ptr, type: '" + inst.cleanName + "' }, this);\n";
            out += "    }\n";
            out += "  }\n\n";

            out += "  set(key: " + kPubType + ", val: " + vPubType + "): void {\n";
            if (jsT1 == "string") out += "    const pK = writeString(key);\n";
            if (jsT2 == "string") out += "    const pV = writeString(val);\n";
            out += "    exports.export_" + inst.cleanName + "_set(this.ptr, ";
            out += (jsT1 == "string" ? "pK" : (jsT1 == "number" ? "key" : "key.ptr"));
            out += ", ";
            out += (jsT2 == "string" ? "pV" : (jsT2 == "number" ? "val" : "val.ptr"));
            out += ");\n";
            if (jsT1 == "string") out += "    exports.free_buf(pK);\n";
            if (jsT2 == "string") out += "    exports.free_buf(pV);\n";
            out += "  }\n\n";

            out += "  size(): number {\n";
            out += "    return exports.export_" + inst.cleanName + "_size(this.ptr);\n";
            out += "  }\n\n";

            out += "  get(key: " + kPubType + "): " + vPubType + " | undefined {\n";
            if (jsT1 == "string") out += "    const pK = writeString(key);\n";
            out += "    const resPtr = exports.export_" + inst.cleanName + "_get(this.ptr, ";
            out += (jsT1 == "string" ? "pK" : (jsT1 == "number" ? "key" : "key.ptr"));
            out += ");\n";
            if (jsT1 == "string") out += "    exports.free_buf(pK);\n";
            out += "    if (resPtr === 0) return undefined;\n";
            String className;
            TemplateInst arg2Inst;
            bool isCls2 = isClassType(inst.arg2, classes, className);
            bool isTmpl2 = parseTemplate(inst.arg2, arg2Inst);
            if (jsT2 == "string") {
                out += "    return readString(resPtr);\n";
            } else if (isCls2) {
                bool owned = (inst.arg2.indexOf('*') < 0 && inst.arg2.indexOf('&') < 0);
                out += "    return new " + jsT2 + "(resPtr, INTERNAL, " + (owned ? "true" : "false") + ");\n";
            } else if (isTmpl2) {
                bool owned = (inst.arg2.indexOf('*') < 0 && inst.arg2.indexOf('&') < 0);
                out += "    const wrapper = new " + arg2Inst.cleanName + "(resPtr, INTERNAL, " + (owned ? "true" : "false") + ");\n";
                out += R"(    const jsVal = wrapper.toJS();\n";
                if (owned) wrapper.delete();
                return jsVal;)";
            } else {
                out += "    return resPtr;\n";
            }
            out += "  }\n\n";

            out += "  delete(): void {\n";
            out += "    if (this.ptr) {\n";
            out += "      registry.unregister(this);\n";
            out += "      exports.export_" + inst.cleanName + "_delete(this.ptr);\n";
            out += "      this.ptr = 0;\n";
            out += "    }\n";
            out += "  }\n\n";

            out += "  toJS(): Record<" + kPubType + ", " + vPubType + "> {\n";
            out += "    const obj: Record<" + kPubType + ", " + vPubType + "> = {} as Record<" + kPubType + ", " + vPubType + ">;\n";
            out += "    const keysPtr = exports.export_" + inst.cleanName + "_keys(this.ptr);\n";
            out += "    const keysArr = new Array_" + replaceColons(inst.arg1) + "(keysPtr, INTERNAL);\n";
            out += "    const keys = keysArr.toJS();\n";
            out += "    keysArr.delete();\n";
            out += "    for (const k of keys) {\n";
            out += "      obj[k] = this.get(k)!;\n";
            out += "    }\n";
            out += "    return obj;\n";
            out += "  }\n\n";

            out += "  static fromJS(obj: Record<" + kPubType + ", " + vPubType + ">): " + inst.cleanName + " {\n";
            out += "  const res = new " + inst.cleanName + "();";
            out += R"( for (const k of Object.keys(obj)) {
                res.set(k as any, (obj as any)[k]);
            }
            return res;})";
        }
        else if (inst.base == "Func") {
            if (inst.cleanName == "Func_bool_u64_u64") {
                out += "export class Func_bool_u64_u64 extends Function {\n";
                out += "  ptr: number;\n";
                out += "  constructor(ptr: number, internal: symbol, owner?: boolean);\n";
                out += "  constructor(cb: (offset: number, size: number) => boolean);\n";
                out += "  constructor(...args: any[]) {\n";
                out += "    super();\n";
                out += "    if (args.length >= 2 && args[1] === INTERNAL) {\n";
                out += "      const fn = function(offset: number, size: number): boolean {\n";
                out += "        return exports.export_Func_bool_u64_u64_call((fn as any).ptr, BigInt(offset), BigInt(size)) !== 0;\n";
                out += "      };\n";
                out += "      Object.setPrototypeOf(fn, Func_bool_u64_u64.prototype);\n";
                out += "      (fn as any).ptr = args[0];\n";
                out += "      const owned = args[2] !== false;\n";
                out += "      if (owned) {\n";
                out += "        registry.register(fn, { ptr: (fn as any).ptr, type: 'Func_bool_u64_u64' }, fn);\n";
                out += "      }\n";
                out += "      return fn as any;\n";
                out += "    } else {\n";
                out += "      const cb = args[0];\n";
                out += "      const cbId = nextCallbackId++;\n";
                out += "      callbackRegistry.set(cbId, (argPtr: number) => {\n";
                out += "        const view = new DataView(exports.memory.buffer);\n";
                out += "        const offset = view.getBigUint64(argPtr, true);\n";
                out += "        const size = view.getBigUint64(argPtr + 8, true);\n";
                out += "        return cb(Number(offset), Number(size)) ? 1 : 0;\n";
                out += "      });\n";
                out += "      const ptr = exports.export_Func_bool_u64_u64_new(cbId);\n";
                out += "      const fn = function(offset: number, size: number): boolean {\n";
                out += "        return cb(offset, size);\n";
                out += "      };\n";
                out += "      Object.setPrototypeOf(fn, Func_bool_u64_u64.prototype);\n";
                out += "      (fn as any).ptr = ptr;\n";
                out += "      registry.register(fn, { ptr: (fn as any).ptr, type: 'Func_bool_u64_u64' }, fn);\n";
                out += "      return fn as any;\n";
                out += "    }\n";
                out += "  }\n";
                out += "  delete(): void {\n";
                out += "    if (this.ptr) {\n";
                out += "      registry.unregister(this);\n";
                out += "      exports.export_Func_bool_u64_u64_delete(this.ptr);\n";
                out += "      this.ptr = 0;\n";
                out += "    }\n";
                out += "  }\n";
                out += "}\n\n";
            }
            else if (inst.cleanName == "Func_bool_u64_String") {
                out += "export class Func_bool_u64_String extends Function {\n";
                out += "  ptr: number;\n";
                out += "  constructor(ptr: number, internal: symbol, owner?: boolean);\n";
                out += "  constructor(cb: (offset: number, data: string) => boolean);\n";
                out += "  constructor(...args: any[]) {\n";
                out += "    super();\n";
                out += "    if (args.length >= 2 && args[1] === INTERNAL) {\n";
                out += "      const fn = function(offset: number, data: string): boolean {\n";
                out += "        const cStrPtr = writeString(data);\n";
                out += "        const res = exports.export_Func_bool_u64_String_call((fn as any).ptr, BigInt(offset), cStrPtr) !== 0;\n";
                out += "        exports.free_buf(cStrPtr);\n";
                out += "        return res;\n";
                out += "      };\n";
                out += "      Object.setPrototypeOf(fn, Func_bool_u64_String.prototype);\n";
                out += "      (fn as any).ptr = args[0];\n";
                out += "      const owned = args[2] !== false;\n";
                out += "      if (owned) {\n";
                out += "        registry.register(fn, { ptr: (fn as any).ptr, type: 'Func_bool_u64_String' }, fn);\n";
                out += "      }\n";
                out += "      return fn as any;\n";
                out += "    } else {\n";
                out += "      const cb = args[0];\n";
                out += "      const cbId = nextCallbackId++;\n";
                out += "      callbackRegistry.set(cbId, (argPtr: number) => {\n";
                out += "        const view = new DataView(exports.memory.buffer);\n";
                out += "        const offset = view.getBigUint64(argPtr, true);\n";
                out += "        const dataPtr = Number(view.getBigUint64(argPtr + 8, true));\n";
                out += "        const dataLen = Number(view.getBigUint64(argPtr + 16, true));\n";
                out += "        const bytes = new Uint8Array(exports.memory.buffer, dataPtr, dataLen);\n";
                out += "        const dataStr = new TextDecoder().decode(bytes);\n";
                out += "        return cb(Number(offset), dataStr) ? 1 : 0;\n";
                out += "      });\n";
                out += "      const ptr = exports.export_Func_bool_u64_String_new(cbId);\n";
                out += "      const fn = function(offset: number, data: string): boolean {\n";
                out += "        return cb(offset, data);\n";
                out += "      };\n";
                out += "      Object.setPrototypeOf(fn, Func_bool_u64_String.prototype);\n";
                out += "      (fn as any).ptr = ptr;\n";
                out += "      registry.register(fn, { ptr: (fn as any).ptr, type: 'Func_bool_u64_String' }, fn);\n";
                out += "      return fn as any;\n";
                out += "    }\n";
                out += "  }\n";
                out += "  delete(): void {\n";
                out += "    if (this.ptr) {\n";
                out += "      registry.unregister(this);\n";
                out += "      exports.export_Func_bool_u64_String_delete(this.ptr);\n";
                out += "      this.ptr = 0;\n";
                out += "    }\n";
                out += "  }\n";
                out += "}\n\n";
            }
            else if (inst.cleanName == "Func_String_u64_u64") {
                out += "export class Func_String_u64_u64 extends Function {\n";
                out += "  ptr: number;\n";
                out += "  constructor(ptr: number, internal: symbol, owner?: boolean);\n";
                out += "  constructor(cb: (offset: number, size: number) => string | Uint8Array);\n";
                out += "  constructor(...args: any[]) {\n";
                out += "    super();\n";
                out += "    if (args.length >= 2 && args[1] === INTERNAL) {\n";
                out += "      const fn = function(offset: number, size: number): string {\n";
                out += "        const resStringObjPtr = exports.export_Func_String_u64_u64_call((fn as any).ptr, BigInt(offset), BigInt(size));\n";
                out += "        const strVal = readBinaryString(exports.export_String_c_str(resStringObjPtr), exports.export_String_size(resStringObjPtr));\n";
                out += "        exports.export_String_delete(resStringObjPtr);\n";
                out += "        return strVal;\n";
                out += "      };\n";
                out += "      Object.setPrototypeOf(fn, Func_String_u64_u64.prototype);\n";
                out += "      (fn as any).ptr = args[0];\n";
                out += "      const owned = args[2] !== false;\n";
                out += "      if (owned) {\n";
                out += "        registry.register(fn, { ptr: (fn as any).ptr, type: 'Func_String_u64_u64' }, fn);\n";
                out += "      }\n";
                out += "      return fn as any;\n";
                out += "    } else {\n";
                out += "      const cb = args[0];\n";
                out += "      const cbId = nextCallbackId++;\n";
                out += "      callbackRegistry.set(cbId, (argPtr: number) => {\n";
                out += "        const view = new DataView(exports.memory.buffer);\n";
                out += "        const offset = view.getBigUint64(argPtr, true);\n";
                out += "        const size = view.getBigUint64(argPtr + 8, true);\n";
                out += "        const res = cb(Number(offset), Number(size));\n";
                out += "        let bufBytes: Uint8Array;\n";
                out += "        if (typeof res === 'string') {\n";
                out += "          if (typeof Buffer !== 'undefined') {\n";
                out += "            bufBytes = Buffer.from(res, 'binary');\n";
                out += "          } else {\n";
                out += "            bufBytes = new Uint8Array(res.length);\n";
                out += "            for (let i = 0; i < res.length; ++i) bufBytes[i] = res.charCodeAt(i) & 0xFF;\n";
                out += "          }\n";
                out += "        } else {\n";
                out += "          bufBytes = res;\n";
                out += "        }\n";
                out += "        const cStrPtr = exports.alloc_buf(bufBytes.length);\n";
                out += "        const viewBytes = new Uint8Array(exports.memory.buffer, cStrPtr, bufBytes.length);\n";
                out += "        viewBytes.set(bufBytes);\n";
                out += "        const resStringObjPtr = exports.export_String_new_with_len(cStrPtr, bufBytes.length);\n";
                out += "        exports.free_buf(cStrPtr);\n";
                out += "        return resStringObjPtr;\n";
                out += "      });\n";
                out += "      const ptr = exports.export_Func_String_u64_u64_new(cbId);\n";
                out += "      const fn = function(offset: number, size: number): string | Uint8Array {\n";
                out += "        return cb(offset, size);\n";
                out += "      };\n";
                out += "      Object.setPrototypeOf(fn, Func_String_u64_u64.prototype);\n";
                out += "      (fn as any).ptr = ptr;\n";
                out += "      registry.register(fn, { ptr: (fn as any).ptr, type: 'Func_String_u64_u64' }, fn);\n";
                out += "      return fn as any;\n";
                out += "    }\n";
                out += "  }\n";
                out += "  delete(): void {\n";
                out += "    if (this.ptr) {\n";
                out += "      registry.unregister(this);\n";
                out += "      exports.export_Func_String_u64_u64_delete(this.ptr);\n";
                out += "      this.ptr = 0;\n";
                out += "    }\n";
                out += "  }\n";
                out += "}\n\n";
            }
        }
    }

    // For each class
    for (usz i = 0; i < classes.size(); ++i) {
        const ParsedClass& cls = classes[i];
        String jsClsName = getJsName(cls.name);
        String bridgeClsName = replaceColons(cls.name);

        if (cls.docComment.length() > 0) {
            out += "/**\n";
            Array<String> docLines = cls.docComment.split("\n");
            for (usz j = 0; j < docLines.size(); ++j) {
                out += " * " + docLines[j] + "\n";
            }
            out += " */\n";
        }

        String parentStr;
        String validParentName;
        if (cls.parentClasses.size() > 0) {
            String dummy;
            if (isClassType(cls.parentClasses[0], classes, dummy)) {
                validParentName = getJsName(cls.parentClasses[0]);
                parentStr = " extends " + validParentName;
            }
        }

        out += "export class " + jsClsName + parentStr + " {\n";
        if (validParentName.isEmpty() && wasmFileName.length() > 0) {
            out += "  ptr: number;\n\n";
        }


        bool hasConstructor = false;
        if (!isAbstractClass(cls)) {
            for (usz j = 0; j < cls.methods.size(); ++j) {
                if (cls.methods[j].isConstructor) {
                    hasConstructor = true;
                    break;
                }
            }
        }
        if (wasmFileName.length() > 0) {
            if (!isAbstractClass(cls) && !hasConstructor) {
                out += "  constructor();\n";
            }
            out += "  constructor(ptr: number, internal: symbol, owner?: boolean);\n";
        }
        out += "  constructor(...args: any[]) {\n";
        if (!validParentName.isEmpty()) {
            out += "    if (args.length >= 2 && args[1] === INTERNAL) {\n";
            out += "      super(args[0], INTERNAL, false);\n";
            out += "      this.ptr = args[0];\n";
            out += "      const owned = args[2] !== false;\n";
            out += "      if (owned) {\n";
            out += "        registry.register(this, { ptr: args[0], type: '" + cls.name + "' }, this);\n";
            out += "      }\n";
            out += "    } else {\n";
        } else {
            out += "    if (args.length >= 2 && args[1] === INTERNAL) {\n";
            out += "      this.ptr = args[0];\n";
            out += "      const owned = args[2] !== false;\n";
            out += "      if (owned) {\n";
            out += "        registry.register(this, { ptr: this.ptr, type: '" + cls.name + "' }, this);\n";
            out += "      }\n";
            out += "    } else {\n";
        }

        // Constructor overload dispatch
        if (isAbstractClass(cls)) {
            out += "      throw new Error('Cannot instantiate abstract class " + cls.name + "');\n";
            out += "    }\n";
            out += "  }\n\n";
        } else {
            if (!hasConstructor) {
                out += "      if (args.length === 0) {\n";
                if (!validParentName.isEmpty()) {
                    out += "        this.ptr = exports.export_" + bridgeClsName + "_new_default();\n";
                    out += "        super(ptr, INTERNAL, false);\n";
                    out += "        registry.register(this, { ptr: ptr, type: '" + cls.name + "' }, this);\n";
                } else {
                    out += "        this.ptr = exports.export_" + bridgeClsName + "_new_default();\n";
                    out += "        registry.register(this, { ptr: this.ptr, type: '" + cls.name + "' }, this);\n";
                }
                out += "        return;\n";
                out += "      }\n";
            }
            for (usz j = 0; j < cls.methods.size(); ++j) {
                const ParsedMethod& m = cls.methods[j];
                if (m.isConstructor) {
                    if (!isValidMethod(m, classes)) continue;
                    out += "      if (args.length === " + String((long long)m.params.size());

                    for (usz k = 0; k < m.params.size(); ++k) {
                        out += " && ";
                        String jsT = getJsType(m.params[k].type, classes);
                        if (jsT.startsWith("Array_")) {
                            out += "(Array.isArray(args[" + String((long long)k) + "]) || args[" + String((long long)k) + "] instanceof " + jsT + ")";
                        } else if (jsT.startsWith("Map_")) {
                            out += "( (typeof args[" + String((long long)k) + "] === 'object' && args[" + String((long long)k) + "] !== null && !(args[" + String((long long)k) + "] instanceof " + jsT + ")) || args[" + String((long long)k) + "] instanceof " + jsT + " )";
                        } else if (jsT == "string") {
                            out += "typeof args[" + String((long long)k) + "] === 'string'";
                        } else if (jsT == "boolean") {
                            out += "typeof args[" + String((long long)k) + "] === 'boolean'";
                        } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                            out += "(args[" + String((long long)k) + "] instanceof " + jsT + " || (typeof args[" + String((long long)k) + "] === 'object' && args[" + String((long long)k) + "] !== null))";
                        } else {
                            out += "typeof args[" + String((long long)k) + "] === 'number'";
                        }
                    }
                    out += ") {\n";

                    // Marshalling constructor args
                    Array<String> toFree;
                    for (usz k = 0; k < m.params.size(); ++k) {
                        String jsT = getJsType(m.params[k].type, classes);
                        if (jsT == "string") {
                            out += "        const p" + String((long long)k) + " = writeString(args[" + String((long long)k) + "]);\n";
                            toFree.push("p" + String((long long)k));
                        } else if (jsT.startsWith("Array_") || jsT.startsWith("Map_")) {
                            out += "        const p" + String((long long)k) + " = args[" + String((long long)k) + "] instanceof " + jsT + " ? args[" + String((long long)k) + "] : " + jsT + ".fromJS(args[" + String((long long)k) + "]);\n";
                            out += "        const needsFree" + String((long long)k) + " = !(args[" + String((long long)k) + "] instanceof " + jsT + ");\n";
                        } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                            out += "        const p" + String((long long)k) + " = " + jsT + ".fromJS(args[" + String((long long)k) + "]);\n";
                            out += "        const needsFree" + String((long long)k) + " = !(args[" + String((long long)k) + "] instanceof " + jsT + ");\n";
                        }
                    }

                   if (!validParentName.isEmpty()) {
                        out += "        const ptr = exports.export_" + bridgeClsName + "_new_" + String((long long)j) + "(";
                    } else {
                        out += "        this.ptr = exports.export_" + bridgeClsName + "_new_" + String((long long)j) + "(";
                    }

                    for (usz k = 0; k < m.params.size(); ++k) {
                        if (k > 0) out += ", ";
                        String jsT = getJsType(m.params[k].type, classes);
                        if (jsT == "string") {
                            out += "p" + String((long long)k);
                        } else if (jsT.startsWith("Array_") || jsT.startsWith("Map_")) {
                            out += "p" + String((long long)k) + ".ptr";
                        } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                            out += "p" + String((long long)k) + ".ptr";
                        } else {
                            String pType = cleanType(m.params[k].type);
                            if (pType == "u64" || pType == "i64" || pType == "longlong" || pType == "unsignedlonglong" || pType == "u64_t" || pType == "i64_t") {
                                out += "BigInt(args[" + String((long long)k) + "])";
                            } else {
                                out += "args[" + String((long long)k) + "]";
                            }
                        }
                    }
                    out += ");\n";

                    // Free allocated strings and templates
                    for (usz k = 0; k < m.params.size(); ++k) {
                        String jsT = getJsType(m.params[k].type, classes);
                        if (jsT == "string") {
                            out += "        exports.free_buf(p" + String((long long)k) + ");\n";
                        } else if (jsT.startsWith("Array_") || jsT.startsWith("Map_") || (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string")) {
                            out += "        if (needsFree" + String((long long)k) + ") p" + String((long long)k) + ".delete();\n";
                        }
                    }

                    if (!validParentName.isEmpty()) {
                        out += "        super(ptr, INTERNAL, false);\n";
                        out += "        registry.register(this, { ptr: ptr, type: '" + cls.name + "' }, this);\n";
                    } else {
                        out += "        registry.register(this, { ptr: this.ptr, type: '" + cls.name + "' }, this);\n";
                    }
                    out += "        return;\n";
                    out += "      }\n";
                }
            }

            out += "      throw new Error('No constructor overload matched given arguments');\n";
            out += "    }\n";
            out += "  }\n\n";
        }

        // Methods
        Map<String, Array<usz>> overloadedMethods;
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (!m.isConstructor && !m.isDestructor) {
                if (!isValidMethod(m, classes)) continue;
                Array<usz>* grp = overloadedMethods.get(m.name);
                if (grp) {
                    grp->push(j);
                } else {
                    Array<usz> newGrp;
                    newGrp.push(j);
                    overloadedMethods.set(m.name, Xi::Move(newGrp));
                }
            }
        }

        for (auto entry = overloadedMethods.begin(); entry != overloadedMethods.end(); ++entry) {
            String mName = entry->key;
            const Array<usz>& overloads = entry->value;
            const ParsedMethod& firstMethod = cls.methods[overloads[0]];

            if (firstMethod.docComment.length() > 0) {
                out += "  /**\n";
                Array<String> docLines = firstMethod.docComment.split("\n");
                for (usz j = 0; j < docLines.size(); ++j) {
                    out += "   * " + docLines[j] + "\n";
                }
                out += "   */\n";
            }

            // Method overloads
            for (usz o = 0; o < overloads.size(); ++o) {
                usz methodIdx = overloads[o];
                const ParsedMethod& m = cls.methods[methodIdx];

                Array<int> paramMap;
                Array<bool> isLenParam;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    paramMap.push(-1);
                    isLenParam.push(false);
                }
                for (usz pIdx = 0; pIdx + 1 < m.params.size(); ++pIdx) {
                    if (isStringAndLengthPattern(m.params[pIdx], m.params[pIdx + 1])) {
                        paramMap.data()[pIdx] = (int)(pIdx + 1);
                        isLenParam.data()[pIdx + 1] = true;
                    }
                }

                if (wasmFileName.length() > 0) {
                    out += "  " + mName + "(";
                    int addedParams = 0;
                    for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                        if (isLenParam[pIdx]) continue;
                        if (addedParams > 0) out += ", ";
                        String pName = sanitizeParamName(m.params[pIdx].name);
                        if (pName.isEmpty()) pName = "arg" + String((long long)addedParams);
                        out += pName + ": " + getPublicJsType(m.params[pIdx].type, classes);
                        addedParams++;
                    }
                    out += "): " + getPublicJsType(m.returnType, classes) + ";\n";
                }
            }
            out += "  " + mName + "(...args: any[]): any {\n";

            for (usz o = 0; o < overloads.size(); ++o) {
                usz methodIdx = overloads[o];
                const ParsedMethod& m = cls.methods[methodIdx];

                Array<int> paramMap;
                Array<bool> isLenParam;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    paramMap.push(-1);
                    isLenParam.push(false);
                }
                for (usz pIdx = 0; pIdx + 1 < m.params.size(); ++pIdx) {
                    if (isStringAndLengthPattern(m.params[pIdx], m.params[pIdx + 1])) {
                        paramMap.data()[pIdx] = (int)(pIdx + 1);
                        isLenParam.data()[pIdx + 1] = true;
                    }
                }

                int jsArgsCount = 0;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    if (!isLenParam[pIdx]) jsArgsCount++;
                }

                out += "    if (args.length === " + String((long long)jsArgsCount);
                int jsArgIdx = 0;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    if (isLenParam[pIdx]) continue;
                    out += " && ";
                    String jsT = getJsType(m.params[pIdx].type, classes);
                    if (jsT.startsWith("Array_")) {
                        out += "(Array.isArray(args[" + String((long long)jsArgIdx) + "]) || args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + ")";
                    } else if (jsT.startsWith("Map_")) {
                        out += "( (typeof args[" + String((long long)jsArgIdx) + "] === 'object' && args[" + String((long long)jsArgIdx) + "] !== null && !(args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + ")) || args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + " )";
                    } else if (paramMap[pIdx] >= 0 || jsT == "string") {
                        out += "typeof args[" + String((long long)jsArgIdx) + "] === 'string'";
                    } else if (jsT == "boolean") {
                        out += "typeof args[" + String((long long)jsArgIdx) + "] === 'boolean'";
                    } else if (jsT.startsWith("Func_")) {
                        out += "typeof args[" + String((long long)jsArgIdx) + "] === 'function'";
                    } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                        out += "(args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + " || (typeof args[" + String((long long)jsArgIdx) + "] === 'object' && args[" + String((long long)jsArgIdx) + "] !== null))";
                    } else {
                        out += "typeof args[" + String((long long)jsArgIdx) + "] === 'number'";
                    }
                    jsArgIdx++;
                }
                out += ") {\n";

                // Marshalling method args
                Array<String> toFree;
                jsArgIdx = 0;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    if (isLenParam[pIdx]) continue;
                    if (paramMap[pIdx] >= 0) {
                        out += "      const p" + String((long long)pIdx) + " = writeBuffer(args[" + String((long long)jsArgIdx) + "]);\n";
                        toFree.push("p" + String((long long)pIdx) + ".ptr");
                    } else {
                        String jsT = getJsType(m.params[pIdx].type, classes);
                        if (jsT == "string") {
                            out += "      const p" + String((long long)pIdx) + " = writeString(args[" + String((long long)jsArgIdx) + "]);\n";
                            toFree.push("p" + String((long long)pIdx));
                        } else if (jsT.startsWith("Array_") || jsT.startsWith("Map_")) {
                            out += "      const p" + String((long long)pIdx) + " = args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + " ? args[" + String((long long)jsArgIdx) + "] : " + jsT + ".fromJS(args[" + String((long long)jsArgIdx) + "]);\n";
                            out += "      const needsFree" + String((long long)pIdx) + " = !(args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + ");\n";
                        } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string" && jsT != "Function") {
                            out += "      const p" + String((long long)pIdx) + " = " + jsT + ".fromJS(args[" + String((long long)jsArgIdx) + "]);\n";
                            out += "      const needsFree" + String((long long)pIdx) + " = !(args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + ");\n";
                        } else if (jsT.startsWith("Func_")) {
                            out += "      let p" + String((long long)pIdx) + " = args[" + String((long long)jsArgIdx) + "];\n";
                            out += "if (typeof p" + String((long long)pIdx) + " === 'function' && !p" + String((long long)pIdx) + ".ptr) {\n";
                            out += "        p" + String((long long)pIdx) + " = new " + jsT + "(p" + String((long long)pIdx) + ");\n";
                            out += "      }\n";
                        }
                    }
                    jsArgIdx++;
                }

                // Call export
                out += "      ";
                bool returnsClass = false;
                String retClassName;
                if (m.returnType != "void") {
                    if (isClassType(m.returnType, classes, retClassName)) {
                        returnsClass = true;
                    }
                }

                TemplateInst retInst;
                bool returnsTemplate = parseTemplate(m.returnType, retInst);

                if (returnsClass || returnsTemplate || isStringType(m.returnType)) {
                    out += "const resPtr = ";
                } else if (m.returnType == "const char*" || m.returnType == "const char *" || m.returnType == "char*" || m.returnType == "char *") {
                    out += "const resPtr = ";
                } else if (m.returnType != "void") {
                    out += "const res = ";
                }

                out += "exports.export_" + bridgeClsName + "_" + m.name + "_" + String((long long)methodIdx) + "(";
                if (!m.isStatic) {
                    out += "this.ptr";
                    if (m.params.size() > 0) out += ", ";
                }

                jsArgIdx = 0;
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    if (pIdx > 0) out += ", ";
                    if (isLenParam[pIdx]) {
                        out += "p" + String((long long)(pIdx - 1)) + ".len";
                        continue;
                    }
                    if (paramMap[pIdx] >= 0) {
                        out += "p" + String((long long)pIdx) + ".ptr";
                    } else {
                        String jsT = getJsType(m.params[pIdx].type, classes);
                        if (jsT == "string") {
                            out += "p" + String((long long)pIdx);
                        } else if (jsT.startsWith("Array_") || jsT.startsWith("Map_")) {
                            out += "p" + String((long long)pIdx) + ".ptr";
                        } else if (jsT.startsWith("Func_")) {
                            out += "p" + String((long long)pIdx) + ".ptr";
                        } else if (jsT == "Function") {
                            out += "cbId";
                        } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                            out += "p" + String((long long)pIdx) + ".ptr";
                        } else {
                            String pType = cleanType(m.params[pIdx].type);
                            if (pType == "u64" || pType == "i64" || pType == "longlong" || pType == "unsignedlonglong" || pType == "u64_t" || pType == "i64_t") {
                                out += "BigInt(args[" + String((long long)jsArgIdx) + "])";
                            } else {
                                out += "args[" + String((long long)jsArgIdx) + "]";
                            }
                        }
                    }
                    jsArgIdx++;
                }
                out += ");\n";

                // Free allocated memory
                for (usz k = 0; k < toFree.size(); ++k) {
                    out += "      exports.free_buf(" + toFree[k] + ");\n";
                }
                for (usz pIdx = 0; pIdx < m.params.size(); ++pIdx) {
                    String jsT = getJsType(m.params[pIdx].type, classes);
                    if (jsT.startsWith("Array_") || jsT.startsWith("Map_") || (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string" && jsT != "Function")) {
                        out += "      if (needsFree" + String((long long)pIdx) + ") p" + String((long long)pIdx) + ".delete();\n";
                    }
                }

                // Return result — ownership: value types are owned, ref/ptr types are not
                bool retIsOwned = (m.returnType.indexOf('&') < 0 && m.returnType.indexOf('*') < 0);
                if (returnsClass) {
                    out += "      return new " + getJsName(retClassName) + "(resPtr, INTERNAL, " + (retIsOwned ? "true" : "false") + ");\n";
                } else if (returnsTemplate) {
                    out += "      const wrapper = new " + retInst.cleanName + "(resPtr, INTERNAL, " + (retIsOwned ? "true" : "false") + ");\n";
                    out += "      const jsVal = wrapper.toJS();\n";
                    if (retIsOwned) out += "      wrapper.delete();\n";
                    out += "      return jsVal;\n";
                } else if (isStringType(m.returnType)) {
                    out += "      const strVal = readString(exports.export_String_c_str(resPtr));\n";
                    out += "      exports.export_String_delete(resPtr);\n";
                    out += "      return strVal;\n";
                } else if (m.returnType == "const char*" || m.returnType == "const char *" || m.returnType == "char*" || m.returnType == "char *") {
                    out += "      return readString(resPtr);\n";
                } else if (m.returnType != "void") {
                    if (getJsType(m.returnType, classes) == "boolean") {
                        out += "      return res !== 0;\n";
                    } else {
                        out += "      return res;\n";
                    }
                } else {
                    out += "      return;\n";
                }

                out += "    }\n";
            }

            out += "    throw new Error('No method overload of \"" + mName + "\" matched given arguments');\n";
            out += "  }\n\n";
        }

        // Fields (Getters / Setters)
        for (usz j = 0; j < cls.fields.size(); ++j) {
            const ParsedField& f = cls.fields[j];
            if (!isValidType(f.type, classes)) continue;
            String jsT = getJsType(f.type, classes);

            if (f.docComment.length() > 0) {
                out += "  /**\n";
                Array<String> docLines = f.docComment.split("\n");
                for (usz k = 0; k < docLines.size(); ++k) {
                    out += "   * " + docLines[k] + "\n";
                }
                out += "   */\n";
            }

            String pubT = getPublicJsType(f.type, classes);
            out += "  get " + f.name + "(): " + pubT + " {\n";
            if (jsT.startsWith("Func_")) {
                out += "    const resPtr = exports.export_" + replaceColons(cls.name) + "_get_" + f.name + "(this.ptr);\n";
                out += "    if (resPtr === 0) return null as any;\n";
                out += "    return new " + jsT + "(resPtr, INTERNAL, false);\n";
            } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                out += "    return new " + jsT + "(exports.export_" + replaceColons(cls.name) + "_get_" + f.name + "(this.ptr), INTERNAL);\n";
            } else if (jsT == "string") {
                out += "    return readString(exports.export_" + replaceColons(cls.name) + "_get_" + f.name + "(this.ptr));\n";
            } else if (jsT == "boolean") {
                out += "    return exports.export_" + replaceColons(cls.name) + "_get_" + f.name + "(this.ptr) !== 0;\n";
            } else {
                out += "    return exports.export_" + replaceColons(cls.name) + "_get_" + f.name + "(this.ptr);\n";
            }
            out += "  }\n\n";

            if (!f.isConst) {
                out += "  set " + f.name + "(val: " + pubT + ") {\n";
                if (jsT.startsWith("Func_")) {
                    out += "    let pVal = val;\n";
                    out += "    if (typeof pVal === 'function' && !(pVal as any).ptr) {\n";
                    out += "      pVal = new " + jsT + "(pVal as any);\n";
                    out += "    }\n";
                    out += "    exports.export_" + replaceColons(cls.name) + "_set_" + f.name + "(this.ptr, (pVal as any).ptr);\n";
                } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                    out += "    exports.export_" + replaceColons(cls.name) + "_set_" + f.name + "(this.ptr, val.ptr);\n";
                } else if (jsT == "string") {
                    out += "    const ptr = writeString(val);\n";
                    out += "    exports.export_" + replaceColons(cls.name) + "_set_" + f.name + "(this.ptr, ptr);\n";
                    out += "    exports.free_buf(ptr);\n";
                } else {
                    String fType = cleanType(f.type);
                    if (fType == "u64" || fType == "i64" || fType == "longlong" || fType == "unsignedlonglong" || fType == "u64_t" || fType == "i64_t") {
                        out += "    exports.export_" + replaceColons(cls.name) + "_set_" + f.name + "(this.ptr, BigInt(val));\n";
                    } else {
                        out += "    exports.export_" + replaceColons(cls.name) + "_set_" + f.name + "(this.ptr, val);\n";
                    }
                }
                out += "  }\n\n";
            }
        }

        if (!isAbstractClass(cls)) {
            out += "  delete(): void {\n";
            out += "    if (this.ptr) {\n";
            out += "      registry.unregister(this);\n";
            out += "      exports.export_" + bridgeClsName + "_delete(this.ptr);\n";
            out += "      this.ptr = 0;\n";
            out += "    }\n";
            out += "  }\n\n";

            out += "  static fromJS(obj: any): " + jsClsName + " {\n";
            out += "    if (obj instanceof " + jsClsName + ") return obj;\n";
            out += "    const res = new " + jsClsName + "();\n";
            for (usz j = 0; j < cls.fields.size(); ++j) {
                const ParsedField& f = cls.fields[j];
                if (!isValidType(f.type, classes)) continue;
                out += "    if (obj." + f.name + " !== undefined) res." + f.name + " = obj." + f.name + ";\n";
            }
            out += "    return res;\n";
            out += "  }\n\n";
        }

        out += "}\n\n";
    }

    // Global Functions JS wraps
    Map<String, Array<usz>> overloadedGlobals;
    for (usz i = 0; i < functions.size(); ++i) {
        const ParsedFunction& fn = functions[i];
        bool fnValid = isValidType(fn.returnType, classes);
        for (usz k = 0; k < fn.params.size(); ++k) {
            if (!isValidType(fn.params[k].type, classes)) {
                fnValid = false;
                break;
            }
        }
        if (!fnValid) continue;

        String fnName = getJsName(fn.name);
        Array<usz>* grp = overloadedGlobals.get(fnName);
        if (grp) {
            grp->push(i);
        } else {
            Array<usz> newGrp;
            newGrp.push(i);
            overloadedGlobals.set(fnName, Xi::Move(newGrp));
        }
    }

    for (auto entry = overloadedGlobals.begin(); entry != overloadedGlobals.end(); ++entry) {
        String fnName = entry->key;
        const Array<usz>& overloads = entry->value;
        const ParsedFunction& firstFn = functions[overloads[0]];

        if (firstFn.docComment.length() > 0) {
            out += "/**\n";
            Array<String> docLines = firstFn.docComment.split("\n");
            for (usz j = 0; j < docLines.size(); ++j) {
                out += " * " + docLines[j] + "\n";
            }
            out += " */\n";
        }

        // Generate TS overloads
        for (usz o = 0; o < overloads.size(); ++o) {
            usz fnIdx = overloads[o];
            const ParsedFunction& fn = functions[fnIdx];

            Array<int> paramMap;
            Array<bool> isLenParam;
            for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
                paramMap.push(-1);
                isLenParam.push(false);
            }
            for (usz pIdx = 0; pIdx + 1 < fn.params.size(); ++pIdx) {
                if (isStringAndLengthPattern(fn.params[pIdx], fn.params[pIdx + 1])) {
                    paramMap.data()[pIdx] = (int)(pIdx + 1);
                    isLenParam.data()[pIdx + 1] = true;
                }
            }

            if (wasmFileName.length() > 0) {
                out += "export function " + fnName + "(";
                int addedParams = 0;
                for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
                    if (isLenParam[pIdx]) continue;
                    if (addedParams > 0) out += ", ";
                    String pName = sanitizeParamName(fn.params[pIdx].name);
                    if (pName.isEmpty()) pName = "arg" + String((long long)addedParams);
                    out += pName + ": " + getPublicJsType(fn.params[pIdx].type, classes);
                    addedParams++;
                }
                out += "): " + getPublicJsType(fn.returnType, classes) + ";\n";
            }
        }

        // Generate JS implementation
        out += "export function " + fnName + "(...args: any[]): any {\n";

        for (usz o = 0; o < overloads.size(); ++o) {
            usz fnIdx = overloads[o];
            const ParsedFunction& fn = functions[fnIdx];

            Array<int> paramMap;
            Array<bool> isLenParam;
            for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
                paramMap.push(-1);
                isLenParam.push(false);
            }
            for (usz pIdx = 0; pIdx + 1 < fn.params.size(); ++pIdx) {
                if (isStringAndLengthPattern(fn.params[pIdx], fn.params[pIdx + 1])) {
                    paramMap.data()[pIdx] = (int)(pIdx + 1);
                    isLenParam.data()[pIdx + 1] = true;
                }
            }

            int jsArgsCount = 0;
            for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
                if (!isLenParam[pIdx]) jsArgsCount++;
            }

            out += "  if (args.length === " + String((long long)jsArgsCount);
            int jsArgIdx = 0;
            for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
                if (isLenParam[pIdx]) continue;
                out += " && ";
                String jsT = getJsType(fn.params[pIdx].type, classes);
                if (jsT.startsWith("Array_")) {
                    out += "(Array.isArray(args[" + String((long long)jsArgIdx) + "]) || args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + ")";
                } else if (jsT.startsWith("Map_")) {
                    out += "( (typeof args[" + String((long long)jsArgIdx) + "] === 'object' && args[" + String((long long)jsArgIdx) + "] !== null && !(args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + ")) || args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + " )";
                } else if (paramMap[pIdx] >= 0 || jsT == "string") {
                    out += "typeof args[" + String((long long)jsArgIdx) + "] === 'string'";
                } else if (jsT == "boolean") {
                    out += "typeof args[" + String((long long)jsArgIdx) + "] === 'boolean'";
                } else if (jsT.startsWith("Func_")) {
                    out += "typeof args[" + String((long long)jsArgIdx) + "] === 'function'";
                } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                    out += "(args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + " || (typeof args[" + String((long long)jsArgIdx) + "] === 'object' && args[" + String((long long)jsArgIdx) + "] !== null))";
                } else {
                    out += "typeof args[" + String((long long)jsArgIdx) + "] === 'number'";
                }
                jsArgIdx++;
            }
            out += ") {\n";

            // Marshalling
            Array<String> toFree;
            jsArgIdx = 0;
            for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
                if (isLenParam[pIdx]) continue;
                if (paramMap[pIdx] >= 0) {
                    out += "    const p" + String((long long)pIdx) + " = writeBuffer(args[" + String((long long)jsArgIdx) + "]);\n";
                    toFree.push("p" + String((long long)pIdx) + ".ptr");
                } else {
                    String jsT = getJsType(fn.params[pIdx].type, classes);
                    if (jsT == "string") {
                        out += "    const p" + String((long long)pIdx) + " = writeString(args[" + String((long long)jsArgIdx) + "]);\n";
                        toFree.push("p" + String((long long)pIdx));
                    } else if (jsT.startsWith("Array_") || jsT.startsWith("Map_")) {
                        out += "    const p" + String((long long)pIdx) + " = args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + " ? args[" + String((long long)jsArgIdx) + "] : " + jsT + ".fromJS(args[" + String((long long)jsArgIdx) + "]);\n";
                        out += "    const needsFree" + String((long long)pIdx) + " = !(args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + ");\n";
                    } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string" && jsT != "Function") {
                        out += "    const p" + String((long long)pIdx) + " = " + jsT + ".fromJS(args[" + String((long long)jsArgIdx) + "]);\n";
                        out += "    const needsFree" + String((long long)pIdx) + " = !(args[" + String((long long)jsArgIdx) + "] instanceof " + jsT + ");\n";
                    } else if (jsT.startsWith("Func_")) {
                        out += "    let p" + String((long long)pIdx) + " = args[" + String((long long)jsArgIdx) + "];\n";
                        out += "if (typeof p" + String((long long)pIdx) + " === 'function' && !p" + String((long long)pIdx) + ".ptr) {\n";
                        out += "      p" + String((long long)pIdx) + " = new " + jsT + "(p" + String((long long)pIdx) + ");\n";
                        out += "    }\n";
                    } else if (jsT == "Function") {
                        out += "    const cbId = nextCallbackId++;\n";
                        TemplateInst pInst;
                        parseTemplate(fn.params[pIdx].type, pInst);
                        TemplateInst arg1Inst;
                        bool hasArgTemplate = parseTemplate(pInst.arg1, arg1Inst);
                        out += "    callbackRegistry.set(cbId, (argPtr: number) => {\n";
                        if (hasArgTemplate && arg1Inst.base == "Map") {
                            out += "      const wrapper = new " + arg1Inst.cleanName + "(argPtr, INTERNAL);\n";
                            out += "      const jsVal = wrapper.toJS();\n";
                            out += "      args[" + String((long long)jsArgIdx) + "](jsVal);\n";
                        } else if (hasArgTemplate && arg1Inst.base == "Array") {
                            out += "      const wrapper = new " + arg1Inst.cleanName + "(argPtr, INTERNAL);\n";
                            out += "      const jsVal = wrapper.toJS();\n";
                            out += "      args[" + String((long long)jsArgIdx) + "](jsVal);\n";
                        } else {
                            String className;
                            if (isClassType(pInst.arg1, classes, className)) {
                                out += "      const wrapper = new " + getJsName(className) + "(argPtr, INTERNAL);\n";
                                out += "      args[" + String((long long)jsArgIdx) + "](wrapper);\n";
                            } else {
                                out += "      args[" + String((long long)jsArgIdx) + "](argPtr);\n";
                            }
                        }
                        out += "    });\n";
                    }
                }
                jsArgIdx++;
            }

            // Call export
            out += "    ";
            bool returnsClass = false;
            String retClassName;
            if (fn.returnType != "void") {
                if (isClassType(fn.returnType, classes, retClassName)) {
                    returnsClass = true;
                }
            }

            TemplateInst retInst;
            bool returnsTemplate = parseTemplate(fn.returnType, retInst);

            if (returnsClass || returnsTemplate || isStringType(fn.returnType)) {
                out += "const resPtr = ";
            } else if (fn.returnType == "const char*" || fn.returnType == "const char *" || fn.returnType == "char*" || fn.returnType == "char *") {
                out += "const resPtr = ";
            } else if (fn.returnType != "void") {
                out += "const res = ";
            }

            out += "exports.export_" + replaceColons(fn.name) + "_" + String((long long)fnIdx) + "(";
            for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
                if (pIdx > 0) out += ", ";
                if (isLenParam[pIdx]) {
                    out += "p" + String((long long)(pIdx - 1)) + ".len";
                    continue;
                }
                if (paramMap[pIdx] >= 0) {
                    out += "p" + String((long long)pIdx) + ".ptr";
                } else {
                    String jsT = getJsType(fn.params[pIdx].type, classes);
                    if (jsT == "string") {
                        out += "p" + String((long long)pIdx);
                    } else if (jsT.startsWith("Array_") || jsT.startsWith("Map_")) {
                        out += "p" + String((long long)pIdx) + ".ptr";
                    } else if (jsT.startsWith("Func_")) {
                        out += "p" + String((long long)pIdx) + ".ptr";
                    } else if (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string") {
                        out += "p" + String((long long)pIdx) + ".ptr";
                    } else {
                        String pType = cleanType(fn.params[pIdx].type);
                        if (pType == "u64" || pType == "i64" || pType == "longlong" || pType == "unsignedlonglong" || pType == "u64_t" || pType == "i64_t") {
                            int foundArgIdx = 0;
                            for (usz pi = 0; pi < pIdx; ++pi) {
                                if (!isLenParam[pi]) foundArgIdx++;
                            }
                            out += "BigInt(args[" + String((long long)foundArgIdx) + "])";
                        } else {
                            int foundArgIdx = 0;
                            for (usz pi = 0; pi < pIdx; ++pi) {
                                if (!isLenParam[pi]) foundArgIdx++;
                            }
                            out += "args[" + String((long long)foundArgIdx) + "]";
                        }
                    }
                }
            }
            out += ");\n";

            for (usz k = 0; k < toFree.size(); ++k) {
                out += "    exports.free_buf(" + toFree[k] + ");\n";
            }
            for (usz pIdx = 0; pIdx < fn.params.size(); ++pIdx) {
                String jsT = getJsType(fn.params[pIdx].type, classes);
                if (jsT.startsWith("Array_") || jsT.startsWith("Map_") || (classes.size() > 0 && jsT != "number" && jsT != "void" && jsT != "boolean" && jsT != "string" && jsT != "Function")) {
                    out += "    if (needsFree" + String((long long)pIdx) + ") p" + String((long long)pIdx) + ".delete();\n";
                }
            }

            // Return result
            bool fnRetIsOwned = (fn.returnType.indexOf('&') < 0 && fn.returnType.indexOf('*') < 0);
            if (returnsClass) {
                out += "    return new " + getJsName(retClassName) + "(resPtr, INTERNAL, " + (fnRetIsOwned ? "true" : "false") + ");\n";
            } else if (returnsTemplate) {
                out += "    const wrapper = new " + retInst.cleanName + "(resPtr, INTERNAL, " + (fnRetIsOwned ? "true" : "false") + ");\n";
                out += "    const jsVal = wrapper.toJS();\n";
                if (fnRetIsOwned) out += "    wrapper.delete();\n";
                out += "    return jsVal;\n";
            } else if (isStringType(fn.returnType)) {
                out += "    const strVal = readString(exports.export_String_c_str(resPtr));\n";
                out += "    exports.export_String_delete(resPtr);\n";
                out += "    return strVal;\n";
            } else if (fn.returnType == "const char*" || fn.returnType == "const char *" || fn.returnType == "char*" || fn.returnType == "char *") {
                out += "    return readString(resPtr);\n";
            } else if (fn.returnType != "void") {
                if (getJsType(fn.returnType, classes) == "boolean") {
                    out += "    return res !== 0;\n";
                } else {
                    out += "    return res;\n";
                }
            } else {
                out += "    return;\n"; // <-- FIX: Prevent global void methods fallthrough!
            }
            out += "  }\n";
        }

        out += "  throw new Error('No overload of \"" + fnName + "\" matched given arguments');\n";
        out += "}\n\n";
    }

    return out;
}

static bool isKnownType(const String& typeName, const Array<ParsedClass>& classes) {
    String t = typeName.trim();
    long long arrowPos = t.indexOf("=>");
    if (arrowPos >= 0) {
        String ret = t.substring((usz)arrowPos + 2).trim();
        if (ret.length() > 0) {
            return true;
        }
    }
    if (t == "number" || t == "string" || t == "boolean" || t == "bigint" || t == "any" || t == "void" || t == "symbol" || t == "Function" || t == "Symbol" || t == "Uint8Array") {
        return true;
    }
    if (t == "WebAssembly.Instance" || t == "WebAssembly.Memory" || t == "WebAssembly.Exports" || t == "Promise<void>") {
        return true;
    }
    if (t.startsWith("Array_") || t.startsWith("Map_") || t.startsWith("Func_")) {
        return true;
    }
    if (t.endsWith("[]")) {
        return true;
    }
    if (t.startsWith("Record<") && t.endsWith(">")) {
        return true;
    }
    if (t.indexOf('|') >= 0) {
        Array<String> parts = t.split("|");
        bool allKnown = true;
        for (usz k = 0; k < parts.size(); ++k) {
            String p = parts[k].trim();
            if (p != "undefined" && p != "null" && !isKnownType(p, classes)) {
                allKnown = false;
                break;
            }
        }
        if (allKnown) return true;
    }
    for (usz i = 0; i < classes.size(); ++i) {
        String clsName = getJsName(classes[i].name);
        if (t == clsName || t == classes[i].name || t == replaceColons(classes[i].name)) {
            return true;
        }
    }
    return false;
}

String BindingGenerator::generateJsGlue(const Array<ParsedClass>& classes,
                                        const Array<ParsedFunction>& functions) {
    String ts = generateTsGlue(classes, functions, "");
    
    // Replace memory helper implementations
    String oldWriteString = "function writeString(str: string): number {\n"
                            "  const encoder = new TextEncoder();\n"
                            "  const bytes = encoder.encode(str);\n"
                            "  const ptr = exports.alloc_buf(bytes.length + 1);\n"
                            "  const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;\n"
                            "  const view = new Uint8Array(activeMemory, ptr, bytes.length + 1);\n"
                            "  view.set(bytes);\n"
                            "  view[bytes.length] = 0;\n"
                            "  return ptr;\n"
                            "}";
    
    String jsWriteString = "function writeString(str) {\n"
                           "  if (isNative) return exports.alloc_str(str);\n"
                           "  const encoder = new TextEncoder();\n"
                           "  const bytes = encoder.encode(str);\n"
                           "  const ptr = exports.alloc_buf(bytes.length + 1);\n"
                           "  const view = new Uint8Array(exports.memory.buffer, ptr, bytes.length + 1);\n"
                           "  view.set(bytes);\n"
                           "  view[bytes.length] = 0;\n"
                           "  return ptr;\n"
                           "}";
                           
    ts = ts.replace(oldWriteString, jsWriteString);
    
    String oldWriteBuffer = "function writeBuffer(buf: Uint8Array | string): { ptr: number, len: number } {\n"
                            "  const bytes = typeof buf === 'string' ? new TextEncoder().encode(buf) : buf;\n"
                            "  const ptr = exports.alloc_buf(bytes.length);\n"
                            "  const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;\n"
                            "  const view = new Uint8Array(activeMemory, ptr, bytes.length);\n"
                            "  view.set(bytes);\n"
                            "  return { ptr, len: bytes.length };\n"
                            "}";
                            
    String jsWriteBuffer = "function writeBuffer(buf) {\n"
                           "  const bytes = typeof buf === 'string' ? new TextEncoder().encode(buf) : buf;\n"
                           "  if (isNative) {\n"
                           "    const ptr = exports.alloc_buf(bytes.length);\n"
                           "    exports.write_buf(ptr, Array.from(bytes));\n"
                           "    return { ptr, len: bytes.length };\n"
                           "  }\n"
                           "  const ptr = exports.alloc_buf(bytes.length);\n"
                           "  const view = new Uint8Array(exports.memory.buffer, ptr, bytes.length);\n"
                           "  view.set(bytes);\n"
                           "  return { ptr, len: bytes.length };\n"
                           "}";
                           
    ts = ts.replace(oldWriteBuffer, jsWriteBuffer);
    
    String oldReadString = "function readString(ptr: number): string {\n"
                           "  const activeMemory = exports?.memory?.buffer || wasmMemory.buffer;\n"
                           "  const view = new Uint8Array(activeMemory, ptr);\n"
                           "  let len = 0;\n"
                           "  while (view[len] !== 0) len++;\n"
                           "  const bytes = new Uint8Array(activeMemory, ptr, len);\n"
                           "  return new TextDecoder().decode(bytes);\n"
                           "}";
                           
    String jsReadString = "function readString(ptr) {\n"
                          "  if (isNative) return exports.read_str(ptr);\n"
                          "  const view = new Uint8Array(exports.memory.buffer, ptr);\n"
                          "  let len = 0;\n"
                          "  while (view[len] !== 0) len++;\n"
                          "  const bytes = new Uint8Array(exports.memory.buffer, ptr, len);\n"
                          "  return new TextDecoder().decode(bytes);\n"
                          "}";
                          
    ts = ts.replace(oldReadString, jsReadString);
    
    // Replace exports initialization
    ts = ts.replace("let wasmInstance: WebAssembly.Instance;\nlet wasmMemory: WebAssembly.Memory;\nlet exports: any;",
                    "const isNative = (typeof globalThis !== 'undefined' && globalThis.__sew_native);\nlet exports = isNative ? globalThis.__sew_native : null;");
                    
    ts = ts.replace("new Map<number, Function>()", "new Map()");
    ts = ts.replace("this.get(k)!;", "this.get(k);");
                    
    // Strip body-less constructor overloads
    Array<String> lines = ts.split("\n");
    String filtered;
    for (usz i = 0; i < lines.size(); ++i) {
        String trimmed = lines[i].trim();
        if (trimmed.startsWith("constructor(") && trimmed.endsWith(";")) {
            continue;
        }
        filtered += lines[i] + "\n";
    }
    ts = filtered;
    
    // Strip property declarations
    ts = ts.replace("  ptr: number;\n", "");
    ts = ts.replace("  ptr?: number;\n", "");
    
    // Explicitly replace complex registry type annotation
    ts = ts.replace("info: { ptr: number, type: string }", "info");
    
    FILE* f_debug = fopen("/tmp/ts_debug.txt", "w");
    if (f_debug) {
        fwrite(ts.data(), 1, ts.size(), f_debug);
        fclose(f_debug);
    }
    String js;
    usz len = ts.length();
    int ternaryDepth = 0;
    bool inSlashComment = false;
    bool inBlockComment = false;
    char inString = 0;
    
    for (usz i = 0; i < len; ++i) {
        char c = ts.data()[i];
        
        // Handle comments and strings
        if (inSlashComment) {
            if (c == '\n') inSlashComment = false;
            js.push(c);
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && i + 1 < len && ts.data()[i+1] == '/') {
                inBlockComment = false;
                js.push(c);
                js.push('/');
                i++;
                continue;
            }
            js.push(c);
            continue;
        }
        if (inString) {
            if (c == inString && (i == 0 || ts.data()[i-1] != '\\')) {
                inString = 0;
            }
            js.push(c);
            continue;
        }
        
        // Check for comment/string start
        if (c == '/' && i + 1 < len && ts.data()[i+1] == '/') {
            inSlashComment = true;
            js.push(c);
            js.push('/');
            i++;
            continue;
        }
        if (c == '/' && i + 1 < len && ts.data()[i+1] == '*') {
            inBlockComment = true;
            js.push(c);
            js.push('*');
            i++;
            continue;
        }
        if (c == '\'' || c == '"' || c == '`') {
            inString = c;
            js.push(c);
            continue;
        }
        
        // Track ternary depth
        if (c == '?' && i + 1 < len && ts.data()[i+1] != '.' && ts.data()[i+1] != '?') {
            ternaryDepth++;
        }
        
        // Check for type annotation
        if (c == ':') {
            if (ternaryDepth > 0) {
                ternaryDepth--;
                js.push(':');
                continue;
            }
            
            usz j = i + 1;
            while (j < len && (ts.data()[j] == ' ' || ts.data()[j] == '\t')) {
                j++;
            }
            
            usz startType = j;
            int templateDepth = 0;
            int parenDepth = 0;
            while (j < len) {
                char nextC = ts.data()[j];
                if (nextC == '<') {
                    templateDepth++;
                    j++;
                } else if (nextC == '>') {
                    templateDepth--;
                    j++;
                } else if (nextC == '(') {
                    parenDepth++;
                    j++;
                } else if (nextC == ')') {
                    if (parenDepth > 0) {
                        parenDepth--;
                        j++;
                    } else {
                        break;
                    }
                } else if (nextC == '=' && j + 1 < len && ts.data()[j+1] == '>') {
                    j += 2;
                } else if (std::isalnum(nextC) || nextC == '_' || nextC == '$' || nextC == '[' || nextC == ']' || nextC == '|' || nextC == '?' || nextC == ' ' || nextC == '.' || nextC == '-') {
                    j++;
                } else if (nextC == ',' && (templateDepth > 0 || parenDepth > 0)) {
                    j++;
                } else if (nextC == ':' && (templateDepth > 0 || parenDepth > 0)) {
                    j++;
                } else {
                    break;
                }
            }
            if (j > startType && j < len && (ts.data()[j] == ',' || ts.data()[j] == ')' || ts.data()[j] == '{' || ts.data()[j] == ';' || ts.data()[j] == '\n' || ts.data()[j] == '=')) {
                String typeName = ts.substring(startType, j).trim();
                if (isKnownType(typeName, classes)) {
                    i = j - 1;
                    continue;
                }
            }
            js.push(':');
            continue;
        }
        
        // Check for 'as' cast
        if (ts.substring(i, i + 4) == " as ") {
            usz j = i + 4;
            int templateDepth = 0;
            while (j < len) {
                char nextC = ts.data()[j];
                if (nextC == '<') {
                    templateDepth++;
                    j++;
                } else if (nextC == '>') {
                    templateDepth--;
                    j++;
                } else if (std::isalnum(nextC) || nextC == '_' || nextC == '$' || nextC == '[' || nextC == ']' || nextC == '|' || nextC == '?' || nextC == ' ' || nextC == '.') {
                    j++;
                } else if (nextC == ',' && templateDepth > 0) {
                    j++;
                } else {
                    break;
                }
            }
            if (j > i + 4 && j < len && (ts.data()[j] == ';' || ts.data()[j] == ',' || ts.data()[j] == ')' || ts.data()[j] == '\n')) {
                String typeName = ts.substring(i + 4, j).trim();
                if (isKnownType(typeName, classes)) {
                    i = j - 1;
                    continue;
                }
            }
        }
        js.push(c);
    }
    return js;
}

String BindingGenerator::generateQuickjsBindings(const Array<ParsedClass>& classes,
                                                 const Array<ParsedFunction>& functions) {
    String out;
    out += "// Auto-generated QuickJS C bindings by Sew\n\n";
    out += "#include <quickjs.h>\n";
    out += "#include <cstdlib>\n";
    out += "#include <cstring>\n";
    out += "#include <cstdint>\n";
    out += "#include <stdint.h>\n\n";
    out += "#define JS_ToFloat64(ctx, pres, val) do { \\\n";
    out += "    if (JS_IsBigInt(ctx, val)) { \\\n";
    out += "        int64_t v = 0; \\\n";
    out += "        JS_ToBigInt64(ctx, &v, val); \\\n";
    out += "        *(pres) = (double)v; \\\n";
    out += "    } else { \\\n";
    out += "        ::JS_ToFloat64(ctx, pres, val); \\\n";
    out += "    } \\\n";
    out += "   } while(0)\n\n";

    // Extern C declarations for C++ bridge functions
    out += "extern \"C\" {\n";
    out += "  void* alloc_buf(int size);\n";
    out += "  void free_buf(void* ptr);\n";
    out += "  const char* export_String_c_str(void* s);\n";
    out += "  void export_String_delete(void* s);\n";
    
    // Discover templates
    Array<TemplateInst> templates = discoverTemplates(classes, functions);
    for (usz i = 0; i < templates.size(); ++i) {
        const TemplateInst& inst = templates[i];
        if (inst.base == "Func" && !(inst.cleanName == "Func_bool_u64_u64" || inst.cleanName == "Func_bool_u64_String" || inst.cleanName == "Func_String_u64_u64")) continue;
        
        if (inst.base == "Func") {
            out += "  void* export_" + inst.cleanName + "_new(int cbId);\n";
        } else {
            out += "  void* export_" + inst.cleanName + "_new();\n";
        }
        out += "  void export_" + inst.cleanName + "_delete(void* self);\n";
        if (inst.base != "Func") {
            out += "  int export_" + inst.cleanName + "_size(void* self);\n";
        }
        if (inst.base == "Array") {
            out += "  void export_" + inst.cleanName + "_push(void* self, void* val);\n";
            out += "  void* export_" + inst.cleanName + "_get(void* self, int index);\n";
        } else if (inst.base == "Map") {
            out += "  void export_" + inst.cleanName + "_set(void* self, void* key, void* val);\n";
            out += "  void* export_" + inst.cleanName + "_get(void* self, void* key);\n";
            out += "  void* export_" + inst.cleanName + "_keys(void* self);\n";
        }
    }
    
    // Class functions and fields declarations
    for (usz i = 0; i < classes.size(); ++i) {
        const ParsedClass& cls = classes[i];
        String cleanClsName = replaceColons(cls.name);
        out += "  void export_" + cleanClsName + "_delete(void* self);\n";
        
        bool hasConstructor = false;
        for (usz j = 0; j < cls.methods.size(); ++j) {
            if (cls.methods[j].isConstructor) {
                if (!isValidMethod(cls.methods[j], classes)) continue;
                hasConstructor = true;
                break;
            }
        }
        if (!hasConstructor) {
            out += "  void* export_" + cleanClsName + "_new_default();\n";
        }
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (m.isConstructor) {
                if (!isValidMethod(m, classes)) continue;
                out += "  void* export_" + cleanClsName + "_new_" + String((long long)j) + "(";
                for (usz p = 0; p < m.params.size(); ++p) {
                    if (p > 0) out += ", ";
                    out += "void*";
                }
                out += ");\n";
            }
        }
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (!m.isConstructor && !m.isDestructor) {
                if (!isValidMethod(m, classes)) continue;
                out += "  void* export_" + cleanClsName + "_" + m.name + "_" + String((long long)j) + "(void* self";
                for (usz p = 0; p < m.params.size(); ++p) {
                    out += ", void*";
                }
                out += ");\n";
            }
        }
        for (usz j = 0; j < cls.fields.size(); ++j) {
            const ParsedField& f = cls.fields[j];
            if (!isValidType(f.type, classes)) continue;
            out += "  void* export_" + cleanClsName + "_get_" + f.name + "(void* self);\n";
            if (!f.isConst) {
                out += "  void export_" + cleanClsName + "_set_" + f.name + "(void* self, void* val);\n";
            }
        }
    }
    
    // Global functions declarations
    for (usz i = 0; i < functions.size(); ++i) {
        const ParsedFunction& fn = functions[i];
        bool fnValid = isValidType(fn.returnType, classes);
        for (usz k = 0; k < fn.params.size(); ++k) {
            if (!isValidType(fn.params[k].type, classes)) { fnValid = false; break; }
        }
        if (!fnValid) continue;
        out += "  void* export_" + replaceColons(fn.name) + "_" + String((long long)i) + "(";
        for (usz p = 0; p < fn.params.size(); ++p) {
            if (p > 0) out += ", ";
            out += "void*";
        }
        out += ");\n";
    }
    out += "}\n\n";

    // Memory helper wrappers
    out += "static JSValue js_alloc_str(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
    out += "  const char *str = JS_ToCString(ctx, argv[0]);\n";
    out += "  if (!str) return JS_NULL;\n";
    out += "  size_t len = strlen(str);\n";
    out += "  void *ptr = malloc(len + 1);\n";
    out += "  memcpy(ptr, str, len + 1);\n";
    out += "  JS_FreeCString(ctx, str);\n";
    out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)ptr);\n";
    out += "}\n\n";

    out += "static JSValue js_read_str(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
    out += "  double d = 0.0;\n";
    out += "  JS_ToFloat64(ctx, &d, argv[0]);\n";
    out += "  const char *str = (const char*)(uintptr_t)d;\n";
    out += "  if (!str) return JS_NULL;\n";
    out += "  return JS_NewString(ctx, str);\n";
    out += "}\n\n";

    out += "static JSValue js_write_buf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
    out += "  double d = 0.0;\n";
    out += "  JS_ToFloat64(ctx, &d, argv[0]);\n";
    out += "  uint8_t *ptr = (uint8_t*)(uintptr_t)d;\n";
    out += "  if (!ptr) return JS_UNDEFINED;\n";
    out += "  JSValue arr = argv[1];\n";
    out += "  int32_t len = 0;\n";
    out += "  JSValue lenVal = JS_GetPropertyStr(ctx, arr, \"length\");\n";
    out += "  JS_ToInt32(ctx, &len, lenVal);\n";
    out += "  JS_FreeValue(ctx, lenVal);\n";
    out += "  for (int i = 0; i < len; ++i) {\n";
    out += "    JSValue val = JS_GetPropertyUint32(ctx, arr, i);\n";
    out += "    int32_t b = 0;\n";
    out += "    JS_ToInt32(ctx, &b, val);\n";
    out += "    ptr[i] = (uint8_t)b;\n";
    out += "    JS_FreeValue(ctx, val);\n";
    out += "  }\n";
    out += "  return JS_UNDEFINED;\n";
    out += "}\n\n";

    out += "static JSValue js_read_buf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
    out += "  double d = 0.0;\n";
    out += "  JS_ToFloat64(ctx, &d, argv[0]);\n";
    out += "  int32_t len = 0;\n";
    out += "  JS_ToInt32(ctx, &len, argv[1]);\n";
    out += "  const uint8_t *ptr = (const uint8_t*)(uintptr_t)d;\n";
    out += "  if (!ptr || len <= 0) return JS_NULL;\n";
    out += "  JSValue arr = JS_NewArray(ctx);\n";
    out += "  for (int i = 0; i < len; ++i) {\n";
    out += "    JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, ptr[i]));\n";
    out += "  }\n";
    out += "  return arr;\n";
    out += "}\n\n";

    // Flat wrappers for allocator
    out += "static JSValue js_alloc_buf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
    out += "  int32_t size = 0;\n";
    out += "  JS_ToInt32(ctx, &size, argv[0]);\n";
    out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)alloc_buf(size));\n";
    out += "}\n\n";

    out += "static JSValue js_free_buf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
    out += "  double d = 0.0;\n";
    out += "  JS_ToFloat64(ctx, &d, argv[0]);\n";
    out += "  free_buf((void*)(uintptr_t)d);\n";
    out += "  return JS_UNDEFINED;\n";
    out += "}\n\n";

    out += "static JSValue js_export_String_c_str(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
    out += "  double d = 0.0;\n";
    out += "  JS_ToFloat64(ctx, &d, argv[0]);\n";
    out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_String_c_str((void*)(uintptr_t)d));\n";
    out += "}\n\n";

    out += "static JSValue js_export_String_delete(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
    out += "  double d = 0.0;\n";
    out += "  JS_ToFloat64(ctx, &d, argv[0]);\n";
    out += "  export_String_delete((void*)(uintptr_t)d);\n";
    out += "  return JS_UNDEFINED;\n";
    out += "}\n\n";

    // Wrappers for Templates
    for (usz i = 0; i < templates.size(); ++i) {
        const TemplateInst& inst = templates[i];
        if (inst.base == "Func" && !(inst.cleanName == "Func_bool_u64_u64" || inst.cleanName == "Func_bool_u64_String" || inst.cleanName == "Func_String_u64_u64")) continue;
        
        if (inst.base == "Func") {
            out += "static JSValue js_export_" + inst.cleanName + "_new(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  int32_t cbId = 0;\n";
            out += "  JS_ToInt32(ctx, &cbId, argv[0]);\n";
            out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + inst.cleanName + "_new(cbId));\n";
            out += "}\n\n";
        } else {
            out += "static JSValue js_export_" + inst.cleanName + "_new(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + inst.cleanName + "_new());\n";
            out += "}\n\n";
        }

        out += "static JSValue js_export_" + inst.cleanName + "_delete(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
        out += "  double d = 0.0;\n";
        out += "  JS_ToFloat64(ctx, &d, argv[0]);\n";
        out += "  export_" + inst.cleanName + "_delete((void*)(uintptr_t)d);\n";
        out += "  return JS_UNDEFINED;\n";
        out += "}\n\n";

        if (inst.base != "Func") {
            out += "static JSValue js_export_" + inst.cleanName + "_size(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  double d = 0.0;\n";
            out += "  JS_ToFloat64(ctx, &d, argv[0]);\n";
            out += "  return JS_NewInt32(ctx, export_" + inst.cleanName + "_size((void*)(uintptr_t)d));\n";
            out += "}\n\n";
        }

        if (inst.base == "Array") {
            out += "static JSValue js_export_" + inst.cleanName + "_push(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  double d_self = 0.0; double d_val = 0.0;\n";
            out += "  JS_ToFloat64(ctx, &d_self, argv[0]);\n";
            out += "  JS_ToFloat64(ctx, &d_val, argv[1]);\n";
            out += "  export_" + inst.cleanName + "_push((void*)(uintptr_t)d_self, (void*)(uintptr_t)d_val);\n";
            out += "  return JS_UNDEFINED;\n";
            out += "}\n\n";

            out += "static JSValue js_export_" + inst.cleanName + "_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  double d_self = 0.0; int32_t idx = 0;\n";
            out += "  JS_ToFloat64(ctx, &d_self, argv[0]);\n";
            out += "  JS_ToInt32(ctx, &idx, argv[1]);\n";
            out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + inst.cleanName + "_get((void*)(uintptr_t)d_self, idx));\n";
            out += "}\n\n";
        } else if (inst.base == "Map") {
            out += "static JSValue js_export_" + inst.cleanName + "_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  double d_self = 0.0; double d_key = 0.0; double d_val = 0.0;\n";
            out += "  JS_ToFloat64(ctx, &d_self, argv[0]);\n";
            out += "  JS_ToFloat64(ctx, &d_key, argv[1]);\n";
            out += "  JS_ToFloat64(ctx, &d_val, argv[2]);\n";
            out += "  export_" + inst.cleanName + "_set((void*)(uintptr_t)d_self, (void*)(uintptr_t)d_key, (void*)(uintptr_t)d_val);\n";
            out += "  return JS_UNDEFINED;\n";
            out += "}\n\n";

            out += "static JSValue js_export_" + inst.cleanName + "_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  double d_self = 0.0; double d_key = 0.0;\n";
            out += "  JS_ToFloat64(ctx, &d_self, argv[0]);\n";
            out += "  JS_ToFloat64(ctx, &d_key, argv[1]);\n";
            out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + inst.cleanName + "_get((void*)(uintptr_t)d_self, (void*)(uintptr_t)d_key));\n";
            out += "}\n\n";

            out += "static JSValue js_export_" + inst.cleanName + "_keys(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  double d_self = 0.0;\n";
            out += "  JS_ToFloat64(ctx, &d_self, argv[0]);\n";
            out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + inst.cleanName + "_keys((void*)(uintptr_t)d_self));\n";
            out += "}\n\n";
        }
    }

    // Wrappers for Classes (Methods & Fields)
    for (usz i = 0; i < classes.size(); ++i) {
        const ParsedClass& cls = classes[i];
        String cleanClsName = replaceColons(cls.name);

        out += "static JSValue js_export_" + cleanClsName + "_delete(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
        out += "  double d = 0.0;\n";
        out += "  JS_ToFloat64(ctx, &d, argv[0]);\n";
        out += "  export_" + cleanClsName + "_delete((void*)(uintptr_t)d);\n";
        out += "  return JS_UNDEFINED;\n";
        out += "}\n\n";

        bool hasConstructor = false;
        for (usz j = 0; j < cls.methods.size(); ++j) {
            if (cls.methods[j].isConstructor) {
                if (!isValidMethod(cls.methods[j], classes)) continue;
                hasConstructor = true;
                break;
            }
        }
        if (!hasConstructor) {
            out += "static JSValue js_export_" + cleanClsName + "_new_default(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + cleanClsName + "_new_default());\n";
            out += "}\n\n";
        }

        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (m.isConstructor) {
                if (!isValidMethod(m, classes)) continue;
                out += "static JSValue js_export_" + cleanClsName + "_new_" + String((long long)j) + "(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
                for (usz p = 0; p < m.params.size(); ++p) {
                    out += "  double d" + String((long long)p) + " = 0.0;\n";
                    out += "  JS_ToFloat64(ctx, &d" + String((long long)p) + ", argv[" + String((long long)p) + "]);\n";
                }
                out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + cleanClsName + "_new_" + String((long long)j) + "(";
                for (usz p = 0; p < m.params.size(); ++p) {
                    if (p > 0) out += ", ";
                    out += "(void*)(uintptr_t)d" + String((long long)p);
                }
                out += "));\n";
                out += "}\n\n";
            }
        }

        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (!m.isConstructor && !m.isDestructor) {
                if (!isValidMethod(m, classes)) continue;
                out += "static JSValue js_export_" + cleanClsName + "_" + m.name + "_" + String((long long)j) + "(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
                out += "  double d_self = 0.0;\n";
                out += "  JS_ToFloat64(ctx, &d_self, argv[0]);\n";
                for (usz p = 0; p < m.params.size(); ++p) {
                    out += "  double d" + String((long long)p) + " = 0.0;\n";
                    out += "  JS_ToFloat64(ctx, &d" + String((long long)p) + ", argv[" + String((long long)(p + 1)) + "]);\n";
                }
                out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + cleanClsName + "_" + m.name + "_" + String((long long)j) + "((void*)(uintptr_t)d_self";
                for (usz p = 0; p < m.params.size(); ++p) {
                    out += ", (void*)(uintptr_t)d" + String((long long)p);
                }
                out += "));\n";
                out += "}\n\n";
            }
        }

        for (usz j = 0; j < cls.fields.size(); ++j) {
            const ParsedField& f = cls.fields[j];
            if (!isValidType(f.type, classes)) continue;
            out += "static JSValue js_export_" + cleanClsName + "_get_" + f.name + "(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
            out += "  double d_self = 0.0;\n";
            out += "  JS_ToFloat64(ctx, &d_self, argv[0]);\n";
            out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + cleanClsName + "_get_" + f.name + "((void*)(uintptr_t)d_self));\n";
            out += "}\n\n";

            if (!f.isConst) {
                out += "static JSValue js_export_" + cleanClsName + "_set_" + f.name + "(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
                out += "  double d_self = 0.0; double d_val = 0.0;\n";
                out += "  JS_ToFloat64(ctx, &d_self, argv[0]);\n";
                out += "  JS_ToFloat64(ctx, &d_val, argv[1]);\n";
                out += "  export_" + cleanClsName + "_set_" + f.name + "((void*)(uintptr_t)d_self, (void*)(uintptr_t)d_val);\n";
                out += "  return JS_UNDEFINED;\n";
                out += "}\n\n";
            }
        }
    }

    for (usz i = 0; i < functions.size(); ++i) {
        const ParsedFunction& fn = functions[i];
        bool fnValid = isValidType(fn.returnType, classes);
        for (usz k = 0; k < fn.params.size(); ++k) {
            if (!isValidType(fn.params[k].type, classes)) { fnValid = false; break; }
        }
        if (!fnValid) continue;
        out += "static JSValue js_export_" + replaceColons(fn.name) + "_" + String((long long)i) + "(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
        for (usz p = 0; p < fn.params.size(); ++p) {
            out += "  double d" + String((long long)p) + " = 0.0;\n";
            out += "  JS_ToFloat64(ctx, &d" + String((long long)p) + ", argv[" + String((long long)p) + "]);\n";
        }
        out += "  return JS_NewFloat64(ctx, (double)(uintptr_t)export_" + replaceColons(fn.name) + "_" + String((long long)i) + "(";
        for (usz p = 0; p < fn.params.size(); ++p) {
            if (p > 0) out += ", ";
            out += "(void*)(uintptr_t)d" + String((long long)p);
        }
        out += "));\n";
        out += "}\n\n";
    }

    // Callback support
    out += "extern \"C\" {\n";
    out += "  static JSContext* global_js_ctx = nullptr;\n";
    out += "  void call_js_callback(int cbId, void* argPtr) {\n";
    out += "    if (!global_js_ctx) return;\n";
    out += "    JSContext* ctx = global_js_ctx;\n";
    out += "    JSValue global = JS_GetGlobalObject(ctx);\n";
    out += "    JSValue cbFunc = JS_GetPropertyStr(ctx, global, \"call_js_callback\");\n";
    out += "    if (JS_IsFunction(ctx, cbFunc)) {\n";
    out += "      JSValue args[2];\n";
    out += "      args[0] = JS_NewInt32(ctx, cbId);\n";
    out += "      args[1] = JS_NewFloat64(ctx, (double)(uintptr_t)argPtr);\n";
    out += "      JSValue res = JS_Call(ctx, cbFunc, global, 2, args);\n";
    out += "      JS_FreeValue(ctx, res);\n";
    out += "      JS_FreeValue(ctx, args[0]);\n";
    out += "      JS_FreeValue(ctx, args[1]);\n";
    out += "    }\n";
    out += "    JS_FreeValue(ctx, cbFunc);\n";
    out += "    JS_FreeValue(ctx, global);\n";
    out += "  }\n";
    out += "  bool call_js_callback_bool(int cbId, void* argPtr) {\n";
    out += "    if (!global_js_ctx) return false;\n";
    out += "    JSContext* ctx = global_js_ctx;\n";
    out += "    JSValue global = JS_GetGlobalObject(ctx);\n";
    out += "    JSValue cbFunc = JS_GetPropertyStr(ctx, global, \"call_js_callback_bool\");\n";
    out += "    bool ret = false;\n";
    out += "    if (JS_IsFunction(ctx, cbFunc)) {\n";
    out += "      JSValue args[2];\n";
    out += "      args[0] = JS_NewInt32(ctx, cbId);\n";
    out += "      args[1] = JS_NewFloat64(ctx, (double)(uintptr_t)argPtr);\n";
    out += "      JSValue res = JS_Call(ctx, cbFunc, global, 2, args);\n";
    out += "      ret = JS_ToBool(ctx, res) != 0;\n";
    out += "      JS_FreeValue(ctx, res);\n";
    out += "      JS_FreeValue(ctx, args[0]);\n";
    out += "      JS_FreeValue(ctx, args[1]);\n";
    out += "    }\n";
    out += "    JS_FreeValue(ctx, cbFunc);\n";
    out += "    JS_FreeValue(ctx, global);\n";
    out += "    return ret;\n";
    out += "  }\n";
    out += "  void* call_js_callback_ptr(int cbId, void* argPtr) {\n";
    out += "    if (!global_js_ctx) return nullptr;\n";
    out += "    JSContext* ctx = global_js_ctx;\n";
    out += "    JSValue global = JS_GetGlobalObject(ctx);\n";
    out += "    JSValue cbFunc = JS_GetPropertyStr(ctx, global, \"call_js_callback_ptr\");\n";
    out += "    void* ret = nullptr;\n";
    out += "    if (JS_IsFunction(ctx, cbFunc)) {\n";
    out += "      JSValue args[2];\n";
    out += "      args[0] = JS_NewInt32(ctx, cbId);\n";
    out += "      args[1] = JS_NewFloat64(ctx, (double)(uintptr_t)argPtr);\n";
    out += "      JSValue res = JS_Call(ctx, cbFunc, global, 2, args);\n";
    out += "      double d = 0.0;\n";
    out += "      JS_ToFloat64(ctx, &d, res);\n";
    out += "      ret = (void*)(uintptr_t)d;\n";
    out += "      JS_FreeValue(ctx, res);\n";
    out += "      JS_FreeValue(ctx, args[0]);\n";
    out += "      JS_FreeValue(ctx, args[1]);\n";
    out += "    }\n";
    out += "    JS_FreeValue(ctx, cbFunc);\n";
    out += "    JS_FreeValue(ctx, global);\n";
    out += "    return ret;\n";
    out += "  }\n";
    out += "}\n\n";

    // Registration Function

    out += R"(
    
    // Forward declarations to avoid external header dependencies
struct GLFWwindow;
struct GLFWmonitor;

extern "C" {
    int glfwInit(void);
    void glfwTerminate(void);
    void glfwWindowHint(int hint, int value);
    void glfwDefaultWindowHints(void);
    GLFWwindow* glfwCreateWindow(int width, int height, const char* title, GLFWmonitor* monitor, GLFWwindow* share);
    int glfwWindowShouldClose(GLFWwindow* window);
    void glfwSetWindowShouldClose(GLFWwindow* window, int value);
    void glfwDestroyWindow(GLFWwindow* window);
    void glfwPollEvents(void);
    void glfwWaitEvents(void);
    void glfwMakeContextCurrent(GLFWwindow* window);
    void glfwSwapBuffers(GLFWwindow* window);
    double glfwGetTime(void);
    void glfwSetTime(double time);
}
    
static JSValue js_glfwInit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return JS_NewInt32(ctx, glfwInit());
}
static JSValue js_glfwTerminate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  glfwTerminate();
  return JS_UNDEFINED;
}
static JSValue js_glfwWindowHint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  int32_t hint = 0, value = 0;
  JS_ToInt32(ctx, &hint, argv[0]);
  JS_ToInt32(ctx, &value, argv[1]);
  glfwWindowHint(hint, value);
  return JS_UNDEFINED;
}
static JSValue js_glfwDefaultWindowHints(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  glfwDefaultWindowHints();
  return JS_UNDEFINED;
}
static JSValue js_glfwCreateWindow(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  int32_t w = 0, h = 0;
  JS_ToInt32(ctx, &w, argv[0]);
  JS_ToInt32(ctx, &h, argv[1]);
  const char* title = JS_ToCString(ctx, argv[2]);
  GLFWwindow* win = glfwCreateWindow(w, h, title, NULL, NULL);
  JS_FreeCString(ctx, title);
  return JS_NewFloat64(ctx, (double)(uintptr_t)win);
}
static JSValue js_glfwWindowShouldClose(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  double d = 0.0;
  JS_ToFloat64(ctx, &d, argv[0]);
  return JS_NewInt32(ctx, glfwWindowShouldClose((GLFWwindow*)(uintptr_t)d));
}
static JSValue js_glfwSetWindowShouldClose(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  double d = 0.0; int32_t val = 0;
  JS_ToFloat64(ctx, &d, argv[0]);
  JS_ToInt32(ctx, &val, argv[1]);
  glfwSetWindowShouldClose((GLFWwindow*)(uintptr_t)d, val);
  return JS_UNDEFINED;
}
static JSValue js_glfwDestroyWindow(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  double d = 0.0;
  JS_ToFloat64(ctx, &d, argv[0]);
  glfwDestroyWindow((GLFWwindow*)(uintptr_t)d);
  return JS_UNDEFINED;
}
static JSValue js_glfwPollEvents(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  glfwPollEvents();
  return JS_UNDEFINED;
}
static JSValue js_glfwWaitEvents(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  glfwWaitEvents();
  return JS_UNDEFINED;
}
static JSValue js_glfwMakeContextCurrent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  double d = 0.0;
  JS_ToFloat64(ctx, &d, argv[0]);
  glfwMakeContextCurrent((GLFWwindow*)(uintptr_t)d);
  return JS_UNDEFINED;
}
static JSValue js_glfwSwapBuffers(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  double d = 0.0;
  JS_ToFloat64(ctx, &d, argv[0]);
  glfwSwapBuffers((GLFWwindow*)(uintptr_t)d);
  return JS_UNDEFINED;
}
static JSValue js_glfwGetTime(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return JS_NewFloat64(ctx, glfwGetTime());
}
static JSValue js_glfwSetTime(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  double t = 0.0;
  JS_ToFloat64(ctx, &t, argv[0]);
  glfwSetTime(t);
  return JS_UNDEFINED;
}
    )";

    out += "extern \"C\" void register_sew_native_bindings(JSContext *ctx, JSValue native_obj) {\n";
    out += "  global_js_ctx = ctx;\n";
    out += "  JS_SetPropertyStr(ctx, native_obj, \"alloc_buf\", JS_NewCFunction(ctx, js_alloc_buf, \"alloc_buf\", 1));\n";
    out += "  JS_SetPropertyStr(ctx, native_obj, \"free_buf\", JS_NewCFunction(ctx, js_free_buf, \"free_buf\", 1));\n";
    out += "  JS_SetPropertyStr(ctx, native_obj, \"export_String_c_str\", JS_NewCFunction(ctx, js_export_String_c_str, \"export_String_c_str\", 1));\n";
    out += "  JS_SetPropertyStr(ctx, native_obj, \"export_String_delete\", JS_NewCFunction(ctx, js_export_String_delete, \"export_String_delete\", 1));\n";
    out += "  JS_SetPropertyStr(ctx, native_obj, \"alloc_str\", JS_NewCFunction(ctx, js_alloc_str, \"alloc_str\", 1));\n";
    out += "  JS_SetPropertyStr(ctx, native_obj, \"read_str\", JS_NewCFunction(ctx, js_read_str, \"read_str\", 1));\n";
    out += "  JS_SetPropertyStr(ctx, native_obj, \"write_buf\", JS_NewCFunction(ctx, js_write_buf, \"write_buf\", 2));\n";
    out += "  JS_SetPropertyStr(ctx, native_obj, \"read_buf\", JS_NewCFunction(ctx, js_read_buf, \"read_buf\", 2));\n";

    out += R"(
    
    JS_SetPropertyStr(ctx, native_obj, "glfwInit", JS_NewCFunction(ctx, js_glfwInit, "glfwInit", 0));
  JS_SetPropertyStr(ctx, native_obj, "glfwTerminate", JS_NewCFunction(ctx, js_glfwTerminate, "glfwTerminate", 0));
  JS_SetPropertyStr(ctx, native_obj, "glfwWindowHint", JS_NewCFunction(ctx, js_glfwWindowHint, "glfwWindowHint", 2));
  JS_SetPropertyStr(ctx, native_obj, "glfwDefaultWindowHints", JS_NewCFunction(ctx, js_glfwDefaultWindowHints, "glfwDefaultWindowHints", 0));
  JS_SetPropertyStr(ctx, native_obj, "glfwCreateWindow", JS_NewCFunction(ctx, js_glfwCreateWindow, "glfwCreateWindow", 5));
  JS_SetPropertyStr(ctx, native_obj, "glfwWindowShouldClose", JS_NewCFunction(ctx, js_glfwWindowShouldClose, "glfwWindowShouldClose", 1));
  JS_SetPropertyStr(ctx, native_obj, "glfwSetWindowShouldClose", JS_NewCFunction(ctx, js_glfwSetWindowShouldClose, "glfwSetWindowShouldClose", 2));
  JS_SetPropertyStr(ctx, native_obj, "glfwDestroyWindow", JS_NewCFunction(ctx, js_glfwDestroyWindow, "glfwDestroyWindow", 1));
  JS_SetPropertyStr(ctx, native_obj, "glfwPollEvents", JS_NewCFunction(ctx, js_glfwPollEvents, "glfwPollEvents", 0));
  JS_SetPropertyStr(ctx, native_obj, "glfwWaitEvents", JS_NewCFunction(ctx, js_glfwWaitEvents, "glfwWaitEvents", 0));
  JS_SetPropertyStr(ctx, native_obj, "glfwMakeContextCurrent", JS_NewCFunction(ctx, js_glfwMakeContextCurrent, "glfwMakeContextCurrent", 1));
  JS_SetPropertyStr(ctx, native_obj, "glfwSwapBuffers", JS_NewCFunction(ctx, js_glfwSwapBuffers, "glfwSwapBuffers", 1));
  JS_SetPropertyStr(ctx, native_obj, "glfwGetTime", JS_NewCFunction(ctx, js_glfwGetTime, "glfwGetTime", 0));
  JS_SetPropertyStr(ctx, native_obj, "glfwSetTime", JS_NewCFunction(ctx, js_glfwSetTime, "glfwSetTime", 1));
    
    )";

    for (usz i = 0; i < templates.size(); ++i) {
        const TemplateInst& inst = templates[i];
        if (inst.base == "Func" && !(inst.cleanName == "Func_bool_u64_u64" || inst.cleanName == "Func_bool_u64_String" || inst.cleanName == "Func_String_u64_u64")) continue;
        
        if (inst.base == "Func") {
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + inst.cleanName + "_new\", JS_NewCFunction(ctx, js_export_" + inst.cleanName + "_new, \"export_" + inst.cleanName + "_new\", 1));\n";
        } else {
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + inst.cleanName + "_new\", JS_NewCFunction(ctx, js_export_" + inst.cleanName + "_new, \"export_" + inst.cleanName + "_new\", 0));\n";
        }
        out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + inst.cleanName + "_delete\", JS_NewCFunction(ctx, js_export_" + inst.cleanName + "_delete, \"export_" + inst.cleanName + "_delete\", 1));\n";
        if (inst.base != "Func") {
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + inst.cleanName + "_size\", JS_NewCFunction(ctx, js_export_" + inst.cleanName + "_size, \"export_" + inst.cleanName + "_size\", 1));\n";
        }
        if (inst.base == "Array") {
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + inst.cleanName + "_push\", JS_NewCFunction(ctx, js_export_" + inst.cleanName + "_push, \"export_" + inst.cleanName + "_push\", 2));\n";
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + inst.cleanName + "_get\", JS_NewCFunction(ctx, js_export_" + inst.cleanName + "_get, \"export_" + inst.cleanName + "_get\", 2));\n";
        } else if (inst.base == "Map") {
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + inst.cleanName + "_set\", JS_NewCFunction(ctx, js_export_" + inst.cleanName + "_set, \"export_" + inst.cleanName + "_set\", 3));\n";
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + inst.cleanName + "_get\", JS_NewCFunction(ctx, js_export_" + inst.cleanName + "_get, \"export_" + inst.cleanName + "_get\", 2));\n";
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + inst.cleanName + "_keys\", JS_NewCFunction(ctx, js_export_" + inst.cleanName + "_keys, \"export_" + inst.cleanName + "_keys\", 1));\n";
        }
    }

    for (usz i = 0; i < classes.size(); ++i) {
        const ParsedClass& cls = classes[i];
        String cleanClsName = replaceColons(cls.name);
        out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + cleanClsName + "_delete\", JS_NewCFunction(ctx, js_export_" + cleanClsName + "_delete, \"export_" + cleanClsName + "_delete\", 1));\n";
        
        bool hasConstructor = false;
        for (usz j = 0; j < cls.methods.size(); ++j) {
            if (cls.methods[j].isConstructor) {
                if (!isValidMethod(cls.methods[j], classes)) continue;
                hasConstructor = true;
                break;
            }
        }
        if (!hasConstructor) {
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + cleanClsName + "_new_default\", JS_NewCFunction(ctx, js_export_" + cleanClsName + "_new_default, \"export_" + cleanClsName + "_new_default\", 0));\n";
        }
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (m.isConstructor) {
                if (!isValidMethod(m, classes)) continue;
                out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + cleanClsName + "_new_" + String((long long)j) + "\", JS_NewCFunction(ctx, js_export_" + cleanClsName + "_new_" + String((long long)j) + ", \"export_" + cleanClsName + "_new\", " + String((long long)m.params.size()) + "));\n";
            }
        }
        for (usz j = 0; j < cls.methods.size(); ++j) {
            const ParsedMethod& m = cls.methods[j];
            if (!m.isConstructor && !m.isDestructor) {
                if (!isValidMethod(m, classes)) continue;
                out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + cleanClsName + "_" + m.name + "_" + String((long long)j) + "\", JS_NewCFunction(ctx, js_export_" + cleanClsName + "_" + m.name + "_" + String((long long)j) + ", \"export_" + cleanClsName + "_" + m.name + "_" + String((long long)j) + "\", " + String((long long)(m.params.size() + 1)) + "));\n";
            }
        }
        for (usz j = 0; j < cls.fields.size(); ++j) {
            const ParsedField& f = cls.fields[j];
            if (!isValidType(f.type, classes)) continue;
            out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + cleanClsName + "_get_" + f.name + "\", JS_NewCFunction(ctx, js_export_" + cleanClsName + "_get_" + f.name + ", \"export_" + cleanClsName + "_get_" + f.name + "\", 1));\n";
            if (!f.isConst) {
                out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + cleanClsName + "_set_" + f.name + "\", JS_NewCFunction(ctx, js_export_" + cleanClsName + "_set_" + f.name + ", \"export_" + cleanClsName + "_set_" + f.name + "\", 2));\n";
            }
        }
    }

    for (usz i = 0; i < functions.size(); ++i) {
        const ParsedFunction& fn = functions[i];
        bool fnValid = isValidType(fn.returnType, classes);
        for (usz k = 0; k < fn.params.size(); ++k) {
            if (!isValidType(fn.params[k].type, classes)) { fnValid = false; break; }
        }
        if (!fnValid) continue;
        out += "  JS_SetPropertyStr(ctx, native_obj, \"export_" + replaceColons(fn.name) + "_" + String((long long)i) + "\", JS_NewCFunction(ctx, js_export_" + replaceColons(fn.name) + "_" + String((long long)i) + ", \"export_" + replaceColons(fn.name) + "_" + String((long long)i) + "\", " + String((long long)fn.params.size()) + "));\n";
    }
    out += "}\n";
    return out;
}

} 
