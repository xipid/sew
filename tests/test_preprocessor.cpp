/**
 * @file test_preprocessor.cpp
 * @brief Tests for the C++ preprocessor.
 */

#include <Languages/CPP/CppPreprocessor.hpp>
#include <cstdio>

using namespace Sew::Languages;
using namespace Collection;

static int passed = 0;
static int failed = 0;

static void check(const char* name, bool cond) {
    if (cond) {
        fprintf(stderr, "  \033[38;2;80;250;123m✓\033[0m %s\n", name);
        passed++;
    } else {
        fprintf(stderr, "  \033[38;2;255;85;85m✗\033[0m %s\n", name);
        failed++;
    }
}

// ─── Test: Basic #define tracking ────────────────────────────────────────

static void test_define() {
    CppPreprocessor pp;
    String source = "#define FOO\n#define BAR 42\nint x = BAR;\n";
    auto result = pp.process(source, "test.cpp");

    check("#define FOO tracked", result.defines.has("FOO"));
    check("#define BAR tracked", result.defines.has("BAR"));
    const MacroDef* bar = result.defines.get("BAR");
    check("#define BAR value is 42", bar && bar->value == "42");
    check("No errors", result.errors.size() == 0);
}

// ─── Test: #undef ────────────────────────────────────────────────────────

static void test_undef() {
    CppPreprocessor pp;
    String source = "#define FOO\n#undef FOO\nint x;\n";
    auto result = pp.process(source, "test.cpp");

    check("#undef removes FOO", !result.defines.has("FOO"));
}

// ─── Test: #ifdef / #ifndef ──────────────────────────────────────────────

static void test_ifdef() {
    CppPreprocessor pp;
    pp.predefined.set("LINUX", MacroDef{"LINUX", "1", {}, false});

    String source =
        "#ifdef LINUX\n"
        "int linux_code = 1;\n"
        "#endif\n"
        "#ifdef WINDOWS\n"
        "int windows_code = 1;\n"
        "#endif\n"
        "int common = 1;\n";

    auto result = pp.process(source, "test.cpp");

    check("#ifdef LINUX: linux_code included",
          result.strippedSource.indexOf("linux_code") >= 0);
    check("#ifdef WINDOWS: windows_code excluded",
          result.strippedSource.indexOf("windows_code") < 0);
    check("common code always included",
          result.strippedSource.indexOf("common") >= 0);
}

static void test_ifndef() {
    CppPreprocessor pp;

    String source =
        "#ifndef MY_HEADER_H\n"
        "#define MY_HEADER_H\n"
        "int guarded = 1;\n"
        "#endif\n";

    auto result = pp.process(source, "test.h");

    check("#ifndef include guard works",
          result.strippedSource.indexOf("guarded") >= 0);
    check("MY_HEADER_H defined after guard",
          result.defines.has("MY_HEADER_H"));
}

// ─── Test: #if / #elif / #else ───────────────────────────────────────────

static void test_if_elif_else() {
    CppPreprocessor pp;
    pp.predefined.set("VERSION", MacroDef{"VERSION", "3", {}, false});

    String source =
        "#if VERSION == 1\n"
        "int v1 = 1;\n"
        "#elif VERSION == 2\n"
        "int v2 = 1;\n"
        "#elif VERSION == 3\n"
        "int v3 = 1;\n"
        "#else\n"
        "int vother = 1;\n"
        "#endif\n";

    auto result = pp.process(source, "test.cpp");

    check("#if VERSION==3: v3 included",
          result.strippedSource.indexOf("v3") >= 0);
    check("#if VERSION==3: v1 excluded",
          result.strippedSource.indexOf("v1") < 0);
    check("#if VERSION==3: v2 excluded",
          result.strippedSource.indexOf("v2") < 0);
    check("#if VERSION==3: vother excluded",
          result.strippedSource.indexOf("vother") < 0);
}

// ─── Test: Nested conditionals ───────────────────────────────────────────

static void test_nested_conditionals() {
    CppPreprocessor pp;
    pp.predefined.set("A", MacroDef{"A", "1", {}, false});

    String source =
        "#ifdef A\n"
        "  #ifdef B\n"
        "  int ab = 1;\n"
        "  #else\n"
        "  int a_not_b = 1;\n"
        "  #endif\n"
        "#else\n"
        "  int not_a = 1;\n"
        "#endif\n";

    auto result = pp.process(source, "test.cpp");

    check("Nested: A defined, B not → a_not_b included",
          result.strippedSource.indexOf("a_not_b") >= 0);
    check("Nested: ab excluded",
          result.strippedSource.indexOf("ab") < 0);
    check("Nested: not_a excluded",
          result.strippedSource.indexOf("not_a") < 0);
}

// ─── Test: #if with defined() ────────────────────────────────────────────

static void test_defined_expr() {
    CppPreprocessor pp;
    pp.predefined.set("X", MacroDef{"X", "", {}, false});

    String source =
        "#if defined(X) && !defined(Y)\n"
        "int x_only = 1;\n"
        "#endif\n"
        "#if defined(X) || defined(Y)\n"
        "int x_or_y = 1;\n"
        "#endif\n"
        "#if defined(Y)\n"
        "int y_only = 1;\n"
        "#endif\n";

    auto result = pp.process(source, "test.cpp");

    check("defined(X) && !defined(Y) → x_only included",
          result.strippedSource.indexOf("x_only") >= 0);
    check("defined(X) || defined(Y) → x_or_y included",
          result.strippedSource.indexOf("x_or_y") >= 0);
    check("defined(Y) → y_only excluded",
          result.strippedSource.indexOf("y_only") < 0);
}

// ─── Test: #if with integer comparisons ──────────────────────────────────

