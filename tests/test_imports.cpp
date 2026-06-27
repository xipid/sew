/**
 * @file test_imports.cpp
 * @brief Tests for JS and Python import parsers.
 */

#include <Languages/JS/JsLanguage.hpp>
#include <Languages/Python/PyLanguage.hpp>
#include <Sew/Cache.hpp>
#include <cstdio>
#include <unistd.h>

using namespace Sew;
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

static bool hasImport(const Array<ImportSpec>& imports, const String& spec) {
    for (usz i = 0; i < imports.size(); ++i) {
        if (imports[i].specifier == spec) return true;
    }
    return false;
}

// ─── JS Import Tests ────────────────────────────────────────────────────

static void test_js_import_from() {
    JsLanguage js;
    String source =
        "import { foo } from './utils'\n"
        "import bar from '../lib/bar.js'\n"
        "import * as math from 'math'\n";

    auto imports = js.parseImports(source, "src/main.js");

    check("JS: 3 imports found", imports.size() == 3);
    check("JS: import from './utils'", hasImport(imports, "./utils"));
    check("JS: import from '../lib/bar.js'", hasImport(imports, "../lib/bar.js"));
    check("JS: import from 'math'", hasImport(imports, "math"));
}

static void test_js_import_bare() {
    JsLanguage js;
    String source = "import './side-effect.js'\n";
    auto imports = js.parseImports(source, "app.js");

    check("JS: bare import found", imports.size() == 1);
    check("JS: bare import spec", hasImport(imports, "./side-effect.js"));
}

static void test_js_require() {
    JsLanguage js;
    String source =
        "const a = require('lodash')\n"
        "const b = require('./local')\n";
    auto imports = js.parseImports(source, "index.js");

    check("JS: 2 require() found", imports.size() == 2);
    check("JS: require('lodash')", hasImport(imports, "lodash"));
    check("JS: require('./local')", hasImport(imports, "./local"));
}

static void test_js_export_from() {
    JsLanguage js;
    String source = "export { default } from './component'\n";
    auto imports = js.parseImports(source, "index.js");

    check("JS: export from detected", imports.size() == 1);
    check("JS: export from spec", hasImport(imports, "./component"));
}

static void test_js_no_dynamic() {
    JsLanguage js;
    String source =
        "const m = import('./dynamic.js')\n"
        "import static from './static.js'\n";
    auto imports = js.parseImports(source, "app.js");

    // Dynamic import() should NOT be picked up (too ambiguous)
    // But static import should be
    check("JS: static import found", hasImport(imports, "./static.js"));
}

// ─── Python Import Tests ────────────────────────────────────────────────

static void test_py_import() {
    PyLanguage py;
    String source =
        "import os\n"
        "import math\n"
        "import json\n";
    auto imports = py.parseImports(source, "main.py");

    check("Python: 3 imports found", imports.size() == 3);
    check("Python: import os", hasImport(imports, "os"));
    check("Python: import math", hasImport(imports, "math"));
}

static void test_py_from_import() {
    PyLanguage py;
    String source =
        "from utils import helper\n"
        "from os.path import join\n";
    auto imports = py.parseImports(source, "main.py");

    check("Python: 2 from...import found", imports.size() == 2);
    check("Python: from utils", hasImport(imports, "utils"));
    check("Python: from os.path → os/path", hasImport(imports, "os/path"));
}

static void test_py_relative() {
    PyLanguage py;
    String source =
        "from . import utils\n"
        "from .models import User\n";
    auto imports = py.parseImports(source, "app/views.py");

    check("Python: relative imports found", imports.size() >= 2);
}

static void test_py_skip_comments() {
    PyLanguage py;
    String source =
        "# import fake\n"
        "import real\n";
    auto imports = py.parseImports(source, "test.py");

    check("Python: comment line skipped", imports.size() == 1);
    check("Python: real import found", hasImport(imports, "real"));
}

// ─── Cache Tests ─────────────────────────────────────────────────────────

static void test_cache_hash() {
    String a = "hello world";
    String b = "hello world";
    String c = "different";

    String ha = Cache::hashContent(a);
    String hb = Cache::hashContent(b);
    String hc = Cache::hashContent(c);

    check("Cache: same content → same hash", ha == hb);
    check("Cache: different content → different hash", ha != hc);
    check("Cache: hash is 64 hex chars", ha.size() == 64);
}

static void test_cache_key_composite() {
    Array<String> flags1, flags2;
    flags1.push("-O2");
    flags2.push("-O3");

    Array<String> deps;
    deps.push("abc123");

    String k1 = Cache::computeKey("source", "amd", flags1, deps);
    String k2 = Cache::computeKey("source", "amd", flags2, deps);
    String k3 = Cache::computeKey("source", "amd", flags1, deps);

    check("Cache key: same inputs → same key", k1 == k3);
    check("Cache key: different flags → different key", k1 != k2);
}

static void test_cache_readwrite() {
    String key = "test_key_12345678";
    String value = "cached object file data";

    Cache::set(key, value);
    check("Cache: has() after set", Cache::has(key));

    String got = Cache::get(key);
    check("Cache: get returns written value", got == value);

    // Cleanup
    String path = Cache::cacheDir();
    path += "/";
    path += key;
    ::unlink(path.c_str());
}

// ─── Main ────────────────────────────────────────────────────────────────

int main() {
    fprintf(stderr, "\n\033[38;2;0;210;255m\033[1m  Import Parser & Cache Tests\033[0m\n\n");

    fprintf(stderr, "  \033[38;2;189;147;249m\033[1m── JS ──\033[0m\n");
    test_js_import_from();
    test_js_import_bare();
    test_js_require();
    test_js_export_from();
    test_js_no_dynamic();

    fprintf(stderr, "\n  \033[38;2;189;147;249m\033[1m── Python ──\033[0m\n");
    test_py_import();
    test_py_from_import();
    test_py_relative();
    test_py_skip_comments();

    fprintf(stderr, "\n  \033[38;2;189;147;249m\033[1m── Cache ──\033[0m\n");
    test_cache_hash();
    test_cache_key_composite();
    test_cache_readwrite();

    fprintf(stderr, "\n  \033[1m%d passed, %d failed\033[0m\n\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
