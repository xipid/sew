#pragma once
#include <Collection/String.hpp>
#include <Collection/Array.hpp>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace Sew {

using namespace Collection;

struct ParsedParam {
    String type;
    String name;
    String defaultValue;
};

struct ParsedMethod {
    String name;
    String returnType;
    Array<ParsedParam> params;
    bool isStatic = false;
    bool isConst = false;
    bool isConstructor = false;
    bool isDestructor = false;
    bool isPureVirtual = false;
    String docComment;
};

struct ParsedField {
    String name;
    String type;
    String docComment;
    bool isConst = false;
    bool isStatic = false;
};

struct ParsedClass {
    String name;
    Array<String> parentClasses;
    Array<ParsedMethod> methods;
    Array<ParsedField> fields;
    String docComment;
    bool isStruct = false;
};

struct ParsedFunction {
    String name;
    String returnType;
    Array<ParsedParam> params;
    String docComment;
};

class CppHeaderParser {
public:
    Array<ParsedClass> classes;
    Array<ParsedFunction> functions;
    Array<String> namespaces;

    // Parse the given header file content
    void parse(const String& content);

    // Helper to clean up comments and return clean docstrings
    String cleanDocComment(const String& rawComment);
};

inline CppHeaderParser parseFile(const String& filePath) {
    FILE* f = fopen(filePath.c_str(), "r");
    if (!f) return CppHeaderParser();
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    String content;
    if (size > 0) {
        content.reserve((usz)size);
        char* buf = (char*)malloc(size + 1);
        if (buf) {
            size_t n = fread(buf, 1, size, f);
            buf[n] = '\0';
            content = buf;
            free(buf);
        }
    }
    fclose(f);
    CppHeaderParser parser;
    parser.parse(content);
    return parser;
}

extern Array<ParsedClass> g_allParsedClasses;
extern std::mutex g_parsedClassesMutex;

} // namespace Sew