static void test_if_comparisons() {
    CppPreprocessor pp;
    pp.predefined.set("VER", MacroDef{"VER", "17", {}, false});

    String source =
        "#if VER >= 14\n"
        "int cpp14 = 1;\n"
        "#endif\n"
        "#if VER < 20\n"
        "int below20 = 1;\n"
        "#endif\n"
        "#if VER == 17\n"
        "int exact17 = 1;\n"
        "#endif\n"
        "#if VER != 14\n"
        "int not14 = 1;\n"
        "#endif\n";

    auto result = pp.process(source, "test.cpp");

    check("VER(17) >= 14 → true", result.strippedSource.indexOf("cpp14") >= 0);
    check("VER(17) < 20 → true", result.strippedSource.indexOf("below20") >= 0);
    check("VER(17) == 17 → true", result.strippedSource.indexOf("exact17") >= 0);
    check("VER(17) != 14 → true", result.strippedSource.indexOf("not14") >= 0);
}

// ─── Test: #include resolution ───────────────────────────────────────────

static void test_include() {
    CppPreprocessor pp;

    String source =
        "#include \"utils.hpp\"\n"
        "#include <cstdio>\n"
        "#include \"../lib/math.h\"\n"
        "int main() {}\n";

    auto result = pp.process(source, "/project/src/main.cpp");

    check("#include \"utils.hpp\" → local include found",
          result.localIncludes.size() >= 1);
    check("#include <cstdio> → system include found",
          result.systemIncludes.size() >= 1);
    check("Local include path resolved relative to current file",
          result.localIncludes.size() > 0 &&
          result.localIncludes[0].indexOf("utils.hpp") >= 0);
    check("System include keeps <cstdio> in stripped output",
          result.strippedSource.indexOf("cstdio") >= 0);
    check("Local #include stripped from output",
          result.strippedSource.indexOf("#include \"utils.hpp\"") < 0);
}

// ─── Test: #pragma once ──────────────────────────────────────────────────

static void test_pragma_once() {
    CppPreprocessor pp;

    String source = "#pragma once\nint x = 1;\n";

    // First process
    auto result1 = pp.process(source, "/test/header.hpp");
    check("#pragma once: first include has content",
          result1.strippedSource.indexOf("int x") >= 0);

    // Second process of same file
    auto result2 = pp.process(source, "/test/header.hpp");
    check("#pragma once: second include returns empty",
          result2.strippedSource.isEmpty());
}

// ─── Test: #error and #warning ───────────────────────────────────────────

static void test_error_warning() {
    CppPreprocessor pp;

    String source =
        "#warning \"This is a warning\"\n"
        "#ifdef MISSING\n"
        "#error \"Should not trigger\"\n"
        "#endif\n"
        "int ok = 1;\n";

    auto result = pp.process(source, "test.cpp");

    check("#warning produces warning", result.warnings.size() == 1);
    check("#error in dead branch not triggered", result.errors.size() == 0);
    check("Code after directives included",
          result.strippedSource.indexOf("ok") >= 0);
}

static void test_error_active() {
    CppPreprocessor pp;

    String source =
        "#error \"Build stopped\"\n"
        "int code = 1;\n";

    auto result = pp.process(source, "test.cpp");

    check("#error in active branch triggers", result.errors.size() == 1);
}

// ─── Test: Function-like macro tracking ──────────────────────────────────

static void test_function_macro() {
    CppPreprocessor pp;

    String source = "#define MAX(a, b) ((a) > (b) ? (a) : (b))\nint x;\n";
    auto result = pp.process(source, "test.cpp");

    check("Function-like macro tracked", result.defines.has("MAX"));
    const MacroDef* m = result.defines.get("MAX");
    check("Function-like macro has params",
          m && m->isFunctionLike && m->params.size() == 2);
}

// ─── Test: #if with hex literals ─────────────────────────────────────────

static void test_hex_literal() {
    CppPreprocessor pp;
    pp.predefined.set("FLAG", MacroDef{"FLAG", "0xFF", {}, false});

    String source =
        "#if FLAG == 0xFF\n"
        "int hex_match = 1;\n"
        "#endif\n"
        "#if FLAG > 0xFE\n"
        "int hex_gt = 1;\n"
        "#endif\n";

    auto result = pp.process(source, "test.cpp");

    check("Hex literal 0xFF == 0xFF", result.strippedSource.indexOf("hex_match") >= 0);
    check("Hex literal 0xFF > 0xFE", result.strippedSource.indexOf("hex_gt") >= 0);
}

// ─── Test: Directive stripping ───────────────────────────────────────────

static void test_stripping() {
    CppPreprocessor pp;

    String source =
        "#ifdef __linux__\n"
        "int linux = 1;\n"
        "#endif\n"
        "#define FOO 1\n"
        "int bar = FOO;\n";

    auto result = pp.process(source, "test.cpp");

    // Conditionals should be stripped
    check("#ifdef stripped", result.strippedSource.indexOf("#ifdef") < 0);
    check("#endif stripped", result.strippedSource.indexOf("#endif") < 0);
    // #define should be KEPT (clang needs it)
    check("#define kept for clang", result.strippedSource.indexOf("#define FOO") >= 0);
}

// ─── Main ────────────────────────────────────────────────────────────────

int main() {
    fprintf(stderr, "\n\033[38;2;0;210;255m\033[1m  C++ Preprocessor Tests\033[0m\n\n");

    test_define();
    test_undef();
    test_ifdef();
    test_ifndef();
    test_if_elif_else();
    test_nested_conditionals();
    test_defined_expr();
    test_if_comparisons();
    test_include();
    test_pragma_once();
    test_error_warning();
    test_error_active();
    test_function_macro();
    test_hex_literal();
    test_stripping();

    fprintf(stderr, "\n  \033[1m%d passed, %d failed\033[0m\n\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
