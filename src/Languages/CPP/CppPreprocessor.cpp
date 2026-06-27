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

namespace Sew { namespace Languages {

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

Array<String> CppPreprocessor::getSearchPaths(const String& currentFile) {
    Array<String> paths;
    paths.push(dirOf(currentFile));
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
            struct stat st;
            if (::stat(tryPaths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
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

Array<String> CppPreprocessor::findSiblingSourceFiles(const String& headerPath) {
    Array<String> siblings;
    String ext = extOf(headerPath);
    if (ext != ".h" && ext != ".hpp" && ext != ".hxx") return siblings;

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

    return siblings;
}

// ─── Main Processing Loop ───────────────────────────────────────────────

PreprocessorResult CppPreprocessor::process(const String& source, const String& filePath) {
    PreprocessorResult result;
    result.defines = predefined; // Start with pre-seeded defines

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
