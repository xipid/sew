/**
 * @file CppLanguage.cpp
 * @brief C++ language plugin implementation.
 */

#include <System/Process.hpp>
#include <Languages/CPP/CppLanguage.hpp>

#include <sys/stat.h>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <cstring>

#include <Sew/Parser.hpp>
#include <Reflection/Type.hpp>

namespace Sew {
namespace Languages {

static String getSewIncludePath() {
    char path[1024];
    ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        char* lastSlash = ::strrchr(path, '/');
        if (lastSlash) {
            *lastSlash = '\0';
            return String(path) + "/../include";
        }
    }
    return "include";
}

static String getWasiSdkDir() {
    const char* home = ::getenv("HOME");
    if (!home) return "";
    return String(home) + "/.cache/sew/wasi-sdk";
}

static bool ensureWasiSdk() {
    String sdkDir = getWasiSdkDir();
    if (sdkDir.isEmpty()) return false;

    String clangPath = sdkDir + "/bin/clang++";
    struct stat st;
    if (::stat(clangPath.c_str(), &st) == 0) {
        return true; // Already installed!
    }

    fprintf(stderr, "WASI SDK toolchain not found at %s. Please run sew CLI to install it.\n", sdkDir.c_str());
    return false;
}

using namespace System;

static String getKindString(const String& typeStr) {
    if (typeStr == "int" || typeStr == "int32_t" || typeStr == "int64_t" || typeStr == "unsigned" || typeStr == "size_t" || typeStr == "usz") {
        return "TypeKind::Int";
    } else if (typeStr == "float" || typeStr == "double") {
        return "TypeKind::Float";
    } else if (typeStr == "String" || typeStr == "Collection::String") {
        return "TypeKind::String";
    } else if (typeStr.endsWith("*")) {
        return "TypeKind::Pointer";
    } else {
        return "TypeKind::Custom";
    }
}

static String getCustomTypeName(const String& typeStr) {
    if (typeStr.endsWith("*")) {
        return typeStr.substring(0, typeStr.length() - 1).trim();
    }
    return typeStr;
}

static String sanitizeName(const String& name) {
    return name.replace("::", "_");
}

static String stripReference(const String& typeStr) {
    String t = typeStr;
    if (t.endsWith("&")) {
        t = t.substring(0, t.length() - 1).trim();
    }
    return t;
}

static bool isClassType(const String& typeStr, const Array<ParsedClass>& classes, String& outClassName) {
    String clean = typeStr;
    if (clean.endsWith("&")) clean = clean.substring(0, clean.length() - 1).trim();
    if (clean.endsWith("*")) clean = clean.substring(0, clean.length() - 1).trim();
    if (clean.startsWith("const")) clean = clean.substring(5).trim();

    for (usz i = 0; i < classes.size(); ++i) {
        String fullName = classes[i].name;
        if (clean == fullName) {
            outClassName = fullName;
            return true;
        }
        if (fullName.endsWith("::" + clean)) {
            outClassName = fullName;
            return true;
        }
    }
    return false;
}

static bool isAbstractClass(const ParsedClass& cls) {
    for (usz m = 0; m < cls.methods.size(); ++m) {
        if (cls.methods[m].isPureVirtual) return true;
    }
    bool inheritsTreeItem = false;
    for (usz i = 0; i < cls.parentClasses.size(); ++i) {
        if (cls.parentClasses[i] == "TreeItem" || cls.parentClasses[i] == "Collection::TreeItem" || cls.parentClasses[i] == "public TreeItem") {
            inheritsTreeItem = true;
            break;
        }
    }
    if (inheritsTreeItem) {
        bool implementsClone = false;
        for (usz m = 0; m < cls.methods.size(); ++m) {
            if (cls.methods[m].name == "clone") {
                implementsClone = true;
                break;
            }
        }
        if (!implementsClone) return true;
    }
    return false;
}

static String generateMetadataBlock(const Array<ParsedClass>& classes) {
    String out;
    out += "\n\n// === Generated Sew Reflection Metadata ===\n";
    out += "namespace Sew { namespace Reflect {\n\n";

    // Add using declarations for all namespaces found in class names
    Map<String, bool> addedNamespaces;
    for (usz i = 0; i < classes.size(); ++i) {
        const auto& cls = classes[i];
        long long pos = 0;
        for (;;) {
            long long colon = cls.name.indexOf("::", (usz)pos);
            if (colon < 0) break;
            String ns = cls.name.substring(0, (usz)colon);
            if (!addedNamespaces.has(ns)) {
                addedNamespaces.set(ns, true);
                out += "using namespace " + ns + ";\n";
            }
            pos = colon + 2;
        }
    }
    out += "\n";
    
    for (usz i = 0; i < classes.size(); ++i) {
        const auto& cls = classes[i];
        if (cls.name.indexOf('<') >= 0) continue; // skip template classes
        
        String cleanName = sanitizeName(cls.name);
        
        // 1. Static wrappers for each method to support generic reflection invocation
        for (usz m = 0; m < cls.methods.size(); ++m) {
            const auto& method = cls.methods[m];
            if (method.isConstructor || method.isDestructor) continue;
            
            out += "inline void* " + cleanName + "_" + method.name + "_" + String((long long)m) + "_wrapper(void* inst, void* args) {\n";
            if (!method.isStatic) {
                out += "    if (inst) {\n";
                out += "        auto* over = Sew::Reflect::ReflectionRegistry::getOverride((usz)inst, \"" + method.name + "\");\n";
                out += "        if (over) return (*over)(inst, args);\n";
                out += "    }\n";
                out += "    " + cls.name + "* self = (" + cls.name + "*)inst;\n";
            }
            out += "    void** argv = (void**)args;\n";
            
            String callStr;
            if (method.isStatic) {
                callStr = cls.name + "::" + method.name + "(";
            } else {
                callStr = "self->" + method.name + "(";
            }
            for (usz p = 0; p < method.params.size(); ++p) {
                if (p > 0) callStr += ", ";
                String paramType = stripReference(method.params[p].type);
                callStr += "*(" + paramType + "*)argv[" + String((long long)p) + "]";
            }
            callStr += ")";
            
            if (method.returnType == "void") {
                out += "    " + callStr + ";\n";
                out += "    return nullptr;\n";
            } else {
                String retType = method.returnType;
                String className;
                if (isClassType(retType, classes, className)) {
                    if (retType.endsWith("*")) {
                        retType = className + "*";
                    } else if (retType.endsWith("&")) {
                        retType = className + "&";
                    } else {
                        retType = className;
                    }
                }

                if (retType.endsWith("&")) {
                    out += "    return (void*)&(" + callStr + ");\n";
                } else {
                    out += "    static " + retType + " ret;\n";
                    out += "    ret = " + callStr + ";\n";
                    out += "    return &ret;\n";
                }
            }
            out += "}\n\n";
        }
        
        // 2. StructDescriptor generator function
        out += "inline const StructDescriptor& get_" + cleanName + "_descriptor() {\n";
        out += "    static StructDescriptor desc = []() {\n";
        out += "        StructDescriptor d;\n";
        out += "        d.name = \"" + cls.name + "\";\n";
        out += "        d.size = sizeof(" + cls.name + ");\n";
        
        bool hasAnyConstructor = false;
        bool hasDefaultConstructor = false;
        for (usz m = 0; m < cls.methods.size(); ++m) {
            if (cls.methods[m].isConstructor) {
                hasAnyConstructor = true;
                if (cls.methods[m].params.size() == 0) {
                    hasDefaultConstructor = true;
                }
            }
        }
        bool canDefaultConstruct = !hasAnyConstructor || hasDefaultConstructor;
        if (cls.name == "Log" || cls.name == "Xi::Log" || cls.name == "Graphics::Screen" || cls.name == "Screen") {
            canDefaultConstruct = false;
        }
        if (isAbstractClass(cls)) {
            canDefaultConstruct = false;
        }
        if (canDefaultConstruct) {
            out += "        d.factory = []() -> void* { return new " + cls.name + "(); };\n";
        }
        out += "        d.destroy = [](void* p) { delete (" + cls.name + "*)p; };\n";
        for (usz p = 0; p < cls.parentClasses.size(); ++p) {
            out += "        d.parentClasses.push(\"" + cls.parentClasses[p] + "\");\n";
        }
        
        // Fields
        for (usz f = 0; f < cls.fields.size(); ++f) {
            const auto& field = cls.fields[f];
            if (field.isStatic) {
                continue;
            }
            if (field.name == "const" || field.name == "static" || field.name == "class" || field.name == "struct" || field.name == "template" || field.name == "public" || field.name == "private" || field.name == "protected") {
                continue;
            }
            String kindStr = getKindString(field.type);
            String customTypeName = getCustomTypeName(field.type);
            
            out += "        {\n";
            out += "            FieldDescriptor fd;\n";
            out += "            fd.name = \"" + field.name + "\";\n";
            out += "            fd.kind = " + kindStr + ";\n";
            out += "            fd.offset = offsetof(" + cls.name + ", " + field.name + ");\n";
            out += "            fd.size = sizeof(((" + cls.name + "*)0)->" + field.name + ");\n";
            out += "            fd.customTypeName = \"" + customTypeName + "\";\n";
            out += "            d.fields.push(fd);\n";
            out += "        }\n";
        }
        
        // Methods
        for (usz m = 0; m < cls.methods.size(); ++m) {
            const auto& method = cls.methods[m];
            if (method.isConstructor || method.isDestructor) continue;
            
            out += "        {\n";
            out += "            MethodDescriptor md;\n";
            out += "            md.name = \"" + method.name + "\";\n";
            out += "            md.returnType = \"" + method.returnType + "\";\n";
            for (usz p = 0; p < method.params.size(); ++p) {
                out += "            md.paramTypes.push(\"" + method.params[p].type + "\");\n";
            }
            out += "            md.functionPtr = (void*)&" + cleanName + "_" + method.name + "_" + String((long long)m) + "_wrapper;\n";
            out += "            d.methods.push(md);\n";
            out += "        }\n";
        }
        
        out += "        return d;\n";
        out += "    }();\n";
        out += "    return desc;\n";
        out += "}\n\n";
        
        // 3. Trait Specialization
        out += "template<>\n";
        out += "struct ReflectionTypeTraits<" + cls.name + "> {\n";
        out += "    static constexpr const char* name() { return \"" + cls.name + "\"; }\n";
        out += "    static const StructDescriptor* descriptor() {\n";
        out += "        return &get_" + cleanName + "_descriptor();\n";
        out += "    }\n";
        out += "};\n\n";
        
        // 4. Registration Helper
        out += "struct Register_" + cleanName + "_Helper {\n";
        out += "    Register_" + cleanName + "_Helper() {\n";
        out += "        ReflectionRegistry::registerStruct(get_" + cleanName + "_descriptor());\n";
        out += "        global[\"" + cls.name + "\"] = \"" + cls.name + "\";\n";
        out += "    }\n";
        out += "};\n";
        out += "inline Register_" + cleanName + "_Helper s_reg_" + cleanName + "_helper;\n\n";
    }
    
    out += "}} // namespace Sew::Reflect\n";
    out += "using namespace Sew::Reflect;\n";
    return out;
}

struct DetectedVar {
    String type;
    String name;
    usz declStart = 0;
    usz declEnd = 0;
    usz insertPos = 0;
};

static inline bool isAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static inline bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline bool matchesAtIndex(const String& source, usz index, const String& target) {
    // Safety guard: An empty string should never match anything
    if (target.isEmpty() || target.length() == 0) return false; 
    
    if (index + target.length() > source.length()) return false;
    return memcmp(source.data() + index, target.data(), target.length()) == 0;
}

static Array<DetectedVar> detectGlobalVars(const String& source, const Array<ParsedClass>& classes) {
    Array<DetectedVar> result;
    int braceDepth = 0;
    bool inComment = false;
    bool inLineComment = false;
    bool inString = false;
    
    usz i = 0;
    while (i < source.length()) {
        char c = source[i];
        if (inComment) {
            if (c == '*' && i + 1 < source.length() && source[i + 1] == '/') {
                inComment = false;
                i += 2;
            } else {
                i++;
            }
            continue;
        }
        if (inLineComment) {
            if (c == '\n') {
                inLineComment = false;
            }
            i++;
            continue;
        }
        if (inString) {
            if (c == '\\' && i + 1 < source.length()) {
                i += 2;
            } else if (c == '"') {
                inString = false;
                i++;
            } else {
                i++;
            }
            continue;
        }
        
        if (c == '/' && i + 1 < source.length()) {
            if (source[i + 1] == '/') {
                inLineComment = true;
                i += 2;
                continue;
            } else if (source[i + 1] == '*') {
                inComment = true;
                i += 2;
                continue;
            }
        }
        if (c == '"') {
            inString = true;
            i++;
            continue;
        }
        
        if (c == '{') {
            braceDepth++;
            i++;
            continue;
        }
        if (c == '}') {
            braceDepth--;
            i++;
            continue;
        }
        
        if (braceDepth == 0) {
            for (usz ci = 0; ci < classes.size(); ++ci) {
                const String& className = classes[ci].name;
                if (className.isEmpty() || className.length() == 0) continue; // <-- ADD THIS LINE
                if (className.indexOf('<') >= 0) continue;
                
                if (matchesAtIndex(source, i, className)) {
                    bool boundaryBefore = (i == 0 || !isAlnum(source[i - 1]) && source[i - 1] != '_');
                    usz nextPos = i + className.length();
                    bool boundaryAfter = (nextPos >= source.length() || !isAlnum(source[nextPos]) && source[nextPos] != '_');
                    
                    if (boundaryBefore && boundaryAfter) {
                        while (nextPos < source.length() && isSpace(source[nextPos])) {
                            nextPos++;
                        }
                        usz idStart = nextPos;
                        while (nextPos < source.length() && (isAlnum(source[nextPos]) || source[nextPos] == '_')) {
                            nextPos++;
                        }
                        String varName = source.substring(idStart, nextPos);
                        if (varName.length() > 0 && varName != "class" && varName != "struct" && varName != "enum" && varName != "union" && varName != "const") {
                            usz checkPos = nextPos;
                            while (checkPos < source.length() && isSpace(source[checkPos])) {
                                checkPos++;
                            }
                            if (checkPos < source.length() && (source[checkPos] == ';' || source[checkPos] == '=' || source[checkPos] == '{' || source[checkPos] == '(')) {
                                usz endPos = checkPos;
                                while (endPos < source.length() && source[endPos] != ';') {
                                    endPos++;
                                }
                                if (endPos < source.length()) {
                                    DetectedVar dv;
                                    dv.type = className;
                                    dv.name = varName;
                                    dv.declStart = idStart;
                                    dv.declEnd = nextPos;
                                    dv.insertPos = endPos + 1;
                                    result.push(dv);
                                }
                            }
                        }
                        i = nextPos;
                        continue;
                    }
                }
            }
        }
        
        i++;
    }
    return result;
}

static String rewriteCastsAndIdentifiers(const String& source, const Array<ParsedClass>& classes, const Array<DetectedVar>& globalVars) {
    String rewritten;
    int braceDepth = 0;
    bool inComment = false;
    bool inLineComment = false;
    bool inString = false;
    
    struct ShadowInfo {
        String name;
        int depth;
        int braceDepth;
    };
    Array<ShadowInfo> shadows;
    for (usz v = 0; v < globalVars.size(); ++v) {
        ShadowInfo si;
        si.name = globalVars[v].name;
        si.depth = 0;
        si.braceDepth = -1;
        shadows.push(si);
    }
    
    usz i = 0;
    while (i < source.length()) {
        char c = source[i];
        
        if (inComment) {
            rewritten.push(c);
            if (c == '*' && i + 1 < source.length() && source[i + 1] == '/') {
                rewritten.push('/');
                inComment = false;
                i += 2;
            } else {
                i++;
            }
            continue;
        }
        if (inLineComment) {
            rewritten.push(c);
            if (c == '\n') {
                inLineComment = false;
            }
            i++;
            continue;
        }
        if (inString) {
            rewritten.push(c);
            if (c == '\\' && i + 1 < source.length()) {
                rewritten.push(source[i + 1]);
                i += 2;
            } else if (c == '"') {
                inString = false;
                i++;
            } else {
                i++;
            }
            continue;
        }
        
        if (c == '/' && i + 1 < source.length()) {
            if (source[i + 1] == '/') {
                rewritten.push('/'); rewritten.push('/');
                inLineComment = true;
                i += 2;
                continue;
            } else if (source[i + 1] == '*') {
                rewritten.push('/'); rewritten.push('*');
                inComment = true;
                i += 2;
                continue;
            }
        }
        if (c == '"') {
            rewritten.push('"');
            inString = true;
            i++;
            continue;
        }
        
        if (c == '{') {
            braceDepth++;
            rewritten.push(c);
            i++;
            continue;
        }
        if (c == '}') {
            for (usz v = 0; v < shadows.size(); ++v) {
                if (shadows[v].depth > 0 && braceDepth == shadows[v].braceDepth) {
                    shadows[v].depth = 0;
                    shadows[v].braceDepth = -1;
                }
            }
            braceDepth--;
            rewritten.push(c);
            i++;
            continue;
        }
        
        // 1. Zero-allocation check for Casts
        bool castMatched = false;
        for (usz ci = 0; ci < classes.size(); ++ci) {
            const String& className = classes[ci].name;
            if (className.isEmpty() || className.length() == 0) continue;
            if (className.indexOf('<') >= 0) continue;
            
            // Check static_cast<className*>
            if (matchesAtIndex(source, i, "static_cast<")) {
                usz classStart = i + 12; // length of "static_cast<"
                if (matchesAtIndex(source, classStart, className) && 
                    matchesAtIndex(source, classStart + className.length(), "*>")) {
                    rewritten += "Sew::Reflect::ReflectionRegistry::resolveCast<" + className + "*>";
                    i += 12 + className.length() + 2;
                    castMatched = true;
                    break;
                }
            }
            
            // Check reinterpret_cast<className*>
            if (matchesAtIndex(source, i, "reinterpret_cast<")) {
                usz classStart = i + 17; // length of "reinterpret_cast<"
                if (matchesAtIndex(source, classStart, className) && 
                    matchesAtIndex(source, classStart + className.length(), "*>")) {
                    rewritten += "Sew::Reflect::ReflectionRegistry::resolveCast<" + className + "*>";
                    i += 17 + className.length() + 2;
                    castMatched = true;
                    break;
                }
            }
            
            // Check const_cast<className*>
            if (matchesAtIndex(source, i, "const_cast<")) {
                usz classStart = i + 11; // length of "const_cast<"
                if (matchesAtIndex(source, classStart, className) && 
                    matchesAtIndex(source, classStart + className.length(), "*>")) {
                    rewritten += "Sew::Reflect::ReflectionRegistry::resolveCast<" + className + "*>";
                    i += 11 + className.length() + 2;
                    castMatched = true;
                    break;
                }
            }
            
            // Check C-style casts (className*)( or (className *)(
            if (c == '(') {
                usz classStart = i + 1;
                if (matchesAtIndex(source, classStart, className)) {
                    usz suffixStart = classStart + className.length();
                    if (matchesAtIndex(source, suffixStart, "*)(")) {
                        rewritten += "Sew::Reflect::ReflectionRegistry::resolveCast<" + className + "*>(";
                        i += 1 + className.length() + 3;
                        castMatched = true;
                        break;
                    }
                    if (matchesAtIndex(source, suffixStart, " *)(")) {
                        rewritten += "Sew::Reflect::ReflectionRegistry::resolveCast<" + className + "*>(";
                        i += 1 + className.length() + 4;
                        castMatched = true;
                        break;
                    }
                }
            }
        }
        if (castMatched) continue;
        
        // 2. Check for Global Variable References
        bool varMatched = false;
        for (usz vi = 0; vi < globalVars.size(); ++vi) {
            const auto& var = globalVars[vi];
            const String& name = var.name;
            const String& type = var.type;
            if (name.isEmpty() || name.length() == 0) continue;
            
            if (matchesAtIndex(source, i, name)) {
                bool boundaryBefore = (i == 0 || !isAlnum(source[i - 1]) && source[i - 1] != '_');
                usz nextPos = i + name.length();
                bool boundaryAfter = (nextPos >= source.length() || !isAlnum(source[nextPos]) && source[nextPos] != '_');
                
                if (boundaryBefore && boundaryAfter) {
                     if (name.length() == 0) {
                        ::printf("\n[ERROR] Infinite loop prevented! Empty variable name of type '%s' matched.\n", type.c_str());
                        ::fflush(stdout);
                        ::exit(1);
                    }

                    bool isDeclaration = false;
                    if (braceDepth > 0) {
                        long long prev = (long long)i - 1;
                        while (prev >= 0 && isSpace(source[(usz)prev])) prev--;
                        long long tokEnd = prev + 1;
                        while (prev >= 0 && (isAlnum(source[(usz)prev]) || source[(usz)prev] == '_')) prev--;
                        String prevTok = source.substring((usz)prev + 1, (usz)tokEnd);
                        if (prevTok.length() > 0 && prevTok != "return" && prevTok != "goto" && prevTok != "throw" && prevTok != "case" && prevTok != "new" && prevTok != "delete") {
                            isDeclaration = true;
                            shadows[vi].depth++;
                            shadows[vi].braceDepth = braceDepth;
                        }
                    }
                    
                    bool isMemberAccess = false;
                    long long pidx = (long long)i - 1;
                    while (pidx >= 0 && isSpace(source[(usz)pidx])) pidx--;
                    if (pidx >= 0) {
                        if (source[(usz)pidx] == '.') {
                            isMemberAccess = true;
                        } else if (pidx >= 1 && source[(usz)pidx] == '>' && source[(usz)pidx - 1] == '-') {
                            isMemberAccess = true;
                        } else if (pidx >= 1 && source[(usz)pidx] == ':' && source[(usz)pidx - 1] == ':') {
                            isMemberAccess = true;
                        }
                    }
                    
                    if (shadows[vi].depth == 0 && !isMemberAccess && !isDeclaration) {
                        rewritten += "(*Sew::Reflect::ReflectionRegistry::resolveCast<" + type + "*>(&" + name + "_raw))";
                    } else {
                        rewritten += name;
                    }
                    i = nextPos;
                    varMatched = true;
                    break;
                }
            }
        }
        if (varMatched) continue;
        
        rewritten.push(c);
        i++;
    }
    return rewritten;
}

static bool containsToken(const String& source, const String& token) {
    String cleanToken = token;
    long long lastColon = -1;
    for (usz i = 0; i < token.length(); ++i) {
        if (token.data()[i] == ':') lastColon = (long long)i;
    }
    if (lastColon >= 0) {
        cleanToken = token.substring((usz)lastColon + 1);
    }
    if (cleanToken.isEmpty()) return false;

    bool inComment = false;
    bool inLineComment = false;
    bool inString = false;
    bool inChar = false;

    usz i = 0;
    usz tokenLen = cleanToken.length();
    while (i < source.length()) {
        char c = source.data()[i];
        if (inComment) {
            if (c == '*' && i + 1 < source.length() && source.data()[i + 1] == '/') {
                inComment = false;
                i += 2;
            } else {
                i++;
            }
            continue;
        }
        if (inLineComment) {
            if (c == '\n') {
                inLineComment = false;
            }
            i++;
            continue;
        }
        if (inString) {
            if (c == '\\' && i + 1 < source.length()) {
                i += 2;
            } else if (c == '"') {
                inString = false;
                i++;
            } else {
                i++;
            }
            continue;
        }
        if (inChar) {
            if (c == '\\' && i + 1 < source.length()) {
                i += 2;
            } else if (c == '\'') {
                inChar = false;
                i++;
            } else {
                i++;
            }
            continue;
        }

        if (c == '/' && i + 1 < source.length()) {
            if (source.data()[i + 1] == '/') {
                inLineComment = true;
                i += 2;
                continue;
            } else if (source.data()[i + 1] == '*') {
                inComment = true;
                i += 2;
                continue;
            }
        }
        if (c == '"') {
            inString = true;
            i++;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            i++;
            continue;
        }

        // Check if token matches at current position
        if (i + tokenLen <= source.length()) {
            bool matches = true;
            for (usz j = 0; j < tokenLen; ++j) {
                if (source.data()[i + j] != cleanToken.data()[j]) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                bool leftOk = true;
                if (i > 0) {
                    char leftC = source.data()[i - 1];
                    if ((leftC >= 'a' && leftC <= 'z') || (leftC >= 'A' && leftC <= 'Z') || (leftC >= '0' && leftC <= '9') || leftC == '_') {
                        leftOk = false;
                    }
                }
                bool rightOk = true;
                usz endPos = i + tokenLen;
                if (endPos < source.length()) {
                    char rightC = source.data()[endPos];
                    if ((rightC >= 'a' && rightC <= 'z') || (rightC >= 'A' && rightC <= 'Z') || (rightC >= '0' && rightC <= '9') || rightC == '_') {
                        rightOk = false;
                    }
                }
                if (leftOk && rightOk) {
                    return true;
                }
            }
        }
        i++;
    }
    return false;
}

static String rewriteCppSource(const String& source, const Array<ParsedClass>& classes) {
    String content = source;
    
    // 1. Detect global variables
    Array<DetectedVar> globalVars = detectGlobalVars(content, classes);
    
    // Rename declarations and insert registration helpers (in reverse order to preserve indices)
    for (long long j = (long long)globalVars.size() - 1; j >= 0; --j) {
        const auto& var = globalVars[(usz)j];
        
        content = content.substring(0, var.declStart) + var.name + "_raw" + content.substring(var.declEnd);
        
        String helper;
        helper += "\n";
        helper += "struct Register_" + var.name + "_Helper {\n";
        helper += "    Register_" + var.name + "_Helper() {\n";
        helper += "        Sew::Reflect::ReflectionRegistry::registerVariable(\"" + var.name + "\", &" + var.name + "_raw);\n";
        helper += "    }\n";
        helper += "};\n";
        helper += "inline Register_" + var.name + "_Helper s_reg_" + var.name + "_helper;\n";
        
        usz adjustedInsertPos = var.insertPos + 4; // account for "_raw" suffix
        content = content.substring(0, adjustedInsertPos) + helper + content.substring(adjustedInsertPos);
    }
    
    // 2. Robust token-based rewriting of casts and global variable references
    content = rewriteCastsAndIdentifiers(content, classes, globalVars);
    
    content = content.replace("reflect(rf.ptr(", "rf.reflect(");
    content = content.replace("reflect( rf.ptr(", "rf.reflect(");
    content = content.replace("reflect(rf.ptr( ", "rf.reflect(");
    
    Array<ParsedClass> filteredClasses;
    for (usz i = 0; i < classes.size(); ++i) {
        if (containsToken(content, classes[i].name)) {
            filteredClasses.push(classes[i]);
        }
    }

    String finalSource;
    finalSource += "#include <Reflection/Reflection.hpp>\n";
    finalSource += "#include <Reflection/Global.hpp>\n\n";
    finalSource += content;
    finalSource += generateMetadataBlock(filteredClasses);
    
    return finalSource;
}

Array<ImportSpec> CppLanguage::parseImports(const String &source,
                                            const String &filePath) {
  PreprocessorResult ppResult = _preprocessor.process(source, filePath);
  Array<ImportSpec> imports;

  // Local includes
  for (usz i = 0; i < ppResult.localIncludes.size(); ++i) {
    ImportSpec spec;
    spec.specifier = ppResult.localIncludes[i];
    spec.fromFile = filePath;
    spec.line = 0;
    spec.isSystem = false;
    imports.push(Xi::Move(spec));
  }

  // System includes
  for (usz i = 0; i < ppResult.systemIncludes.size(); ++i) {
    ImportSpec spec;
    spec.specifier = ppResult.systemIncludes[i];
    spec.fromFile = filePath;
    spec.line = 0;
    spec.isSystem = true;
    imports.push(Xi::Move(spec));
  }

  bool skipSiblings = (::getenv("SEW_NO_SIBLINGS") != nullptr);

  if (!skipSiblings) {
      // Sibling source files (auto-discovered .cpp for .h)
      for (usz i = 0; i < ppResult.siblingSourceFiles.size(); ++i) {
        ImportSpec spec;
        spec.specifier = ppResult.siblingSourceFiles[i];
        spec.fromFile = filePath;
        spec.line = 0;
        spec.isSystem = false;
        imports.push(Xi::Move(spec));
      }

      // Sibling source files for the header itself (e.g. String.cpp for String.hpp)
      Array<String> selfSiblings = _preprocessor.findSiblingSourceFiles(filePath);
      for (usz i = 0; i < selfSiblings.size(); ++i) {
        ImportSpec spec;
        spec.specifier = selfSiblings[i];
        spec.fromFile = filePath;
        spec.line = 0;
        spec.isSystem = false;
        imports.push(Xi::Move(spec));
      }
  }

  return imports;
}

static String getTempDir() {
    const char* envTemp = ::getenv("SEW_TEMP_DIR");
    if (envTemp && envTemp[0] != '\0') {
        return String(envTemp);
    }

    const char* home = ::getenv("HOME");
    if (home && home[0] != '\0') {
        String tempDir = String(home) + "/.cache/sew";
        struct stat st;
        if (::stat(tempDir.c_str(), &st) != 0) {
            ::mkdir(tempDir.c_str(), 0755);
        }
        return tempDir;
    }

    return "/tmp";
}

static String parentDir(const String& path) {
    long long lastSlash = -1;
    for (usz i = 0; i < path.size(); ++i) {
        if (path.data()[i] == '/') lastSlash = (long long)i;
    }
    if (lastSlash >= 0) {
        return path.substring(0, (usz)lastSlash);
    }
    return "";
}

CompileResult CppLanguage::compile(const CompileRequest &req) {
  if (req.sourcePath.endsWith(".c")) {
    CompileRequest modReq = req;
    Array<String> searchPaths = _preprocessor.getSearchPaths(req.sourcePath);
    for (usz i = 0; i < searchPaths.size(); ++i) {
        modReq.includePaths.push(searchPaths[i]);
    }
    modReq.flags.push("-DGLFW_AVAILABLE=1");
    modReq.flags.push("-D_GLFW_X11");
    modReq.flags.push("-DPLATFORM_LINUX=1");
    modReq.flags.push("-DVULKAN_SUPPORTED=1");
    modReq.flags.push("-DENABLE_HLSL=1");
    modReq.flags.push("-DVK_USE_PLATFORM_XLIB_KHR=1");
    modReq.flags.push("-DVK_USE_PLATFORM_XCB_KHR=1");
    modReq.flags.push("-DVK_USE_PLATFORM_WAYLAND_KHR=1");
    modReq.flags.push("-D__forceinline=inline");
    modReq.flags.push("-DvkCmdBeginRenderingKHR=vkCmdBeginRendering");
    modReq.flags.push("-DvkCmdEndRenderingKHR=vkCmdEndRendering");
    modReq.flags.push("-pthread");

    if (req.sourcePath.includes("/deps/diligent/")) {
        long long idx = req.sourcePath.indexOf("/deps/diligent/");
        if (idx >= 0) {
            String dilRoot = req.sourcePath.substring(0, (usz)idx + 15);
            modReq.includePaths.push(dilRoot + "/ThirdParty/volk");
            modReq.includePaths.push(dilRoot + "/ThirdParty/SPIRV-Cross");
            modReq.includePaths.push(dilRoot + "/ThirdParty/SPIRV-Headers/include");
            modReq.includePaths.push(dilRoot + "/ThirdParty/volk");
            modReq.includePaths.push(dilRoot + "/ThirdParty/Vulkan-Headers/include");
            modReq.includePaths.push(dilRoot + "/ThirdParty/xxHash");
            modReq.includePaths.push(dilRoot + "/ThirdParty/DirectXShaderCompiler");
            modReq.includePaths.push(dilRoot + "/ThirdParty/glslang");
        }
    }
    return invokeClang(modReq, "");
  }

  if (req.sourcePath.endsWith("Reflection.cpp") || req.sourcePath.endsWith("sew_bridge.cpp")) {
    CompileRequest modReq = req;
    PreprocessorResult ppResult = _preprocessor.process(req.sourceContent, req.sourcePath);
    Array<String> searchPaths = _preprocessor.getSearchPaths(req.sourcePath);
    for (usz i = 0; i < searchPaths.size(); ++i) {
        modReq.includePaths.push(searchPaths[i]);
    }
    
    // Safe temporary array to collect extra paths without modifying req.includePaths in-place
    Array<String> extraPaths;
    for (usz i = 0; i < req.includePaths.size(); ++i) {
        if (req.includePaths[i].includes("/xic/")) {
            long long idx = req.includePaths[i].indexOf("/xic/");
            if (idx >= 0) {
                String xicRoot = req.includePaths[i].substring(0, (usz)idx + 4);
                String dilRoot = xicRoot + "/deps/diligent";
                extraPaths.push(dilRoot);
                extraPaths.push(dilRoot + "/Platforms/interface");
            }
        }
    }
    
    // Safely append the collected paths
    for (usz i = 0; i < extraPaths.size(); ++i) {
        modReq.includePaths.push(extraPaths[i]);
    }
    
    return invokeClang(modReq, ppResult.strippedSource);
  }

  Array<ParsedClass> classesCopy;
  {
      std::lock_guard<std::mutex> lock(g_parsedClassesMutex);
      classesCopy = g_allParsedClasses;
  }
//   ::printf("Compiling %s: classesCopy size is %d\n", req.sourcePath.c_str(), (int)classesCopy.size());
//   ::fflush(stdout);
  String rewritten = rewriteCppSource(req.sourceContent, classesCopy);

  String safePath = req.sourcePath;
  safePath = safePath.replace("/", "_");
  safePath = safePath.replace("\\", "_");
  String tempPath = getTempDir() + "/sew_prep_" + safePath + ".cpp";
  FILE* f = fopen(tempPath.c_str(), "w");
  if (f) {
      fwrite(rewritten.data(), 1, rewritten.size(), f);
      fclose(f);
  }

  CompileRequest modReq = req;
  modReq.sourcePath = tempPath;
  String srcDir = parentDir(req.sourcePath);
  if (!srcDir.isEmpty()) {
      modReq.includePaths.push(srcDir);
  }
  Array<String> searchPaths = _preprocessor.getSearchPaths(req.sourcePath);
  for (usz i = 0; i < searchPaths.size(); ++i) {
      modReq.includePaths.push(searchPaths[i]);
  }
  modReq.flags.push("-DGLFW_AVAILABLE=1");
  modReq.flags.push("-D_GLFW_X11");
  modReq.flags.push("-DPLATFORM_LINUX=1");
  modReq.flags.push("-DVULKAN_SUPPORTED=1");
  modReq.flags.push("-DENABLE_HLSL=1");
  modReq.flags.push("-DVK_USE_PLATFORM_XLIB_KHR=1");
  modReq.flags.push("-DVK_USE_PLATFORM_XCB_KHR=1");
  modReq.flags.push("-DVK_USE_PLATFORM_WAYLAND_KHR=1");
  modReq.flags.push("-D__forceinline=inline");
  modReq.flags.push("-DvkCmdBeginRenderingKHR=vkCmdBeginRendering");
  modReq.flags.push("-DvkCmdEndRenderingKHR=vkCmdEndRendering");
  modReq.flags.push("-pthread");

  if (req.sourcePath.includes("/deps/diligent/")) {
      long long idx = req.sourcePath.indexOf("/deps/diligent/");
      if (idx >= 0) {
          String dilRoot = req.sourcePath.substring(0, (usz)idx + 15);
          modReq.includePaths.push(dilRoot + "/ThirdParty/volk");
          modReq.includePaths.push(dilRoot + "/ThirdParty/SPIRV-Cross");
          modReq.includePaths.push(dilRoot + "/ThirdParty/SPIRV-Headers/include");
          modReq.includePaths.push(dilRoot + "/ThirdParty/volk");
          modReq.includePaths.push(dilRoot + "/ThirdParty/Vulkan-Headers/include");
          modReq.includePaths.push(dilRoot + "/ThirdParty/xxHash");
          modReq.includePaths.push(dilRoot + "/ThirdParty/DirectXShaderCompiler");
          modReq.includePaths.push(dilRoot + "/ThirdParty/glslang");
      }
  }

  CompileResult res = invokeClang(modReq, rewritten);

  ::unlink(tempPath.c_str());
  return res;
}

CompileResult CppLanguage::invokeClang(const CompileRequest &req,
                                       const String &strippedSource) {
  CompileResult result;

  Process p;
  bool isC = req.sourcePath.endsWith(".c");
  p.file = isC ? "clang" : "clang++";

  if (req.targetTriple.indexOf("wasm32") >= 0) {
      if (ensureWasiSdk()) {
          p.file = getWasiSdkDir() + (isC ? "/bin/clang" : "/bin/clang++");
      }
      if (!isC) {
          p.arg.push("-fno-exceptions");
      }
  }

  // Base flags
  p.arg.push("-c");
  if (!isC) {
      p.arg.push("-std=c++17");
      p.arg.push("-Wno-invalid-offsetof");
  } else {
      p.arg.push("-std=gnu99");
  }

  // Target triple
  if (req.targetTriple.length() > 0) {
    String targetFlag = "--target=";
    targetFlag += req.targetTriple;
    p.arg.push(targetFlag);

    // Automatically add sysroot for wasm targets on Fedora/RHEL if using system clang++ and it's present
    if (req.targetTriple.indexOf("wasm32") >= 0 && p.file == "clang++") {
      struct stat st;
      if (::stat("/usr/wasm32-wasi", &st) == 0 && S_ISDIR(st.st_mode)) {
        p.arg.push("--sysroot=/usr/wasm32-wasi");
      }
    }
  }

  // Output
  if (req.outputPath.length() > 0) {
    p.arg.push("-o");
    p.arg.push(req.outputPath);
  }

  // Extra flags
  for (usz i = 0; i < req.flags.size(); ++i) {
    p.arg.push(req.flags[i]);
  }

  const char* envFlags = getenv("SEW_EXTRA_FLAGS");
  if (envFlags) {
      String envFlagsStr(envFlags);
      String current;
      for (usz i = 0; i < envFlagsStr.length(); ++i) {
          if (envFlagsStr.data()[i] == ' ') {
              if (!current.isEmpty()) {
                  p.arg.push(current);
                  current.clear();
              }
          } else {
              current.push(envFlagsStr.data()[i]);
          }
      }
      if (!current.isEmpty()) {
          p.arg.push(current);
      }
  }

  // Add sew include path
  p.arg.push("-I");
  p.arg.push(getSewIncludePath());

  for (usz i = 0; i < req.includePaths.size(); ++i) {
    p.arg.push("-I");
    p.arg.push(req.includePaths[i]);
  }

  // Add project include path (if it exists)
  struct stat st;
  if (::stat("include", &st) == 0 && S_ISDIR(st.st_mode)) {
    p.arg.push("-I");
    p.arg.push("include");
  }

  // Add XiC include path
  const char* xicPath = getenv("SEW_XIC_INCLUDE");
  if (xicPath) {
    p.arg.push("-I");
    p.arg.push(xicPath);
  } else {
    // Try common locations
    const char* tryPaths[] = {
        "../xic/include",
        "/home/xi/Repo/xic/include",
        nullptr
    };
    for (int i = 0; tryPaths[i]; ++i) {
      struct stat st;
      if (stat(tryPaths[i], &st) == 0) {
        p.arg.push("-I");
        p.arg.push(tryPaths[i]);
        break;
      }
    }
  }

  // Add extra include paths (split by ':')
  const char* extraInclude = getenv("SEW_EXTRA_INCLUDE");
  if (extraInclude) {
    String extraStr(extraInclude);
    Array<String> paths;
    String current;
    for (usz i = 0; i < extraStr.length(); ++i) {
      if (extraStr.data()[i] == ':') {
        if (!current.isEmpty()) {
          paths.push(current);
          current.clear();
        }
      } else {
        current.push(extraStr.data()[i]);
      }
    }
    if (!current.isEmpty()) {
      paths.push(current);
    }
    for (usz i = 0; i < paths.size(); ++i) {
      p.arg.push("-I");
      p.arg.push(paths[i]);
    }
  }

  if (req.sourcePath.includes("sew_bridge") || req.sourcePath.includes("sew_qjs_bindings")) {
      p.arg.push("-O0");
      p.arg.push("-g0");
  }

  // Source file
  p.arg.push(req.sourcePath);

  // Execute
  p.wait();

  result.success = (p.exitCode == 0);
  result.outputPath = req.outputPath;

  // Collect stderr
  while (p.stderr.size() > 0) {
    String chunk = p.stderr.shift();
    result.errors += chunk;
  }

  return result;
}

} // namespace Languages
} // namespace Sew



