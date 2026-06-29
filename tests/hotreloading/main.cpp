#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <Reflection/Reflection.hpp>
#include <Reflection/Global.hpp>
#include <Collection/String.hpp>

using namespace Sew::Reflect;
using namespace Collection;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static int passed = 0;
static int failed = 0;
typedef void (*VoidFn)();

static void check(const char* name, bool cond) {
    if (cond) {
        fprintf(stderr, "  \033[38;2;80;250;123m✓\033[0m %s\n", name);
        passed++;
    } else {
        fprintf(stderr, "  \033[38;2;255;85;85m✗\033[0m %s\n", name);
        failed++;
    }
}

// Compile orig + temp together into a shared library at out.
// Uses the sew binary that lives next to the build directory.
// Compile a source file into a shared library using the sew compiler.
// All template traits, descriptors, and global registry hooks are
// generated and registered automatically by the sew compiler!
static void compileShared(const char* orig, const char* out) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "SEW_EXTRA_FLAGS=\"-fPIC -shared -fvisibility=hidden -Wl,--unresolved-symbols=ignore-all\" "
        "./build/sew --no-cache %s -t amd -o %s 2>&1",
        orig, out);
    int res = system(cmd);
    if (res != 0) {
        fprintf(stderr, "  [!] Sew compilation failed: %s -> %s\n", orig, out);
        exit(1);
    }
}

