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
    if (s == "HNSW") return false;
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

        // Number literal (needed for detecting `= 0` in pure virtual declarations)
        if (*p >= '0' && *p <= '9') {
            const u8* start = p;
            while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'x' || *p == 'X' ||
                               (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F') ||
                               *p == 'u' || *p == 'U' || *p == 'l' || *p == 'L')) {
                p++;
            }
            tokens.push({TokenType::Word, String(start, (usz)(p - start))});
            continue;
        }

        if (*p == '{' || *p == '}' || *p == '(' || *p == ')' || *p == '[' || *p == ']' || *p == ';' || *p == ':' || *p == ',' || *p == '*' || *p == '&' || *p == '<' || *p == '>' || *p == '=' || *p == '~') {
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

            long long colonOrBraceIdx = -1;
            for (usz k = idx; k < tokens.size(); ++k) {
                if (tokens[k].text == ":" || tokens[k].text == "{" || tokens[k].text == ";") {
                    colonOrBraceIdx = (long long)k;
                    break;
                }
            }
            if (colonOrBraceIdx < 0) continue;

            long long nameIdx = -1;
            for (long long k = colonOrBraceIdx - 1; k >= (long long)idx; --k) {
                if (tokens[k].type == TokenType::Word) {
                    nameIdx = k;
                    break;
                }
            }
            if (nameIdx < 0) continue;

            String className = tokens[nameIdx].text;

            Array<String> parents;
            usz checkIdx = (usz)colonOrBraceIdx;
            if (tokens[checkIdx].text == ":") {
                checkIdx++;
                while (checkIdx < tokens.size() && tokens[checkIdx].text != "{" && tokens[checkIdx].text != ";") {
                    if (tokens[checkIdx].type == TokenType::Word && tokens[checkIdx].text != "public" && tokens[checkIdx].text != "protected" && tokens[checkIdx].text != "private") {
                        parents.push(tokens[checkIdx].text);
                    }
                    checkIdx++;
                }
            }

            idx = checkIdx;

            if (idx < tokens.size() && tokens[idx].text == ";") {
                idx++;
                continue;
            }

            if (idx < tokens.size() && tokens[idx].text == "{") {
                idx++;
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

                // Dedup: only push if no class with this name exists yet
                bool clsExists = false;
                for (usz ci = 0; ci < classes.size(); ++ci) {
                    if (classes[ci].name == fullName) { clsExists = true; break; }
                }
                if (clsExists) {
                    // Skip the entire class body
                    int skipBrace = 1;
                    while (idx < tokens.size() && skipBrace > 0) {
                        if (tokens[idx].text == "{") skipBrace++;
                        else if (tokens[idx].text == "}") skipBrace--;
                        idx++;
                    }
                    if (idx < tokens.size() && tokens[idx].text == ";") idx++;
                    continue;
                }
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
                        int parenLevel = 0;
                        while (idx < tokens.size()) {
                            const Token& tok = tokens[idx];
                            if (tok.text == "(") parenLevel++;
                            else if (tok.text == ")") parenLevel--;

                            if (parenLevel == 0 && (tok.text == ";" || tok.text == "{" || tok.text == "}")) {
                                break;
                            }
                            declTokens.push(tok);
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
                            bool isFriend = false;
                            for (usz k = 0; k < declTokens.size(); ++k) {
                                if (declTokens[k].text == "friend") {
                                    isFriend = true;
                                    break;
                                }
                            }
                            if (isFriend) {
                                lastDocComment.clear();
                                continue;
                            }
                            // Parse member declaration
                            bool isDestructor = false;
                            bool isConstructor = false;
                            long long parenIdx = -1;
                            long long tildeIdx = -1;

                            int angleLevel = 0;
                            for (usz k = 0; k < declTokens.size(); ++k) {
                                if (declTokens[k].text == "<") {
                                    angleLevel++;
                                } else if (declTokens[k].text == ">") {
                                    angleLevel--;
                                } else if (declTokens[k].text == "(" && angleLevel == 0) {
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
                                        if (declTokens[k].text == "virtual" || declTokens[k].text == "inline" || (isDeclSpec(declTokens[k].text) && !(k + 1 < nameIdx && declTokens[k + 1].text == "::"))) {
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
                                 int pLevel = 0;
                                 for (usz k = (usz)parenIdx; k < declTokens.size(); ++k) {
                                     if (declTokens[k].text == "(") pLevel++;
                                     else if (declTokens[k].text == ")") {
                                         pLevel--;
                                         if (pLevel == 0) {
                                             closeParenIdx = (long long)k;
                                             break;
                                         }
                                     }
                                 }

                                if (closeParenIdx >= 0) {
                                     for (usz k = (usz)closeParenIdx + 1; k < declTokens.size(); ++k) {
                                         if (declTokens[k].text == "const") {
                                             m.isConst = true;
                                         }
                                         if (declTokens[k].text == "=" && k + 1 < declTokens.size() && declTokens[k + 1].text == "0") {
                                             m.isPureVirtual = true;
                                         }
                                     }

                                    // Parse parameters
                                    Array<Array<Token>> paramGroups;
                                    Array<Token> currentParam;
                                    int angleLevel = 0;
                                    int parenLevel = 0;
                                    int bracketLevel = 0;
                                    for (usz k = (usz)parenIdx + 1; k < (usz)closeParenIdx; ++k) {
                                        const Token& tok = declTokens[k];
                                        if (tok.text == "<") angleLevel++;
                                        else if (tok.text == ">") angleLevel--;
                                        else if (tok.text == "(") parenLevel++;
                                        else if (tok.text == ")") parenLevel--;
                                        else if (tok.text == "[") bracketLevel++;
                                        else if (tok.text == "]") bracketLevel--;

                                        if (tok.text == "," && angleLevel == 0 && parenLevel == 0 && bracketLevel == 0) {
                                            if (currentParam.size() > 0) {
                                                 paramGroups.push(currentParam);
                                                 currentParam.clear();
                                            }
                                        } else {
                                            currentParam.push(tok);
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
                                            long long openBracketIdx = -1;
                                            if (pTokens[limit - 1].text == "]") {
                                                for (long long j = (long long)limit - 1; j >= 0; --j) {
                                                    if (pTokens[(usz)j].text == "[") {
                                                        openBracketIdx = j;
                                                        break;
                                                    }
                                                }
                                            }

                                            if (openBracketIdx > 0) {
                                                param.name = pTokens[(usz)openBracketIdx - 1].text;
                                                for (usz j = 0; j + 1 < (usz)openBracketIdx; ++j) {
                                                    if (param.type.length() > 0 && !param.type.endsWith("*") && !param.type.endsWith("&") && pTokens[j].text != "*" && pTokens[j].text != "&") {
                                                        param.type += " ";
                                                    }
                                                    param.type += pTokens[j].text;
                                                }
                                                if (!param.type.endsWith("*")) {
                                                    param.type += "*";
                                                }
                                            } else {
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
                                     long long openBracketIdx = -1;
                                     if (declTokens[limit - 1].text == "]") {
                                         for (long long j = (long long)limit - 1; j >= 0; --j) {
                                             if (declTokens[(usz)j].text == "[") {
                                                 openBracketIdx = j;
                                                 break;
                                             }
                                         }
                                     }

                                     if (openBracketIdx > 0) {
                                         f.name = declTokens[(usz)openBracketIdx - 1].text;
                                         for (usz k = 0; k + 1 < (usz)openBracketIdx; ++k) {
                                             if (declTokens[k].text == "static") {
                                                 f.isStatic = true;
                                                 continue;
                                             }
                                             if (declTokens[k].text == "const" || declTokens[k].text == "constexpr") {
                                                 f.isConst = true;
                                             }
                                             if (isDeclSpec(declTokens[k].text) && !(k + 1 < (usz)openBracketIdx && declTokens[k + 1].text == "::")) continue;
                                             if (f.type.length() > 0 && !f.type.endsWith("*") && !f.type.endsWith("&") && declTokens[k].text != "*" && declTokens[k].text != "&") {
                                                 f.type += " ";
                                             }
                                             f.type += declTokens[k].text;
                                         }
                                         for (usz k = (usz)openBracketIdx; k < limit; ++k) {
                                             f.type += declTokens[k].text;
                                         }
                                     } else {
                                         f.name = declTokens[limit - 1].text;
                                         for (usz k = 0; k + 1 < limit; ++k) {
                                             if (declTokens[k].text == "static") {
                                                 f.isStatic = true;
                                                 continue;
                                             }
                                             if (declTokens[k].text == "const" || declTokens[k].text == "constexpr") {
                                                 f.isConst = true;
                                             }
                                             if (isDeclSpec(declTokens[k].text) && !(k + 1 < limit && declTokens[k + 1].text == "::")) continue;
                                             if (f.type.length() > 0 && !f.type.endsWith("*") && !f.type.endsWith("&") && declTokens[k].text != "*" && declTokens[k].text != "&") {
                                                 f.type += " ";
                                             }
                                             f.type += declTokens[k].text;
                                         }
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
            if (t.type == TokenType::Word) {
                // Peek ahead to see if it's a global function
                Array<Token> declTokens;
                usz tempIdx = idx;
                int parenLevel = 0;
                bool hasParen = false;
                while (tempIdx < tokens.size()) {
                    const Token& tok = tokens[tempIdx];
                    if (tok.text == "(") {
                        parenLevel++;
                        hasParen = true;
                    }
                    else if (tok.text == ")") parenLevel--;

                    if (parenLevel == 0 && (tok.text == ";" || tok.text == "{")) {
                        break;
                    }
                    declTokens.push(tok);
                    tempIdx++;
                }

                // If it is a function signature (has parens and ends with ';' or '{')
                if (hasParen && tempIdx < tokens.size() && (tokens[tempIdx].text == "{" || tokens[tempIdx].text == ";")) {
                    bool skipFn = (lastDocComment.length() == 0);
                    bool isInvalidGlobal = false;
                    for (usz k = 0; k < declTokens.size(); ++k) {
                        if (declTokens[k].text == "operator" || declTokens[k].text == "constexpr" || declTokens[k].text == "using" || declTokens[k].text == "typedef" ||
                            declTokens[k].text == "if" || declTokens[k].text == "for" || declTokens[k].text == "while" || declTokens[k].text == "switch" || declTokens[k].text == "return") {
                            isInvalidGlobal = true;
                        }
                    }

                    if (isInvalidGlobal) {
                        lastDocComment.clear();
                        idx++;
                        continue;
                    }

                    if (skipFn) {
                        if (tempIdx < tokens.size() && tokens[tempIdx].text == "{") {
                            skipMatchingBraces(tokens, tempIdx);
                        } else if (tempIdx < tokens.size() && tokens[tempIdx].text == ";") {
                            tempIdx++;
                        }
                        idx = tempIdx;
                        lastDocComment.clear();
                        continue;
                    }

                    // Proceed to compile matching body if any, and parse function
                    usz bodyIdx = tempIdx;
                    if (bodyIdx < tokens.size() && tokens[bodyIdx].text == "{") {
                        skipMatchingBraces(tokens, bodyIdx);
                    } else if (bodyIdx < tokens.size() && tokens[bodyIdx].text == ";") {
                        bodyIdx++;
                    }

                    // We will update tempIdx so the loop moves past the body at the end
                    tempIdx = bodyIdx;

                } else {
                    lastDocComment.clear();
                    idx++;
                    continue;
                }

                long long parenIdx = -1;
                int angleLevel = 0;
                for (usz k = 0; k < declTokens.size(); ++k) {
                    if (declTokens[k].text == "<") {
                        angleLevel++;
                    } else if (declTokens[k].text == ">") {
                        angleLevel--;
                    } else if (declTokens[k].text == "(" && angleLevel == 0) {
                        parenIdx = (long long)k;
                        break;
                    }
                }

                if (parenIdx >= 0) {
                    // Detect out-of-class method definitions: e.g. "RetType ClassName::method("
                    // The token before the function name (parenIdx-1) should not be preceded by "::" 
                    // unless that qualifier is a known namespace.
                    if ((usz)parenIdx >= 2 && declTokens[(usz)parenIdx - 2].text == "::") {
                        // There's a scope qualifier before the function name.
                        // Build the full qualifier chain to check if it's a namespace.
                        String qualChain;
                        long long qEnd = (long long)parenIdx - 2; // points at the last "::"
                        long long qStart = qEnd;
                        while (qStart >= 1 && declTokens[(usz)qStart].text == "::" && declTokens[(usz)qStart - 1].type == TokenType::Word) {
                            qStart -= 2;
                        }
                        qStart += 1; // first word of qualifier
                        for (long long q = qStart; q <= qEnd; ++q) {
                            qualChain += declTokens[(usz)q].text;
                        }
                        // Check if this qualifier is a known namespace
                        bool isKnownNamespace = false;
                        for (usz nsIdx = 0; nsIdx < namespaces.size(); ++nsIdx) {
                            if (namespaces[nsIdx] == qualChain || qualChain.startsWith(namespaces[nsIdx])) {
                                isKnownNamespace = true;
                                break;
                            }
                        }
                        if (!isKnownNamespace) {
                            // Out-of-class method definition — not a global function, skip it
                            idx = tempIdx;
                            lastDocComment.clear();
                            continue;
                        }
                    }

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

                    // Strip scope qualifier (e.g. ClassName::) immediately before the function name
                    // e.g. for "Array<SelectorPart> TreeItem::parse_selector" the "TreeItem::" is scope, not return type
                    usz returnTypeEndIdx = nameIdx;
                    if (nameIdx >= 2 && declTokens[nameIdx - 1].text == "::") {
                        // Walk backwards past all "Name::" qualifiers
                        long long j = (long long)nameIdx - 1;
                        while (j >= 1 && declTokens[(usz)j].text == "::" && declTokens[(usz)j - 1].type == TokenType::Word) {
                            j -= 2; // skip "::" and the word before it
                        }
                        returnTypeEndIdx = (usz)(j + 1); // exclusive end for return type
                    }

                    for (usz k = 0; k < returnTypeEndIdx; ++k) {
                        if (declTokens[k].text == "inline" || (isDeclSpec(declTokens[k].text) && !(k + 1 < returnTypeEndIdx && declTokens[k + 1].text == "::"))) continue;
                        if (fn.returnType.length() > 0 && !fn.returnType.endsWith("*") && !fn.returnType.endsWith("&") && declTokens[k].text != "*" && declTokens[k].text != "&") {
                            fn.returnType += " ";
                        }
                        fn.returnType += declTokens[k].text;
                    }

                    // Parse parameters similarly...
                    long long closeParenIdx = -1;
                    int pLevel = 0;
                    for (usz k = (usz)parenIdx; k < declTokens.size(); ++k) {
                        if (declTokens[k].text == "(") pLevel++;
                        else if (declTokens[k].text == ")") {
                            pLevel--;
                            if (pLevel == 0) {
                                closeParenIdx = (long long)k;
                                break;
                            }
                        }
                    }

                    if (closeParenIdx >= 0) {
                        Array<Array<Token>> paramGroups;
                        Array<Token> currentParam;
                        int angleLevel = 0;
                        int parenLevel = 0;
                        int bracketLevel = 0;
                        for (usz k = (usz)parenIdx + 1; k < (usz)closeParenIdx; ++k) {
                            const Token& tok = declTokens[k];
                            if (tok.text == "<") angleLevel++;
                            else if (tok.text == ">") angleLevel--;
                            else if (tok.text == "(") parenLevel++;
                            else if (tok.text == ")") parenLevel--;
                            else if (tok.text == "[") bracketLevel++;
                            else if (tok.text == "]") bracketLevel--;

                            if (tok.text == "," && angleLevel == 0 && parenLevel == 0 && bracketLevel == 0) {
                                if (currentParam.size() > 0) {
                                    paramGroups.push(currentParam);
                                    currentParam.clear();
                                }
                            } else {
                                currentParam.push(tok);
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
                                long long openBracketIdx = -1;
                                if (pTokens[limit - 1].text == "]") {
                                    for (long long j = (long long)limit - 1; j >= 0; --j) {
                                        if (pTokens[(usz)j].text == "[") {
                                            openBracketIdx = j;
                                            break;
                                        }
                                    }
                                }

                                if (openBracketIdx > 0) {
                                    param.name = pTokens[(usz)openBracketIdx - 1].text;
                                    for (usz j = 0; j + 1 < (usz)openBracketIdx; ++j) {
                                        if (param.type.length() > 0 && !param.type.endsWith("*") && !param.type.endsWith("&") && pTokens[j].text != "*" && pTokens[j].text != "&") {
                                            param.type += " ";
                                        }
                                        param.type += pTokens[j].text;
                                    }
                                    if (!param.type.endsWith("*")) {
                                        param.type += "*";
                                    }
                                } else {
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
                            }
                            fn.params.push(param);
                        }
                    }

                    // Dedup functions by name+param count
                    bool fnExists = false;
                    for (usz fi = 0; fi < functions.size(); ++fi) {
                        if (functions[fi].name == fn.name && functions[fi].params.size() == fn.params.size()) {
                            fnExists = true; break;
                        }
                    }
                    if (!fnExists) functions.push(fn);
                    idx = tempIdx;
                    continue;
                }
            }

            lastDocComment.clear();
            idx++;
        }
    }
}

Array<ParsedClass> g_allParsedClasses;
std::mutex g_parsedClassesMutex;

} // namespace Sew