#include <functional>
#include <vector>

enum spv_target_env {
    SPV_ENV_UNIVERSAL_1_0,
    SPV_ENV_VULKAN_1_0,
    SPV_ENV_UNIVERSAL_1_1,
    SPV_ENV_OPENCL_2_1,
    SPV_ENV_OPENCL_2_2,
    SPV_ENV_GLSL_450,
    SPV_ENV_UNIVERSAL_1_2,
    SPV_ENV_OPENCL_1_2,
    SPV_ENV_OPENCL_2_0,
    SPV_ENV_VULKAN_1_1,
    SPV_ENV_WEBGPU_0,
    SPV_ENV_UNIVERSAL_1_3,
    SPV_ENV_VULKAN_1_1_SPIRV_1_4,
    SPV_ENV_UNIVERSAL_1_4,
    SPV_ENV_VULKAN_1_2,
    SPV_ENV_UNIVERSAL_1_5,
    SPV_ENV_VULKAN_1_3,
    SPV_ENV_MAX
};

enum spv_message_level_t {
    SPV_MSG_FATAL,
    SPV_MSG_INTERNAL_ERROR,
    SPV_MSG_ERROR,
    SPV_MSG_WARNING,
    SPV_MSG_INFO,
    SPV_MSG_DEBUG
};

