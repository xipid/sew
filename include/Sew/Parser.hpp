#pragma once
#include <Collection/String.hpp>
#include <Collection/Array.hpp>

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

private:
    // Helper to clean up comments and return clean docstrings
    String cleanDocComment(const String& rawComment);
};

} // namespace Sew
