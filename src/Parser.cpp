#include <Sew/Parser.hpp>
#include <cstdio>

namespace Sew {

enum class TokenType {
    Word,
    Symbol,
    DocComment,
    Other
};

struct Token {
    TokenType type;
    String text;
};

static bool isDeclSpec(const String& s) {
    if (s.isEmpty()) return false;
    for (usz i = 0; i < s.size(); ++i) {
        u8 c = s.data()[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

static Array<Token> tokenize(const String& content) {
    Array<Token> tokens;
    const u8* p = content.data();
    const u8* end = p + content.size();

    while (p < end) {
        if (*p <= ' ') {
            p++;
            continue;
        }

        if (*p == '#') {
            while (p < end && *p != '\n') p++;
            continue;
        }

        if (*p == '/' && p + 1 < end) {
            if (p[1] == '*') {
                const u8* start = p;
                p += 2;
                while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
                if (p + 1 < end) p += 2;
                String commentText(start, (usz)(p - start));
                if (commentText.startsWith("/**")) {
                    tokens.push({TokenType::DocComment, commentText});
                }
                continue;
            } else if (p[1] == '/') {
                const u8* start = p;
                p += 2;
                while (p < end && *p != '\n') p++;
                String commentText(start, (usz)(p - start));
                if (commentText.startsWith("///") || commentText.startsWith("// ")) {
                    tokens.push({TokenType::DocComment, commentText});
                }
                continue;
            }
        }

        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            const u8* start = p;
            while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) {
                p++;
            }
            tokens.push({TokenType::Word, String(start, (usz)(p - start))});
            continue;
        }

        if (*p == '{' || *p == '}' || *p == '(' || *p == ')' || *p == ';' || *p == ':' || *p == ',' || *p == '*' || *p == '&' || *p == '<' || *p == '>' || *p == '=' || *p == '~') {
            const u8* start = p;
            p++;
            if (start[0] == ':' && p < end && *p == ':') {
                p++;
            } else if (start[0] == '-' && p < end && *p == '>') {
                p++;
            }
            tokens.push({TokenType::Symbol, String(start, (usz)(p - start))});
            continue;
        }

        p++;
    }
    return tokens;
}

static void skipMatchingBraces(const Array<Token>& tokens, usz& idx) {
    int level = 0;
    while (idx < tokens.size()) {
        if (tokens[idx].text == "{") {
            level++;
        } else if (tokens[idx].text == "}") {
            level--;
            if (level == 0) {
                idx++;
                return;
            }
        }
        idx++;
    }
}

String CppHeaderParser::cleanDocComment(const String& rawComment) {
    if (rawComment.isEmpty()) return "";
    Array<String> lines = rawComment.split("\n");
    String clean;
    for (usz i = 0; i < lines.size(); ++i) {
        String line = lines[i].trim();
        if (line.startsWith("/**")) line = line.substring(3);
        if (line.endsWith("*/")) line = line.substring(0, line.length() - 2);
        if (line.startsWith("*")) line = line.substring(1);
        if (line.startsWith("///")) line = line.substring(3);
        if (line.startsWith("//")) line = line.substring(2);
        line = line.trim();
        if (!line.isEmpty()) {
            if (!clean.isEmpty()) clean += "\n";
            clean += line;
        }
    }
    return clean;
}

void CppHeaderParser::parse(const String& content) {
    Array<Token> tokens = tokenize(content);
    String lastDocComment;

    Array<String> namespaceStack;
    Array<int> namespaceBraceLevels;
    int globalBraceLevel = 0;

    usz idx = 0;
    while (idx < tokens.size()) {
        const Token& t = tokens[idx];

        if (t.type == TokenType::Word && t.text == "namespace") {
            idx++; // skip namespace
            String nsName;
            while (idx < tokens.size() && tokens[idx].text != "{" && tokens[idx].text != "=" && tokens[idx].text != ";") {
                if (tokens[idx].type == TokenType::Word || tokens[idx].text == "::") {
                    nsName += tokens[idx].text;
                }
                idx++;
            }
            if (idx < tokens.size() && tokens[idx].text == "{") {
                idx++; // skip "{"
                globalBraceLevel++;
                namespaceStack.push(nsName);
                namespaceBraceLevels.push(globalBraceLevel);

                bool exists = false;
                for (usz nsIdx = 0; nsIdx < namespaces.size(); ++nsIdx) {
                    if (namespaces[nsIdx] == nsName) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    namespaces.push(nsName);
                }
            } else {
                while (idx < tokens.size() && tokens[idx].text != ";") {
                    idx++;
                }
                if (idx < tokens.size()) idx++; // skip ";"
            }
            lastDocComment.clear();
            continue;
        }

        if (t.text == "{") {
            globalBraceLevel++;
            idx++;
            continue;
        }

        if (t.text == "}") {
            if (namespaceBraceLevels.size() > 0 && globalBraceLevel == namespaceBraceLevels[namespaceBraceLevels.size() - 1]) {
                namespaceStack.pop();
                namespaceBraceLevels.pop();
            }
            globalBraceLevel--;
            idx++;
            continue;
        }

        if (t.type == TokenType::DocComment) {
            lastDocComment = t.text;
            idx++;
            continue;
        }

        if (t.type == TokenType::Word && (t.text == "using" || t.text == "typedef")) {
            while (idx < tokens.size() && tokens[idx].text != ";") {
                idx++;
            }
            if (idx < tokens.size()) idx++; // skip ";"
            lastDocComment.clear();
            continue;
        }

        if (t.type == TokenType::Word && t.text == "template") {
            idx++; // skip "template"
            if (idx < tokens.size() && tokens[idx].text == "<") {
                int angleLevel = 0;
                while (idx < tokens.size()) {
                    if (tokens[idx].text == "<") angleLevel++;
                    else if (tokens[idx].text == ">") {
                        angleLevel--;
                        if (angleLevel == 0) {
                            idx++;
                            break;
                        }
                    }
                    idx++;
                }
            }
            // Skip the declaration following the template
            while (idx < tokens.size()) {
                if (tokens[idx].text == ";") {
                    idx++;
                    break;
                }
                if (tokens[idx].text == "{") {
                    skipMatchingBraces(tokens, idx);
                    if (idx < tokens.size() && tokens[idx].text == ";") {
                        idx++;
                    }
                    break;
                }
                idx++;
            }
            lastDocComment.clear();
            continue;
        }

        if (t.type == TokenType::Word && (t.text == "class" || t.text == "struct")) {
            String classOrStruct = t.text;
            idx++;

            // Skip declspec macros
            while (idx < tokens.size() && tokens[idx].type == TokenType::Word && isDeclSpec(tokens[idx].text) && tokens[idx].text != "class" && tokens[idx].text != "struct") {
                idx++;
            }

            if (idx >= tokens.size()) break;
            String className = tokens[idx].text;
            idx++;

            // Parse parents
            Array<String> parents;
            if (idx < tokens.size() && tokens[idx].text == ":") {
                idx++;
                while (idx < tokens.size() && tokens[idx].text != "{" && tokens[idx].text != ";") {
                    if (tokens[idx].type == TokenType::Word && tokens[idx].text != "public" && tokens[idx].text != "protected" && tokens[idx].text != "private") {
                        parents.push(tokens[idx].text);
                    }
                    idx++;
                }
            }

            if (idx < tokens.size() && tokens[idx].text == ";") {
                // Forward declaration, skip
                idx++;
                continue;
            }

            if (idx < tokens.size() && tokens[idx].text == "{") {
                idx++; // skip '{'
                ParsedClass cls;
                String fullName;
                for (usz nsIdx = 0; nsIdx < namespaceStack.size(); ++nsIdx) {
                    fullName += namespaceStack[nsIdx] + "::";
                }
                fullName += className;
                cls.name = fullName;
                cls.isStruct = (classOrStruct == "struct");
                cls.docComment = cleanDocComment(lastDocComment);
                cls.parentClasses = parents;
                lastDocComment.clear();

                classes.push(Xi::Move(cls));
                ParsedClass* currentClass = &classes[classes.size() - 1];

                bool isPublic = cls.isStruct;
                int braceLevel = 1;

                while (idx < tokens.size() && braceLevel > 0) {
                    if (tokens[idx].text == "{") {
                        braceLevel++;
                        idx++;
                        continue;
                    }
                    if (tokens[idx].text == "}") {
                        braceLevel--;
                        idx++;
                        continue;
                    }

                    if (braceLevel == 1) {
                        if (tokens[idx].type == TokenType::Word && (tokens[idx].text == "public" || tokens[idx].text == "private" || tokens[idx].text == "protected")) {
                            String access = tokens[idx].text;
                            idx++;
                            if (idx < tokens.size() && tokens[idx].text == ":") {
                                idx++;
                                isPublic = (access == "public");
                            }
                            continue;
                        }

                        if (tokens[idx].type == TokenType::DocComment) {
                            lastDocComment = tokens[idx].text;
                            idx++;
                            continue;
                        }

                        // Collect member declaration tokens
                        Array<Token> declTokens;
                        while (idx < tokens.size() && tokens[idx].text != ";" && tokens[idx].text != "{" && tokens[idx].text != "}") {
                            declTokens.push(tokens[idx]);
                            idx++;
                        }

                        if (declTokens.size() > 0 && declTokens[0].text == "template") {
                            if (idx < tokens.size() && tokens[idx].text == "{") {
                                skipMatchingBraces(tokens, idx);
                            } else if (idx < tokens.size() && tokens[idx].text == ";") {
                                idx++;
                            }
                            lastDocComment.clear();
                            continue;
                        }

                        if (declTokens.size() > 0 && declTokens[0].text == "using") {
                            if (idx < tokens.size() && tokens[idx].text == ";") {
                                idx++;
                            }
                            lastDocComment.clear();
                            continue;
                        }

                        if (declTokens.size() > 0 && (declTokens[0].text == "class" || declTokens[0].text == "struct")) {
                            if (idx < tokens.size() && tokens[idx].text == "{") {
                                skipMatchingBraces(tokens, idx);
                                if (idx < tokens.size() && tokens[idx].text == ";") {
                                    idx++;
                                }
                            } else if (idx < tokens.size() && tokens[idx].text == ";") {
                                idx++;
                            }
                            lastDocComment.clear();
                            continue;
                        }

                        if (idx < tokens.size() && tokens[idx].text == "{") {
                            // Inline method body, skip it
                            skipMatchingBraces(tokens, idx);
                        } else if (idx < tokens.size() && tokens[idx].text == ";") {
                            idx++;
                        }

                        if (isPublic && declTokens.size() > 0) {
                            // Parse member declaration
                            bool isDestructor = false;
                            bool isConstructor = false;
                            long long parenIdx = -1;
                            long long tildeIdx = -1;

                            for (usz k = 0; k < declTokens.size(); ++k) {
                                if (declTokens[k].text == "(") {
                                    parenIdx = (long long)k;
                                    break;
                                }
                                if (declTokens[k].text == "~") {
                                    tildeIdx = (long long)k;
                                }
                            }

                            if (tildeIdx >= 0) {
                                isDestructor = true;
                            } else if (parenIdx >= 0) {
                                String maybeName = declTokens[(usz)parenIdx - 1].text;
                                if (maybeName == className) {
                                    isConstructor = true;
                                }
                            }

                            bool isOperator = false;
                            if (parenIdx >= 0) {
                                for (usz k = 0; k < (usz)parenIdx; ++k) {
                                    if (declTokens[k].text == "operator") {
                                        isOperator = true;
                                        break;
                                    }
                                }
                            }
                            if (isOperator) {
                                lastDocComment.clear();
                                continue;
                            }

                            if (isDestructor || isConstructor || parenIdx >= 0) {
                                // Method or Constructor/Destructor
                                ParsedMethod m;
                                m.docComment = cleanDocComment(lastDocComment);
                                lastDocComment.clear();
                                m.isConstructor = isConstructor;
                                m.isDestructor = isDestructor;

                                usz nameIdx = 0;
                                if (isDestructor) {
                                    m.name = "~" + className;
                                    nameIdx = (usz)tildeIdx;
                                } else if (isConstructor) {
                                    m.name = className;
                                    nameIdx = (usz)parenIdx - 1;
                                } else {
                                    m.name = declTokens[(usz)parenIdx - 1].text;
                                    if (parenIdx >= 2 && declTokens[(usz)parenIdx - 2].text == "operator") {
                                        m.name = "operator" + m.name;
                                        nameIdx = (usz)parenIdx - 2;
                                    } else {
                                        nameIdx = (usz)parenIdx - 1;
                                    }
                                }

                                // Return type (for non-constructor/destructor)
                                if (!isConstructor && !isDestructor) {
                                    for (usz k = 0; k < nameIdx; ++k) {
                                        if (declTokens[k].text == "static") {
                                            m.isStatic = true;
                                            continue;
                                        }
                                        if (declTokens[k].text == "virtual" || declTokens[k].text == "inline" || isDeclSpec(declTokens[k].text)) {
                                            continue;
                                        }
                                        if (m.returnType.length() > 0 && !m.returnType.endsWith("*") && !m.returnType.endsWith("&") && declTokens[k].text != "*" && declTokens[k].text != "&") {
                                            m.returnType += " ";
                                        }
                                        m.returnType += declTokens[k].text;
                                    }
                                }

                                // Modifiers after ')'
                                long long closeParenIdx = -1;
                                for (usz k = (usz)parenIdx; k < declTokens.size(); ++k) {
                                    if (declTokens[k].text == ")") {
                                        closeParenIdx = (long long)k;
                                        break;
                                    }
                                }

                                if (closeParenIdx >= 0) {
                                    for (usz k = (usz)closeParenIdx + 1; k < declTokens.size(); ++k) {
                                        if (declTokens[k].text == "const") {
                                            m.isConst = true;
                                        }
                                    }

                                    // Parse parameters
                                    Array<Array<Token>> paramGroups;
                                    Array<Token> currentParam;
                                    for (usz k = (usz)parenIdx + 1; k < (usz)closeParenIdx; ++k) {
                                        if (declTokens[k].text == ",") {
                                            if (currentParam.size() > 0) {
                                                paramGroups.push(currentParam);
                                                currentParam.clear();
                                            }
                                        } else {
                                            currentParam.push(declTokens[k]);
                                        }
                                    }
                                    if (currentParam.size() > 0) {
                                        paramGroups.push(currentParam);
                                    }

                                    for (usz k = 0; k < paramGroups.size(); ++k) {
                                        Array<Token>& pTokens = paramGroups[k];
                                        ParsedParam param;

                                        // Check for default value
                                        long long eqIdx = -1;
                                        for (usz j = 0; j < pTokens.size(); ++j) {
                                            if (pTokens[j].text == "=") {
                                                eqIdx = (long long)j;
                                                break;
                                            }
                                        }

                                        usz limit = pTokens.size();
                                        if (eqIdx >= 0) {
                                            limit = (usz)eqIdx;
                                            for (usz j = (usz)eqIdx + 1; j < pTokens.size(); ++j) {
                                                if (param.defaultValue.length() > 0) param.defaultValue += " ";
                                                param.defaultValue += pTokens[j].text;
                                            }
                                        }

                                        // Name is the last token before '=' or end of parameter, if it's not a type/symbol
                                        if (limit > 0) {
                                            String lastTokText = pTokens[limit - 1].text;
                                            bool nameOmitted = (lastTokText == "*" || lastTokText == "&" || lastTokText == "int" || lastTokText == "char" || lastTokText == "float" || lastTokText == "double" || lastTokText == "bool");
                                            if (nameOmitted) {
                                                param.name = "arg" + String((long long)k);
                                                for (usz j = 0; j < limit; ++j) {
                                                    if (param.type.length() > 0 && !param.type.endsWith("*") && !param.type.endsWith("&") && pTokens[j].text != "*" && pTokens[j].text != "&") {
                                                        param.type += " ";
                                                    }
                                                    param.type += pTokens[j].text;
                                                }
                                            } else {
                                                param.name = lastTokText;
                                                for (usz j = 0; j + 1 < limit; ++j) {
                                                    if (param.type.length() > 0 && !param.type.endsWith("*") && !param.type.endsWith("&") && pTokens[j].text != "*" && pTokens[j].text != "&") {
                                                        param.type += " ";
                                                    }
                                                    param.type += pTokens[j].text;
                                                }
                                            }
                                        }
                                        m.params.push(param);
                                    }
                                }

                                currentClass->methods.push(m);
                            } else {
                                // Field
                                ParsedField f;
                                f.docComment = cleanDocComment(lastDocComment);
                                lastDocComment.clear();

                                long long eqIdx = -1;
                                for (usz k = 0; k < declTokens.size(); ++k) {
                                    if (declTokens[k].text == "=") {
                                        eqIdx = (long long)k;
                                        break;
                                    }
                                }

                                usz limit = declTokens.size();
                                if (eqIdx >= 0) {
                                    limit = (usz)eqIdx;
                                }

                                if (limit > 0) {
                                    f.name = declTokens[limit - 1].text;
                                    for (usz k = 0; k + 1 < limit; ++k) {
                                        if (declTokens[k].text == "static") {
                                            f.isStatic = true;
                                            continue;
                                        }
                                        if (declTokens[k].text == "const" || declTokens[k].text == "constexpr") {
                                            f.isConst = true;
                                        }
                                        if (isDeclSpec(declTokens[k].text)) continue;
                                        if (f.type.length() > 0 && !f.type.endsWith("*") && !f.type.endsWith("&") && declTokens[k].text != "*" && declTokens[k].text != "&") {
                                            f.type += " ";
                                        }
                                        f.type += declTokens[k].text;
                                    }
                                }
                                currentClass->fields.push(f);
                            }
                        }
                    } else {
                        idx++;
                    }
                }
            } else {
                idx++;
            }
        } else {
            // Check for global function declaration
            if (t.type == TokenType::Word && lastDocComment.length() > 0) {
                // Peek ahead to see if it's a global function
                Array<Token> declTokens;
                usz tempIdx = idx;
                while (tempIdx < tokens.size() && tokens[tempIdx].text != ";" && tokens[tempIdx].text != "{") {
                    declTokens.push(tokens[tempIdx]);
                    tempIdx++;
                }

                if (tempIdx < tokens.size() && tokens[tempIdx].text == "{") {
                    // Skip matching body
                    skipMatchingBraces(tokens, tempIdx);
                } else if (tempIdx < tokens.size() && tokens[tempIdx].text == ";") {
                    tempIdx++;
                }

                bool isInvalidGlobal = false;
                fprintf(stderr, "=== PARSING GLOBAL DECLARATION ===\n");
                for (usz k = 0; k < declTokens.size(); ++k) {
                    fprintf(stderr, "  Token: '%s'\n", declTokens[k].text.c_str());
                    if (declTokens[k].text == "operator" || declTokens[k].text == "constexpr" || declTokens[k].text == "using" || declTokens[k].text == "typedef") {
                        isInvalidGlobal = true;
                    }
                }
                if (isInvalidGlobal) {
                    fprintf(stderr, "  => SKIPPED (invalid)\n");
                    lastDocComment.clear();
                    idx = tempIdx;
                    continue;
                }

                long long parenIdx = -1;
                for (usz k = 0; k < declTokens.size(); ++k) {
                    if (declTokens[k].text == "(") {
                        parenIdx = (long long)k;
                        break;
                    }
                }

                if (parenIdx >= 0) {
                    // Global Function
                    ParsedFunction fn;
                    fn.docComment = cleanDocComment(lastDocComment);
                    lastDocComment.clear();
                    String fullName;
                    for (usz nsIdx = 0; nsIdx < namespaceStack.size(); ++nsIdx) {
                        fullName += namespaceStack[nsIdx] + "::";
                    }
                    fullName += declTokens[(usz)parenIdx - 1].text;
                    fn.name = fullName;

                    usz nameIdx = (usz)parenIdx - 1;
                    for (usz k = 0; k < nameIdx; ++k) {
                        if (declTokens[k].text == "inline" || isDeclSpec(declTokens[k].text)) continue;
                        if (fn.returnType.length() > 0 && !fn.returnType.endsWith("*") && !fn.returnType.endsWith("&") && declTokens[k].text != "*" && declTokens[k].text != "&") {
                            fn.returnType += " ";
                        }
                        fn.returnType += declTokens[k].text;
                    }

                    // Parse parameters similarly...
                    long long closeParenIdx = -1;
                    for (usz k = (usz)parenIdx; k < declTokens.size(); ++k) {
                        if (declTokens[k].text == ")") {
                            closeParenIdx = (long long)k;
                            break;
                        }
                    }

                    if (closeParenIdx >= 0) {
                        Array<Array<Token>> paramGroups;
                        Array<Token> currentParam;
                        for (usz k = (usz)parenIdx + 1; k < (usz)closeParenIdx; ++k) {
                            if (declTokens[k].text == ",") {
                                if (currentParam.size() > 0) {
                                    paramGroups.push(currentParam);
                                    currentParam.clear();
                                }
                            } else {
                                currentParam.push(declTokens[k]);
                            }
                        }
                        if (currentParam.size() > 0) {
                            paramGroups.push(currentParam);
                        }

                        for (usz k = 0; k < paramGroups.size(); ++k) {
                            Array<Token>& pTokens = paramGroups[k];
                            ParsedParam param;
                            long long eqIdx = -1;
                            for (usz j = 0; j < pTokens.size(); ++j) {
                                if (pTokens[j].text == "=") {
                                    eqIdx = (long long)j;
                                    break;
                                }
                            }
                            usz limit = pTokens.size();
                            if (eqIdx >= 0) {
                                limit = (usz)eqIdx;
                                for (usz j = (usz)eqIdx + 1; j < pTokens.size(); ++j) {
                                    if (param.defaultValue.length() > 0) param.defaultValue += " ";
                                    param.defaultValue += pTokens[j].text;
                                }
                            }
                            if (limit > 0) {
                                String lastTokText = pTokens[limit - 1].text;
                                bool nameOmitted = (lastTokText == "*" || lastTokText == "&" || lastTokText == "int" || lastTokText == "char" || lastTokText == "float" || lastTokText == "double" || lastTokText == "bool");
                                if (nameOmitted) {
                                    param.name = "arg" + String((long long)k);
                                    for (usz j = 0; j < limit; ++j) {
                                        if (param.type.length() > 0 && !param.type.endsWith("*") && !param.type.endsWith("&") && pTokens[j].text != "*" && pTokens[j].text != "&") {
                                            param.type += " ";
                                        }
                                        param.type += pTokens[j].text;
                                    }
                                } else {
                                    param.name = lastTokText;
                                    for (usz j = 0; j + 1 < limit; ++j) {
                                        if (param.type.length() > 0 && !param.type.endsWith("*") && !param.type.endsWith("&") && pTokens[j].text != "*" && pTokens[j].text != "&") {
                                            param.type += " ";
                                        }
                                        param.type += pTokens[j].text;
                                    }
                                }
                            }
                            fn.params.push(param);
                        }
                    }

                    functions.push(fn);
                    idx = tempIdx;
                    continue;
                }
            }

            lastDocComment.clear();
            idx++;
        }
    }
}

} // namespace Sew