struct spv_position_t {
    size_t line;
    size_t column;
    size_t index;
};
struct spv_optimizer_options_t {};
struct spv_validator_options_t {};

namespace spvtools {
    class ValidatorOptions {
    public:
        ValidatorOptions();
        ~ValidatorOptions();
        void SetBeforeHlslLegalization(bool);
    };
    ValidatorOptions::ValidatorOptions() {}
    ValidatorOptions::~ValidatorOptions() {}
    void ValidatorOptions::SetBeforeHlslLegalization(bool) {}

    class OptimizerOptions {
    public:
        OptimizerOptions();
        ~OptimizerOptions();
        void set_validator_options(const ValidatorOptions&);
        void set_run_validator(bool);
    };
    OptimizerOptions::OptimizerOptions() {}
    OptimizerOptions::~OptimizerOptions() {}
    void OptimizerOptions::set_validator_options(const ValidatorOptions&) {}
    void OptimizerOptions::set_run_validator(bool) {}

    class Optimizer {
    public:
        struct PassToken {
            ~PassToken();
        };
        Optimizer(spv_target_env env);
        ~Optimizer();
        void SetMessageConsumer(std::function<void(spv_message_level_t, char const*, spv_position_t const&, char const*)> consumer);
        void RegisterLegalizationPasses();
        void RegisterPerformancePasses();
        void RegisterPass(PassToken&& token);
        bool Run(const uint32_t* code, size_t size, std::vector<uint32_t>* optimized, spv_optimizer_options_t* options) const;
    };
    Optimizer::PassToken::~PassToken() {}
    Optimizer::Optimizer(spv_target_env env) {}
    Optimizer::~Optimizer() {}
    void Optimizer::SetMessageConsumer(std::function<void(spv_message_level_t, char const*, spv_position_t const&, char const*)> consumer) {}
    void Optimizer::RegisterLegalizationPasses() {}
    void Optimizer::RegisterPerformancePasses() {}
    void Optimizer::RegisterPass(PassToken&& token) {}
    bool Optimizer::Run(const uint32_t* code, size_t size, std::vector<uint32_t>* optimized, spv_optimizer_options_t* options) const { return true; }

    Optimizer::PassToken CreateStripReflectInfoPass() {
        return Optimizer::PassToken();
    }
}

extern "C" {
    spv_validator_options_t* spvValidatorOptionsCreate() { return nullptr; }
    void spvValidatorOptionsDestroy(spv_validator_options_t*) {}
    void spvValidatorOptionsSetBeforeHlslLegalization(spv_validator_options_t*, bool) {}
    spv_optimizer_options_t* spvOptimizerOptionsCreate() { return nullptr; }
    void spvOptimizerOptionsDestroy(spv_optimizer_options_t*) {}
    void spvOptimizerOptionsSetValidatorOptions(spv_optimizer_options_t*, spv_validator_options_t*) {}
    void spvOptimizerOptionsSetRunValidator(spv_optimizer_options_t*, bool) {}
}

#include <string>

namespace Diligent {
    std::vector<uint32_t> ConvertUBOToPushConstants(const std::vector<uint32_t>& SPIRV, const std::string& PushConstantName) {
        return SPIRV;
    }
}
