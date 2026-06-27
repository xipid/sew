#include <Languages/CPP/CppPreprocessor.hpp>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>

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

static void test_absolute_include_resolved() {
    CppPreprocessor pp;
    pp.includePaths.push("/tmp");

    String source = "#include \"/tmp/example.hpp\"\nint main() {}\n";
    auto result = pp.process(source, "/project/src/main.cpp");

    check("Absolute include path stays absolute", 
          result.localIncludes.size() == 1 && result.localIncludes[0] == "/tmp/example.hpp");
}

static void test_relative_input_path_is_canonicalized() {
    // Create a temporary include directory and header file to verify canonical resolution.
    char tmpDir[] = "/tmp/sew_test_include_XXXXXX";
    if (::mkdtemp(tmpDir) == nullptr) {
        check("Unable to create temp dir", false);
        return;
    }

    String headerPath = String(tmpDir) + "/example.hpp";
    FILE* f = fopen(headerPath.c_str(), "w");
    if (!f) {
        check("Unable to create temp header file", false);
        return;
    }
    fprintf(f, "// temp header\n");
    fclose(f);

    CppPreprocessor pp;
    pp.includePaths.push(tmpDir);

    String source = "#include \"./example.hpp\"\nint main() {}\n";
    auto result = pp.process(source, String(tmpDir) + "/main.cpp");

    bool ok = result.localIncludes.size() == 1 && result.localIncludes[0] == headerPath;
    check("Relative include path resolves to canonical absolute path", ok);

    ::unlink(headerPath.c_str());
    ::rmdir(tmpDir);
}

int main() {
    fprintf(stderr, "\n\033[38;2;0;210;255m\033[1m  Absolute Include Resolution Test\033[0m\n\n");
    test_absolute_include_resolved();
    test_relative_input_path_is_canonicalized();
    fprintf(stderr, "\n  \033[1m%d passed, %d failed\033[0m\n\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
