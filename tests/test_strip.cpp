#include <Collection/String.hpp>
#include <Collection/Array.hpp>
#include <iostream>
#include <cctype>

struct ParsedClass {
    Collection::String name;
};

static Collection::String replaceColons(const Collection::String& s) {
    Collection::String res;
    for (size_t i = 0; i < s.length(); ++i) {
        if (i + 1 < s.length() && s.data()[i] == ':' && s.data()[i+1] == ':') {
            res += "_";
            i++;
        } else {
            char c = (char)s.data()[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            } else if (c == '<' || c == '>' || c == ',' || c == '(' || c == ')' || c == '*' || c == '&') {
                res += "_";
            } else {
                res += c;
            }
        }
    }
    return res;
}

static Collection::String getJsName(const Collection::String& fullName) {
    long long lastColon = -1;
    for (size_t i = 0; i + 1 < fullName.length(); ++i) {
        if (fullName.data()[i] == ':' && fullName.data()[i+1] == ':') {
            lastColon = (long long)i;
        }
    }
    Collection::String name = (lastColon >= 0) ? fullName.substring((size_t)lastColon + 2) : fullName;
    long long anglePos = name.indexOf('<');
    if (anglePos >= 0) name = name.substring(0, (size_t)anglePos);
    return name;
}

static bool isKnownType(const Collection::String& typeName, const Collection::Array<ParsedClass>& classes) {
    Collection::String t = typeName.trim();
    std::cout << "[DEBUG] isKnownType check: '" << t.c_str() << "'" << std::endl;
    if (t == "number" || t == "string" || t == "boolean" || t == "bigint" || t == "any" || t == "void" || t == "symbol" || t == "Function" || t == "Symbol") {
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
        Collection::Array<Collection::String> parts = t.split("|");
        bool allKnown = true;
        for (size_t k = 0; k < parts.size(); ++k) {
            Collection::String p = parts[k].trim();
            if (p != "undefined" && p != "null" && !isKnownType(p, classes)) {
                allKnown = false;
                break;
            }
        }
        if (allKnown) return true;
    }
    for (size_t i = 0; i < classes.size(); ++i) {
        Collection::String clsName = getJsName(classes[i].name);
        if (t == clsName || t == classes[i].name || t == replaceColons(classes[i].name)) {
            return true;
        }
    }
    return false;
}

int main() {
    Collection::Array<ParsedClass> classes;
    ParsedClass cls;
    cls.name = "Xylem::LockState";
    classes.push(cls);

    Collection::String ts = "const obj: Record<number, LockState> = {} as Record<number, LockState>;\n";
    std::cout << "Original TS: " << ts.c_str();

    // Replace complex registry type annotation
    ts = ts.replace("info: { ptr: number, type: string }", "info");

    Collection::String js;
    size_t len = ts.length();
    int ternaryDepth = 0;
    bool inSlashComment = false;
    bool inBlockComment = false;
    char inString = 0;

    for (size_t i = 0; i < len; ++i) {
        char c = ts.data()[i];

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

        if (c == '?' && i + 1 < len && ts.data()[i+1] != '.' && ts.data()[i+1] != '?') {
            ternaryDepth++;
        }

        if (c == ':' && i + 1 < len && ts.data()[i+1] == ' ') {
            if (ternaryDepth > 0) {
                ternaryDepth--;
                js.push(':');
                continue;
            }

            size_t j = i + 2;
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
            if (j > i + 2 && j < len && (ts.data()[j] == ',' || ts.data()[j] == ')' || ts.data()[j] == '{' || ts.data()[j] == ';' || ts.data()[j] == '\n' || ts.data()[j] == '=')) {
                Collection::String typeName = ts.substring(i + 2, j).trim();
                std::cout << "[DEBUG] Colon match found: '" << typeName.c_str() << "', next char: '" << ts.data()[j] << "'" << std::endl;
                if (isKnownType(typeName, classes)) {
                    i = j - 1;
                    continue;
                }
            }
        }

        if (ts.substring(i, i + 4) == " as ") {
            std::cout << "[DEBUG] Found ' as ' at index " << i << std::endl;
            size_t j = i + 4;
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
                Collection::String typeName = ts.substring(i + 4, j).trim();
                std::cout << "[DEBUG] 'as' match typeName: '" << typeName.c_str() << "', next char: '" << ts.data()[j] << "'" << std::endl;
                if (isKnownType(typeName, classes)) {
                    i = j - 1;
                    continue;
                }
            }
        }

        js.push(c);
    }

    std::cout << "Result JS:   " << js.c_str() << std::endl;
    return 0;
}
