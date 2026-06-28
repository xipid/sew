/**
 * @file CppPreprocessor.cpp
 * @brief Full C++ preprocessor implementation for Sew.
 *
 * Handles: #include, #define, #undef, #ifdef, #ifndef, #if, #elif,
 *          #else, #endif, #pragma once, #error, #warning.
 *
 * Strips all handled directives from the output source so clang
 * doesn't double-process them. Passes #define through if still
 * needed by clang for macro expansion.
 */

#include <Languages/CPP/CppPreprocessor.hpp>
#include <sys/stat.h>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <cstdio>

namespace Sew { namespace Languages {

static std::mutex s_wildcardMutex;
static thread_local Array<String> t_currentSearchPaths;

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

static String canonicalizePath(const String& path);

// ─── Utilities ──────────────────────────────────────────────────────────

static bool isSpace(u8 c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static bool isAlpha(u8 c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isAlnum(u8 c) {
    return isAlpha(c) || (c >= '0' && c <= '9');
}

static bool isDigit(u8 c) {
    return c >= '0' && c <= '9';
}

static String trimStr(const String& s) {
    return s.trim();
}

// ─── Conditional Stack ──────────────────────────────────────────────────

bool CppPreprocessor::allActive(const Array<CondFrame>& stack) const {
    for (usz i = 0; i < stack.size(); ++i) {
        if (!stack[i].active) return false;
    }
    return true;
}

// ─── Directive Parsing ──────────────────────────────────────────────────

CppPreprocessor::Directive CppPreprocessor::parseLine(const String& line) const {
    Directive dir;
    const u8* p = line.data();
    const u8* end = p + line.size();

    // Skip whitespace
    while (p < end && isSpace(*p)) p++;

    // Must start with #
    if (p >= end || *p != '#') return dir;
    p++;

    // Skip whitespace after #
    while (p < end && isSpace(*p)) p++;

    // Read directive name
    const u8* nameStart = p;
    while (p < end && isAlpha(*p)) p++;
    dir.type = String(nameStart, (usz)(p - nameStart));

    // Skip whitespace
    while (p < end && isSpace(*p)) p++;

    // Rest is args
    dir.args = String(p, (usz)(end - p));

    return dir;
}

// ─── #define Handler ────────────────────────────────────────────────────

void CppPreprocessor::handleDefine(const String& args, Map<String, MacroDef>& defines) {
    const u8* p = args.data();
    const u8* end = p + args.size();

    // Read macro name
    while (p < end && isSpace(*p)) p++;
    const u8* nameStart = p;
    while (p < end && isAlnum(*p)) p++;
    String name(nameStart, (usz)(p - nameStart));
    if (name.isEmpty()) return;

    MacroDef def;
    def.name = name;

    // Check for function-like macro: NAME(
    if (p < end && *p == '(') {
        def.isFunctionLike = true;
        p++; // skip '('
        // Parse params
        while (p < end && *p != ')') {
            const u8* loopStart = p;
            while (p < end && isSpace(*p)) p++;
            const u8* paramStart = p;
            while (p < end && isAlnum(*p)) p++;
            if (p > paramStart) {
                def.params.push(String(paramStart, (usz)(p - paramStart)));
            }
            while (p < end && isSpace(*p)) p++;
            if (p < end && *p == ',') p++;
            if (p == loopStart) {
                p++;
            }
        }
        if (p < end && *p == ')') p++;
    }

    // Skip whitespace
    while (p < end && isSpace(*p)) p++;

    // Rest is value
    if (p < end) {
        def.value = String(p, (usz)(end - p)).trim();
    }

    defines.set(name, Xi::Move(def));
}

void CppPreprocessor::handleUndef(const String& args, Map<String, MacroDef>& defines) {
    String name = trimStr(args);
    defines.remove(name);
}

bool CppPreprocessor::handleIfdef(const String& args, const Map<String, MacroDef>& defines) {
    String name = trimStr(args);
    return defines.has(name);
}

bool CppPreprocessor::handleIfndef(const String& args, const Map<String, MacroDef>& defines) {
    return !handleIfdef(args, defines);
}

// ─── Expression Evaluator for #if / #elif ───────────────────────────────

void CppPreprocessor::skipWhitespace(const u8*& p, const u8* end) const {
    while (p < end && isSpace(*p)) p++;
}

String CppPreprocessor::readIdentifier(const u8*& p, const u8* end) const {
    const u8* start = p;
    while (p < end && isAlnum(*p)) p++;
    return String(start, (usz)(p - start));
}

// Recursive descent parser: expr = or_expr
// or_expr = and_expr ('||' and_expr)*
// and_expr = cmp_expr ('&&' cmp_expr)*
// cmp_expr = atom (('==' | '!=' | '<' | '>' | '<=' | '>=') atom)?
// atom = '!' atom | '(' expr ')' | 'defined' '(' IDENT ')' | 'defined' IDENT | NUMBER | IDENT

long long CppPreprocessor::evalExpr(const String& expr, const Map<String, MacroDef>& defines) {
    const u8* p = expr.data();
    const u8* end = p + expr.size();
    return evalExprOr(p, end, defines);
}

long long CppPreprocessor::evalExprOr(const u8*& p, const u8* end,
                                       const Map<String, MacroDef>& defines) {
    long long val = evalExprAnd(p, end, defines);
    while (true) {
        skipWhitespace(p, end);
        if (p + 1 < end && p[0] == '|' && p[1] == '|') {
            p += 2;
            long long rhs = evalExprAnd(p, end, defines);
            val = (val || rhs) ? 1 : 0;
        } else break;
    }
    return val;
}

long long CppPreprocessor::evalExprAnd(const u8*& p, const u8* end,
                                        const Map<String, MacroDef>& defines) {
    long long val = evalExprCmp(p, end, defines);
    while (true) {
        skipWhitespace(p, end);
        if (p + 1 < end && p[0] == '&' && p[1] == '&') {
            p += 2;
            long long rhs = evalExprCmp(p, end, defines);
            val = (val && rhs) ? 1 : 0;
        } else break;
    }
    return val;
}

long long CppPreprocessor::evalExprCmp(const u8*& p, const u8* end,
                                        const Map<String, MacroDef>& defines) {
    long long lhs = evalExprAtom(p, end, defines);
    skipWhitespace(p, end);

    if (p + 1 < end) {
        if (p[0] == '=' && p[1] == '=') {
            p += 2;
            long long rhs = evalExprAtom(p, end, defines);
            return (lhs == rhs) ? 1 : 0;
        }
        if (p[0] == '!' && p[1] == '=') {
            p += 2;
            long long rhs = evalExprAtom(p, end, defines);
            return (lhs != rhs) ? 1 : 0;
        }
        if (p[0] == '<' && p[1] == '=') {
            p += 2;
            long long rhs = evalExprAtom(p, end, defines);
            return (lhs <= rhs) ? 1 : 0;
        }
        if (p[0] == '>' && p[1] == '=') {
            p += 2;
            long long rhs = evalExprAtom(p, end, defines);
            return (lhs >= rhs) ? 1 : 0;
        }
    }
    if (p < end) {
        if (p[0] == '<' && !(p + 1 < end && p[1] == '<')) {
            p++;
            long long rhs = evalExprAtom(p, end, defines);
            return (lhs < rhs) ? 1 : 0;
        }
        if (p[0] == '>' && !(p + 1 < end && p[1] == '>')) {
            p++;
            long long rhs = evalExprAtom(p, end, defines);
            return (lhs > rhs) ? 1 : 0;
        }
    }
    return lhs;
}

long long CppPreprocessor::evalExprAtom(const u8*& p, const u8* end,
                                         const Map<String, MacroDef>& defines) {
    skipWhitespace(p, end);
    if (p >= end) return 0;

    // Negation
    if (*p == '!') {
        p++;
        return evalExprAtom(p, end, defines) ? 0 : 1;
    }

    // Parentheses
    if (*p == '(') {
        p++;
        long long val = evalExprOr(p, end, defines);
        skipWhitespace(p, end);
        if (p < end && *p == ')') p++;
        return val;
    }

    // Number
    if (isDigit(*p)) {
        long long val = 0;
        // Handle 0x prefix
        if (*p == '0' && p + 1 < end && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            while (p < end) {
                u8 c = *p;
                if (c >= '0' && c <= '9') { val = val * 16 + (c - '0'); p++; }
                else if (c >= 'a' && c <= 'f') { val = val * 16 + (c - 'a' + 10); p++; }
                else if (c >= 'A' && c <= 'F') { val = val * 16 + (c - 'A' + 10); p++; }
                else break;
            }
        } else {
            while (p < end && isDigit(*p)) {
                val = val * 10 + (*p - '0');
                p++;
            }
        }
        // Skip L/LL/U suffixes
        while (p < end && (*p == 'L' || *p == 'l' || *p == 'U' || *p == 'u')) p++;
        return val;
    }

    // Identifier: "defined" or macro name
    if (isAlpha(*p)) {
        String ident = readIdentifier(p, end);
        if (ident == "defined") {
            skipWhitespace(p, end);
            bool hasParen = false;
            if (p < end && *p == '(') { hasParen = true; p++; }
            skipWhitespace(p, end);
            String macroName = readIdentifier(p, end);
            skipWhitespace(p, end);
            if (hasParen && p < end && *p == ')') p++;
            return defines.has(macroName) ? 1 : 0;
        }

        if (ident == "__has_include") {
            skipWhitespace(p, end);
            bool hasParen = false;
            if (p < end && *p == '(') { hasParen = true; p++; }
            skipWhitespace(p, end);
            
            u8 delim = 0;
            if (p < end && (*p == '<' || *p == '"')) {
                delim = *p;
                p++;
            }
            String headerName;
            while (p < end) {
                if (delim != 0 && *p == (delim == '<' ? '>' : '"')) {
                    p++;
                    break;
                }
                if (delim == 0 && (*p == ')' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
                    break;
                }
                headerName += (char)*p;
                p++;
            }
            skipWhitespace(p, end);
            if (hasParen && p < end && *p == ')') p++;
            
            bool found = false;
            struct stat st;
            if (::stat(headerName.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                found = true;
            } else {
                for (usz i = 0; i < t_currentSearchPaths.size(); ++i) {
                    String fullPath = t_currentSearchPaths[i] + "/" + headerName;
                    if (::stat(fullPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                        found = true;
                        break;
                    }
                }
            }
            return found ? 1 : 0;
        }

        // Look up macro value
        const MacroDef* def = defines.get(ident);
        if (def && !def->value.isEmpty()) {
            // Re-parse the macro value through our own evaluator
            // which handles 0x hex, suffixes, etc.
            const u8* vp = def->value.data();
            const u8* ve = vp + def->value.size();
            return evalExprAtom(vp, ve, defines);
        }
        return defines.has(ident) ? 1 : 0;
    }

    // Character literal 'x'
    if (*p == '\'') {
        p++;
        long long val = 0;
        if (p < end && *p != '\'') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                switch (*p) {
                    case 'n': val = '\n'; break;
                    case 't': val = '\t'; break;
                    case 'r': val = '\r'; break;
                    case '0': val = '\0'; break;
                    case '\\': val = '\\'; break;
                    case '\'': val = '\''; break;
                    default: val = *p; break;
                }
                p++;
            } else {
                val = *p;
                p++;
            }
        }
        if (p < end && *p == '\'') p++;
        return val;
    }

    return 0;
}

bool CppPreprocessor::handleIf(const String& expr, const Map<String, MacroDef>& defines) {
    return evalExpr(expr, defines) != 0;
}

// ─── Path Resolution ────────────────────────────────────────────────────

String CppPreprocessor::dirOf(const String& path) const {
    long long lastSlash = path.indexOf('/');
    long long pos = lastSlash;
    for (usz i = 0; i < path.size(); ++i) {
        if (path.data()[i] == '/') pos = (long long)i;
    }
    if (pos < 0) return ".";
    return path.substring(0, (usz)pos);
}

String CppPreprocessor::extOf(const String& path) const {
    long long lastDot = -1;
    for (usz i = 0; i < path.size(); ++i) {
        if (path.data()[i] == '.') lastDot = (long long)i;
    }
    if (lastDot < 0) return "";
    return path.substring((usz)lastDot);
}

String CppPreprocessor::replaceInPath(const String& path, const String& from, const String& to) const {
    return path.replace(from, to);
}

static void scanSubdirsSingle(const String& path, Array<String>& out) {
    struct stat st;
    if (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        out.push(path);
        DIR* dir = ::opendir(path.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = ::readdir(dir)) != nullptr) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                String child = path + "/" + entry->d_name;
                struct stat childSt;
                if (::stat(child.c_str(), &childSt) == 0 && S_ISDIR(childSt.st_mode)) {
                    out.push(child);
                }
            }
            ::closedir(dir);
        }
    }
}

static void scanSubdirsRec(const String& path, const String& targetName, Array<String>& out, int depth = 0) {
    if (depth > 3) return;
    DIR* dir = ::opendir(path.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (strcmp(entry->d_name, ".git") == 0 || strcmp(entry->d_name, "build") == 0 || strcmp(entry->d_name, ".sew") == 0) continue;
        
        String child = path + "/" + entry->d_name;
        struct stat st;
        if (::stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            if (strcmp(entry->d_name, targetName.c_str()) == 0) {
                out.push(child);
                DIR* subDir = ::opendir(child.c_str());
                if (subDir) {
                    struct dirent* subEntry;
                    while ((subEntry = ::readdir(subDir)) != nullptr) {
                        if (strcmp(subEntry->d_name, ".") == 0 || strcmp(subEntry->d_name, "..") == 0) continue;
                        String subChild = child + "/" + subEntry->d_name;
                        struct stat subSt;
                        if (::stat(subChild.c_str(), &subSt) == 0 && S_ISDIR(subSt.st_mode)) {
                            out.push(subChild);
                        }
                    }
                    ::closedir(subDir);
                }
            }
            scanSubdirsRec(child, targetName, out, depth + 1);
        }
    }
    ::closedir(dir);
}

static Map<String, Array<String>>& getWildcardCache() {
    static Map<String, Array<String>> cache;
    return cache;
}

Array<String> CppPreprocessor::getSearchPaths(const String& currentFile) {
    std::lock_guard<std::mutex> lock(s_wildcardMutex);
    Array<String> paths;
    paths.push(dirOf(currentFile));
    
    long long dilIdx = currentFile.indexOf("/deps/diligent/");
    if (dilIdx >= 0) {
        String dilRoot = currentFile.substring(0, (usz)dilIdx + 15);
        paths.push(dilRoot + "/ThirdParty/volk");
        paths.push(dilRoot + "/ThirdParty/SPIRV-Cross");
        paths.push(dilRoot + "/ThirdParty/Vulkan-Headers/include");
        paths.push(dilRoot + "/ThirdParty/xxHash");
        paths.push(dilRoot + "/ThirdParty/DirectXShaderCompiler");
        paths.push(dilRoot + "/ThirdParty/glslang");
    }
    
    String checkDir = dirOf(currentFile);
    while (!checkDir.isEmpty()) {
        checkDir = canonicalizePath(checkDir);
        if (checkDir.isEmpty()) break;
        
        Array<String>* cached = getWildcardCache().get(checkDir);
        if (cached) {
            for (usz i = 0; i < cached->size(); ++i) {
                paths.push((*cached)[i]);
            }
        } else {
            Array<String> levelPaths;
            
            // %%include/*
            scanSubdirsSingle(checkDir + "/include", levelPaths);
            
            // %%src/*
            scanSubdirsSingle(checkDir + "/src", levelPaths);
            
            // %%node_modules/**/include/* and src/*
            struct stat stNode;
            if (::stat((checkDir + "/node_modules").c_str(), &stNode) == 0 && S_ISDIR(stNode.st_mode)) {
                scanSubdirsRec(checkDir + "/node_modules", "include", levelPaths);
                scanSubdirsRec(checkDir + "/node_modules", "src", levelPaths);
            }
            
            // %%deps/**/include/* and src/* and interface/*
            struct stat stDeps;
            if (::stat((checkDir + "/deps").c_str(), &stDeps) == 0 && S_ISDIR(stDeps.st_mode)) {
                scanSubdirsSingle(checkDir + "/deps", levelPaths);
                scanSubdirsRec(checkDir + "/deps", "include", levelPaths);
                scanSubdirsRec(checkDir + "/deps", "src", levelPaths);
                scanSubdirsRec(checkDir + "/deps", "interface", levelPaths);
            }
            
            getWildcardCache().set(checkDir, levelPaths);
            for (usz i = 0; i < levelPaths.size(); ++i) {
                paths.push(levelPaths[i]);
            }
        }
        
        long long parentSlash = -1;
        for (usz i = 0; i < checkDir.size(); ++i) {
            if (checkDir.data()[i] == '/') parentSlash = (long long)i;
        }
        if (parentSlash > 0) {
            checkDir = checkDir.substring(0, (usz)parentSlash);
        } else if (parentSlash == 0) {
            checkDir = "/";
            if (getWildcardCache().get("/") == nullptr) {
                Array<String> rootPaths;
                scanSubdirsSingle("/include", rootPaths);
                scanSubdirsSingle("/src", rootPaths);
                getWildcardCache().set("/", rootPaths);
            }
            Array<String>* rootCached = getWildcardCache().get("/");
            if (rootCached) {
                for (usz i = 0; i < rootCached->size(); ++i) {
                    paths.push((*rootCached)[i]);
                }
            }
            break;
        } else {
            break;
        }
    }
    
    for (usz i = 0; i < includePaths.size(); ++i) {
        paths.push(includePaths[i]);
    }
    paths.push(getSewIncludePath());
    
    struct stat st;
    if (::stat("include", &st) == 0 && S_ISDIR(st.st_mode)) {
        paths.push("include");
    }
    
    const char* xicPath = getenv("SEW_XIC_INCLUDE");
    if (xicPath) {
        paths.push(xicPath);
    } else {
        const char* tryPaths[] = {
            "../xic/include",
            "/home/xi/Repo/xic/include",
            nullptr
        };
        for (int i = 0; tryPaths[i]; ++i) {
            struct stat stDir;
            if (::stat(tryPaths[i], &stDir) == 0 && S_ISDIR(stDir.st_mode)) {
                paths.push(tryPaths[i]);
                break;
            }
        }
    }
    
    const char* extraInclude = getenv("SEW_EXTRA_INCLUDE");
    if (extraInclude) {
        String extraStr(extraInclude);
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
    }
    return paths;
}

static String canonicalizePath(const String& path) {
    char* rp = ::realpath(path.c_str(), nullptr);
    if (rp) {
        String res(rp);
        free(rp);
        return res;
    }
    String absPath = path;
    if (!path.startsWith("/")) {
        char cwd[1024];
        if (::getcwd(cwd, sizeof(cwd))) {
            absPath = String(cwd) + "/" + path;
        }
    }
    Array<String> parts = absPath.split("/");
    Array<String> clean;
    for (usz i = 0; i < parts.size(); ++i) {
        if (parts[i] == "." || parts[i].isEmpty()) continue;
        if (parts[i] == "..") {
            if (clean.size() > 0 && clean[clean.size() - 1] != "..") {
                clean.pop();
            } else {
                clean.push("..");
            }
        } else {
            clean.push(parts[i]);
        }
    }
    String res;
    if (absPath.startsWith("/")) res += "/";
    for (usz i = 0; i < clean.size(); ++i) {
        if (i > 0) res += "/";
        res += clean[i];
    }
    return res;
}

String CppPreprocessor::resolveIncludePath(const String& specifier, const String& currentFile) {
    if (specifier.startsWith("/")) {
        return canonicalizePath(specifier);
    }

    Array<String> paths = getSearchPaths(currentFile);
    for (usz i = 0; i < paths.size(); ++i) {
        String candidate = paths[i];
        if (!candidate.endsWith("/")) {
            candidate += "/";
        }
        candidate += specifier;
        candidate = canonicalizePath(candidate);
        struct stat st;
        if (::stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            return candidate;
        }
    }
    return "";
}

static String getDirOf(const String& path) {
    long long lastSlash = -1;
    for (usz i = 0; i < path.size(); ++i) {
        if (path.data()[i] == '/') lastSlash = (long long)i;
    }
    if (lastSlash >= 0) {
        return path.substring(0, (usz)lastSlash);
    }
    return ".";
}

static String getBaseName(const String& path) {
    long long lastSlash = -1;
    for (usz i = 0; i < path.size(); ++i) {
        if (path.data()[i] == '/') lastSlash = (long long)i;
    }
    String name = (lastSlash >= 0) ? path.substring((usz)lastSlash + 1) : path;
    long long dot = name.indexOf(".");
    if (dot >= 0) {
        return name.substring(0, (usz)dot);
    }
    return name;
}

static void scanForFile(const String& dirPath, const String& targetBase, Array<String>& results, int depth) {
    if (depth > 3) return; // Limit depth to 3 levels to keep it fast
    DIR* dir = ::opendir(dirPath.c_str());
    if (!dir) return;
    struct dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        String name(entry->d_name);
        if (name == "." || name == "..") continue;
        String fullPath = dirPath + "/" + name;
        bool isDir = (entry->d_type == DT_DIR);
        if (entry->d_type == DT_UNKNOWN) {
            struct stat st;
            if (::stat(fullPath.c_str(), &st) == 0) {
                isDir = S_ISDIR(st.st_mode);
            }
        }
        if (isDir) {
            if (name != ".git" && name != "build" && name != ".sew" &&
                name != "test" && name != "tests" && name != "gtests" &&
                name != "samples" && name != "demo" && name != "demos") {
                scanForFile(fullPath, targetBase, results, depth + 1);
            }
        } else {
            if (name.startsWith(targetBase) && 
                (name.endsWith(".cpp") || name.endsWith(".c") || name.endsWith(".cc") || name.endsWith(".cxx"))) {
                long long dot = name.indexOf(".");
                if (dot >= 0 && name.substring(0, (usz)dot) == targetBase) {
                    // Avoid duplicates
                    bool exists = false;
                    for (usz k = 0; k < results.size(); ++k) {
                        if (results[k] == fullPath) { exists = true; break; }
                    }
                    if (!exists) results.push(fullPath);
                }
            }
        }
    }
    ::closedir(dir);
}

Array<String> CppPreprocessor::findSiblingSourceFiles(const String& headerPath) {
    Array<String> siblings;
    if (headerPath.includes("SPIRV-Tools") || 
        headerPath.includes("SPIRV-Headers") || 
        headerPath.includes("DirectXShaderCompiler")) {
        return siblings;
    }

    if (headerPath.endsWith("DXCompiler.hpp")) {
        siblings.push(replaceInPath(headerPath, "include/DXCompiler.hpp", "src/DXILUtilsStub.cpp"));
        return siblings;
    }

    String ext = extOf(headerPath);
    if (ext != ".h" && ext != ".hpp" && ext != ".hxx") return siblings;

    // If the header is part of a third-party dependency, scan its entire directory for implementation files
    if (headerPath.includes("/deps/") || headerPath.includes("/ThirdParty/") || headerPath.includes("/diligent/") || headerPath.includes("/glfw/")) {
        // Exclude bypassed compiler libraries
        if (!headerPath.includes("SPIRV-Tools") && 
            !headerPath.includes("SPIRV-Headers") &&
            !headerPath.includes("DirectXShaderCompiler")) {
            String dir = getDirOf(headerPath);
            DIR* d = ::opendir(dir.c_str());
            if (d) {
                struct dirent* entry;
                while ((entry = ::readdir(d)) != nullptr) {
                    String name(entry->d_name);
                    if (name == "." || name == "..") continue;
                    String fullPath = dir + "/" + name;
                    bool isFile = (entry->d_type == DT_REG);
                    if (entry->d_type == DT_UNKNOWN) {
                        struct stat st;
                        if (::stat(fullPath.c_str(), &st) == 0) {
                            isFile = S_ISREG(st.st_mode);
                        }
                    }
                    if (isFile) {
                        if (name.endsWith(".cpp") || name.endsWith(".c") || name.endsWith(".cc") || name.endsWith(".cxx")) {
                            if (name != "main.cpp" && name != "main.c" && name != "main.cc" && name != "main.cxx") {
                                bool exists = false;
                                for (usz k = 0; k < siblings.size(); ++k) {
                                    if (siblings[k] == fullPath) { exists = true; break; }
                                }
                                if (!exists) siblings.push(fullPath);
                            }
                        }
                    }
                }
                ::closedir(d);
            }

            // Also scan GenericCodeGen directory when compiling glslang
            if (dir.includes("/glslang/")) {
                long long idx = dir.indexOf("/glslang/");
                if (idx >= 0) {
                    String root = dir.substring(0, (usz)idx + 9);
                    String genDir = root + "glslang/GenericCodeGen";
                    DIR* genD = ::opendir(genDir.c_str());
                    if (genD) {
                        struct dirent* entry;
                        while ((entry = ::readdir(genD)) != nullptr) {
                            String name(entry->d_name);
                            if (name == "." || name == "..") continue;
                            String fullPath = genDir + "/" + name;
                            if (name.endsWith(".cpp") || name.endsWith(".c")) {
                                bool exists = false;
                                for (usz k = 0; k < siblings.size(); ++k) {
                                    if (siblings[k] == fullPath) { exists = true; break; }
                                }
                                if (!exists) siblings.push(fullPath);
                            }
                        }
                        ::closedir(genD);
                    }
                }
            }
        }
    }

    // Generic recursive name-matching sibling lookup
    if (!headerPath.includes("SPIRV-Tools") && 
        !headerPath.includes("SPIRV-Headers") &&
        !headerPath.includes("DirectXShaderCompiler")) {
        String currentDir = getDirOf(headerPath);
        String targetBase = getBaseName(headerPath);
        for (int lvl = 0; lvl < 3; ++lvl) {
            if (currentDir.isEmpty() || currentDir == "/" || currentDir == "/home" || currentDir == "/home/xi" || currentDir == "/home/xi/Repo") break;
            if (currentDir.endsWith("/ThirdParty") || currentDir.endsWith("/deps") || currentDir.endsWith("/diligent") || currentDir.endsWith("/glfw")) break;
            scanForFile(currentDir, targetBase, siblings, 0);
            currentDir = getDirOf(currentDir);
        }
        // if (targetBase == "ShaderLang") {
        //     ::printf("DEBUG: findSiblingSourceFiles for ShaderLang.h (path: %s) found %d siblings:\n", headerPath.c_str(), (int)siblings.size());
        //     for (usz s = 0; s < siblings.size(); ++s) {
        //         ::printf("  -> %s\n", siblings[s].c_str());
        //     }
        //     ::fflush(stdout);
        // }
    }

    // Strip extension
    String base = headerPath.substring(0, headerPath.size() - ext.size());

    // Try direct siblings
    const char* exts[] = { ".cpp", ".c", ".cc", ".cxx" };
    for (int i = 0; i < 4; ++i) {
        String candidate = base;
        candidate += exts[i];
        siblings.push(candidate);
    }

    // Try include/ → src/ rewrite
    long long includePos = -1;
    String includeStr = "/include/";
    for (usz i = 0; i + includeStr.size() <= headerPath.size(); ++i) {
        bool match = true;
        for (usz j = 0; j < includeStr.size() && match; ++j) {
            if (headerPath.data()[i + j] != includeStr.data()[j]) match = false;
        }
        if (match) { includePos = (long long)i; break; }
    }

    if (includePos >= 0) {
        String srcBase = replaceInPath(base, "/include/", "/src/");
        for (int i = 0; i < 4; ++i) {
            String candidate = srcBase;
            candidate += exts[i];
            siblings.push(candidate);
        }
    }

    // Try interface/ → src/ rewrite
    long long interfacePos = -1;
    String interfaceStr = "/interface/";
    for (usz i = 0; i + interfaceStr.size() <= headerPath.size(); ++i) {
        bool match = true;
        for (usz j = 0; j < interfaceStr.size() && match; ++j) {
            if (headerPath.data()[i + j] != interfaceStr.data()[j]) match = false;
        }
        if (match) { interfacePos = (long long)i; break; }
    }

    if (interfacePos >= 0) {
        String srcBase = replaceInPath(base, "/interface/", "/src/");
        for (int i = 0; i < 4; ++i) {
            String candidate = srcBase;
            candidate += exts[i];
            siblings.push(candidate);
        }
    }

    if (headerPath.endsWith("Impl.hpp")) {
        String baseBase = replaceInPath(base, "Impl", "Base");
        if (interfacePos >= 0) {
            String srcBase = replaceInPath(baseBase, "/interface/", "/src/");
            for (int i = 0; i < 4; ++i) {
                siblings.push(srcBase + exts[i]);
            }
        } else if (includePos >= 0) {
            String srcBase = replaceInPath(baseBase, "/include/", "/src/");
            for (int i = 0; i < 4; ++i) {
                siblings.push(srcBase + exts[i]);
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                siblings.push(baseBase + exts[i]);
            }
        }
    }

    if (headerPath.includes("/deps/glfw/include/GLFW/glfw3.h")) {
        long long idx = headerPath.indexOf("/deps/glfw/");
        if (idx >= 0) {
            String glfwRoot = headerPath.substring(0, (usz)idx + 10);
            String srcDir = glfwRoot + "/src";
            const char* glfwSrcs[] = {
                "context.c", "init.c", "input.c", "monitor.c", "platform.c", "vulkan.c", "window.c",
                "egl_context.c", "osmesa_context.c", "posix_module.c", "posix_time.c", "posix_thread.c",
                "x11_init.c", "x11_monitor.c", "x11_window.c", "xkb_unicode.c", "glx_context.c", "linux_joystick.c",
                "null_init.c", "null_monitor.c", "null_window.c", "null_joystick.c", "posix_poll.c",
                nullptr
            };
            for (int i = 0; glfwSrcs[i]; ++i) {
                siblings.push(srcDir + "/" + glfwSrcs[i]);
            }
        }
    }

    if (headerPath.includes("/deps/diligent/Platforms/interface/")) {
        long long idx = headerPath.indexOf("/deps/diligent/");
        if (idx >= 0) {
            String dilRoot = headerPath.substring(0, (usz)idx + 15);
            String linuxSrcDir = dilRoot + "/Platforms/Linux/src";
            siblings.push(linuxSrcDir + "/LinuxDebug.cpp");
            siblings.push(linuxSrcDir + "/LinuxFileSystem.cpp");
            siblings.push(linuxSrcDir + "/LinuxPlatformMisc.cpp");
        }
    }

    if (headerPath.includes("/ThirdParty/SPIRV-Cross/")) {
        long long idx = headerPath.indexOf("/deps/diligent/");
        if (idx >= 0) {
            String dilRoot = headerPath.substring(0, (usz)idx + 15);
            String crossDir = dilRoot + "/ThirdParty/SPIRV-Cross";
            const char* crossSrcs[] = {
                "spirv_cfg.cpp", "spirv_cpp.cpp", "spirv_cross.cpp", "spirv_cross_c.cpp",
                "spirv_cross_parsed_ir.cpp", "spirv_cross_util.cpp", "spirv_glsl.cpp",
                "spirv_hlsl.cpp", "spirv_msl.cpp", "spirv_parser.cpp", "spirv_reflect.cpp",
                nullptr
            };
            for (int i = 0; crossSrcs[i]; ++i) {
                siblings.push(crossDir + "/" + crossSrcs[i]);
            }
        }
    }



    return siblings;
}

// ─── Main Processing Loop ───────────────────────────────────────────────
PreprocessorResult CppPreprocessor::process(const String& source, const String& filePath) {
    t_currentSearchPaths = getSearchPaths(filePath);
    PreprocessorResult result;
    result.defines = predefined; // Start with pre-seeded defines
    MacroDef defHasInclude;
    defHasInclude.name = "__has_include";
    defHasInclude.value = "1";
    result.defines.set("__has_include", defHasInclude);

    Array<CondFrame> condStack;
    String strippedSource;

    // Split source into lines
    Array<String> lines = source.split("\n");

    for (usz lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        const String& rawLine = lines[lineIdx];
        String trimmed = rawLine.trim();
        bool active = allActive(condStack);

        // Check if this is a preprocessor directive
        if (trimmed.size() > 0 && trimmed.data()[0] == '#') {
            Directive dir = parseLine(trimmed);

            // --- Conditional directives always processed ---
            if (dir.type == "ifdef") {
                CondFrame frame;
                if (active) {
                    frame.active = handleIfdef(dir.args, result.defines);
                    frame.branchTaken = frame.active;
                } else {
                    frame.active = false;
                    frame.branchTaken = false;
                }
                frame.wasElse = false;
                condStack.push(Xi::Move(frame));
                continue; // Strip
            }
            if (dir.type == "ifndef") {
                CondFrame frame;
                if (active) {
                    frame.active = handleIfndef(dir.args, result.defines);
                    frame.branchTaken = frame.active;
                } else {
                    frame.active = false;
                    frame.branchTaken = false;
                }
                frame.wasElse = false;
                condStack.push(Xi::Move(frame));
                continue;
            }
            if (dir.type == "if") {
                CondFrame frame;
                if (active) {
                    frame.active = handleIf(dir.args, result.defines);
                    frame.branchTaken = frame.active;
                } else {
                    frame.active = false;
                    frame.branchTaken = false;
                }
                frame.wasElse = false;
                condStack.push(Xi::Move(frame));
                continue;
            }
            if (dir.type == "elif") {
                if (condStack.size() > 0) {
                    CondFrame& top = condStack[condStack.size() - 1];
                    // Check parent frames are active
                    bool parentActive = true;
                    for (usz i = 0; i + 1 < condStack.size(); ++i) {
                        if (!condStack[i].active) { parentActive = false; break; }
                    }
                    if (parentActive && !top.branchTaken) {
                        top.active = handleIf(dir.args, result.defines);
                        if (top.active) top.branchTaken = true;
                    } else {
                        top.active = false;
                    }
                }
                continue;
            }
            if (dir.type == "else") {
                if (condStack.size() > 0) {
                    CondFrame& top = condStack[condStack.size() - 1];
                    bool parentActive = true;
                    for (usz i = 0; i + 1 < condStack.size(); ++i) {
                        if (!condStack[i].active) { parentActive = false; break; }
                    }
                    if (parentActive && !top.branchTaken) {
                        top.active = true;
                        top.branchTaken = true;
                    } else {
                        top.active = false;
                    }
                    top.wasElse = true;
                }
                continue;
            }
            if (dir.type == "endif") {
                if (condStack.size() > 0)
                    condStack.pop();
                continue;
            }

            // --- Other directives only processed in active branches ---
            if (!active) continue;

            if (dir.type == "include") {
                String args = dir.args.trim();
                if (args.size() < 2) continue;

                u8 first = args.data()[0];
                if (first == '"') {
                    // Local include
                    long long closeQuote = args.indexOf('"', 1);
                    if (closeQuote > 0) {
                        String spec = args.substring(1, (usz)closeQuote);
                        String resolved = resolveIncludePath(spec, filePath);
                        if (resolved.isEmpty()) {
                            // Fallback to relative path if not resolved (e.g. in tests)
                            resolved = dirOf(filePath);
                            if (!resolved.endsWith("/")) resolved += "/";
                            resolved += spec;
                        }
                        result.localIncludes.push(resolved);

                        // Check for sibling source files
                        Array<String> siblings = findSiblingSourceFiles(resolved);
                        for (usz s = 0; s < siblings.size(); ++s) {
                            result.siblingSourceFiles.push(siblings[s]);
                        }
                    }
                } else if (first == '<') {
                    // System/bracket include
                    long long closeAngle = args.indexOf('>');
                    if (closeAngle > 0) {
                        String spec = args.substring(1, (usz)closeAngle);
                        String resolved = resolveIncludePath(spec, filePath);
                        if (!resolved.isEmpty()) {
                            // If resolved, treat it as a local include!
                            result.localIncludes.push(resolved);

                            // Check for sibling source files
                            Array<String> siblings = findSiblingSourceFiles(resolved);
                            for (usz s = 0; s < siblings.size(); ++s) {
                                result.siblingSourceFiles.push(siblings[s]);
                            }
                        } else {
                            // True system header (e.g. <vector>, <cstdio>), pass to clang
                            result.systemIncludes.push(spec);
                            // Keep this line in stripped output for clang
                            strippedSource += rawLine;
                            strippedSource += "\n";
                        }
                    }
                }
                continue;
            }

            if (dir.type == "define") {
                handleDefine(dir.args, result.defines);
                // KEEP #define in stripped output — clang needs it for expansion
                strippedSource += rawLine;
                strippedSource += "\n";
                continue;
            }

            if (dir.type == "undef") {
                handleUndef(dir.args, result.defines);
                // Keep for clang
                strippedSource += rawLine;
                strippedSource += "\n";
                continue;
            }

            if (dir.type == "pragma") {
                String pragmaArgs = dir.args.trim();
                if (pragmaArgs == "once") {
                    std::lock_guard<std::mutex> lock(pragmaMutex);
                    if (pragmaOnceFiles.has(filePath)) {
                        // Already included — skip entire file
                        result.strippedSource = "";
                        return result;
                    }
                    pragmaOnceFiles.set(filePath, true);
                    continue; // Strip #pragma once
                }
                // Other pragmas — pass to clang
                strippedSource += rawLine;
                strippedSource += "\n";
                continue;
            }

            if (dir.type == "error") {
                String msg = dir.args.trim();
                // Remove quotes if present
                if (msg.size() >= 2 && msg.data()[0] == '"') {
                    msg = msg.substring(1, msg.size() - 1);
                }
                result.errors.push(msg);
                continue;
            }

            if (dir.type == "warning") {
                String msg = dir.args.trim();
                if (msg.size() >= 2 && msg.data()[0] == '"') {
                    msg = msg.substring(1, msg.size() - 1);
                }
                result.warnings.push(msg);
                continue;
            }

            // Unknown directive — pass through to clang
            strippedSource += rawLine;
            strippedSource += "\n";
            continue;
        }

        // Non-directive line
        if (active) {
            strippedSource += rawLine;
            strippedSource += "\n";
        }
    }

    result.strippedSource = Xi::Move(strippedSource);
    return result;
}

}} // namespace Sew::Languages