static void writeToFile(const char* path, const String& content) {
    FILE* f = fopen(path, "w");
    if (f) {
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// V1 / V2 player.hpp writers
// ─────────────────────────────────────────────────────────────────────────────

static void writeV1Header() {
    FILE* f = fopen("tests/hotreloading/player.hpp", "w");
    if (!f) return;
    fprintf(f,
        "#pragma once\n"
        "class Player {\n"
        "public:\n"
        "    int health = 100;\n"
        "    int score  = 0;\n"
        "    void tick() {}\n"
        "};\n");
    fclose(f);
}

static void writeV2Header() {
    FILE* f = fopen("tests/hotreloading/player.hpp", "w");
    if (!f) return;
    fprintf(f,
        "#pragma once\n"
        "class Player {\n"
        "public:\n"
        "    int score  = 0;\n"   // field order changed
        "    int shield = 50;\n"  // new field
        "    int health = 0;\n"
        "    void tick() {}\n"
        "};\n");
    fclose(f);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    fprintf(stderr, "\n\033[38;2;0;210;255m\033[1m  Sew Reflection Hot-Reloading Tests\033[0m\n\n");

    // ── Phase 1: compile & load V1 ───────────────────────────────────────────
    fprintf(stderr, "  \033[1m[1] V1 module\033[0m\n");
    writeV1Header();

    compileShared(
        "/home/xi/Repo/sew/tests/hotreloading/first.cpp",
        "/tmp/sew_hotreload_v1.so");

    void* lib1 = dlopen("/tmp/sew_hotreload_v1.so", RTLD_NOW | RTLD_GLOBAL);
    if (!lib1) { fprintf(stderr, "  dlopen V1: %s\n", dlerror()); return 1; }

    // start() creates the Player instance and stores its Reflection
    VoidFn start1 = (VoidFn)dlsym(lib1, "start");
    if (start1) start1();

    // Call tick() of V1 gameplay module
    VoidFn tick1 = (VoidFn)dlsym(lib1, "tick");
    if (tick1) {
        fprintf(stderr, "  Calling V1 tick(): ");
        tick1();
    }

    // ── Call getReflection() ──────────────────────────────────────────────────
    // The module exports `Reflection* getReflection()` — we call it to retrieve
    // the pointer to the static Reflection, then dereference it.
    typedef Reflection* (*GetReflectionFn)();
    GetReflectionFn getRefl1 = (GetReflectionFn)dlsym(lib1, "getReflection");
    if (!getRefl1) {
        fprintf(stderr, "  [!] getReflection not found in V1 module\n");
        return 1;
    }
    Reflection* pRf = getRefl1();
    if (!pRf) {
        fprintf(stderr, "  [!] Reflection pointer is null in V1 module\n");
        return 1;
    }
    Reflection rf = *pRf;   // dereference V1 Reflection

    // ptr() — base address of the Player instance
    void* v1PlayerAddr = rf.ptr();
    check("V1: rf.ptr() returns non-null instance address", v1PlayerAddr != nullptr);

    // Read fields via ptr(name)
    int* healthPtr = (int*)rf.ptr("health");
    int* scorePtr  = (int*)rf.ptr("score");
    check("V1: ptr(\"health\") resolves correctly", healthPtr != nullptr);
    check("V1: ptr(\"score\")  resolves correctly", scorePtr  != nullptr);
    check("V1: health == 100 (default)",  healthPtr && *healthPtr == 100);
    check("V1: score  == 0   (default)",  scorePtr  && *scorePtr  == 0);

    // Mutate health to half via set()
    int half = *healthPtr / 2;   // 50
    rf.set("health", &half, sizeof(int));
    check("V1: set(\"health\", 50) written through reflection", *healthPtr == 50);

    // Call V1 tick() again to check it prints the halved health
    if (tick1) {
        fprintf(stderr, "  Calling V1 tick() after reflection change: ");
        tick1();
    }

    // list() enumerates fields
    Array<String> fields = rf.list().keys();
    check("V1: list() returns 2 fields", fields.size() == 2);

    // type() per field
    check("V1: type(\"health\") == \"int\"", rf.type("health") == "int");
    check("V1: type(\"score\")  == \"int\"", rf.type("score")  == "int");

    // ── Phase 2: compile & load V2, trigger hot-reload ───────────────────────
    fprintf(stderr, "\n  \033[1m[2] V2 module (hot-reload)\033[0m\n");
    writeV2Header();

    compileShared(
        "/home/xi/Repo/sew/tests/hotreloading/second.cpp",
        "/tmp/sew_hotreload_v2.so");

    void* lib2 = dlopen("/tmp/sew_hotreload_v2.so", RTLD_NOW | RTLD_GLOBAL);
    if (!lib2) { fprintf(stderr, "  dlopen V2: %s\n", dlerror()); return 1; }

    // start2 is looked up but NOT called!
    VoidFn start2 = (VoidFn)dlsym(lib2, "start");
    check("V2: start() is present in V2 module", start2 != nullptr);

    // Call tick() of V2 gameplay module (which uses migrated data!)
    VoidFn tick2 = (VoidFn)dlsym(lib2, "tick");
    if (tick2) {
        fprintf(stderr, "  Calling V2 tick() post-migration: ");
        tick2();
    }

    // The old V1 reflection rf dynamically redirects to the migrated V2 layout!
    void* v2PlayerAddr = rf.ptr();
    check("V2: rf.ptr() returns migrated instance address", v2PlayerAddr != nullptr);
    check("V2: V2 instance is at a different address than V1", v2PlayerAddr != v1PlayerAddr);

    // V2 has three fields: score, shield, health
    Array<String> fields2 = rf.list().keys();
    check("V2: list() returns 3 fields", fields2.size() == 3);

    check("V2: type(\"score\")  == \"int\"", rf.type("score")  == "int");
    check("V2: type(\"shield\") == \"int\"", rf.type("shield") == "int");
    check("V2: type(\"health\") == \"int\"", rf.type("health") == "int");

    // V2 migrated and default values
    int* v2ScorePtr  = (int*)rf.ptr("score");
    int* v2ShieldPtr = (int*)rf.ptr("shield");
    int* v2HealthPtr = (int*)rf.ptr("health");
    check("V2: score  == 0  (default)", v2ScorePtr  && *v2ScorePtr  == 0);
    check("V2: shield == 50 (default)", v2ShieldPtr && *v2ShieldPtr == 50);
    check("V2: health == 50 (migrated)", v2HealthPtr && *v2HealthPtr == 50);

    // ── Phase 3: redirect / resolveCast ──────────────────────────────────────
    fprintf(stderr, "\n  \033[1m[3] Pointer redirect (resolveCast)\033[0m\n");

    usz origAddr = (usz)v2PlayerAddr;
    // allocate a minimal stand-in
    struct MinPlayer { int score; int shield; int health; };
    static MinPlayer mp; mp.score = 7; mp.shield = 3; mp.health = 999;

    ReflectionRegistry::redirect(origAddr, (usz)&mp);
    check("redirect: resolveCast returns new address",
          ReflectionRegistry::resolveCast<void*>(origAddr) == (void*)&mp);

    // rf.ptr() now follows the redirect
    check("V1 rf.ptr() follows redirect after ReflectionRegistry::redirect",
          rf.ptr() == (void*)&mp);

    // ── Clean up ─────────────────────────────────────────────────────────────
    fprintf(stderr, "\n  \033[1m[4] Cleanup\033[0m\n");
    dlclose(lib1);
    dlclose(lib2);

    fprintf(stderr, "\n  \033[1m%d passed, %d failed\033[0m\n\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

