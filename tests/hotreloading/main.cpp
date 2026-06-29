#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <Reflection/Reflection.hpp>
#include <Reflection/Global.hpp>
#include <Xi/Log.hpp>
#include <Collection/String.hpp>

using namespace Sew::Reflect;
using namespace Collection;

typedef void (*VoidFn)();
typedef Reflection* (*GetReflectionFn)();

static void compileShared(const char* source_path, const char* out_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "SEW_EXTRA_FLAGS=\"-fPIC -shared -fvisibility=hidden -Wl,--unresolved-symbols=ignore-all\" "
        "./build/sew --no-cache %s -t amd -o %s 2>&1",
        source_path, out_path);
    
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: Compilation failed for %s\n", source_path);
        exit(1);
    }
}

static void writeV1Header() {
    FILE* f = fopen("tests/hotreloading/player.hpp", "w");
    if (f) {
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
}

static void writeV2Header() {
    FILE* f = fopen("tests/hotreloading/player.hpp", "w");
    if (f) {
        fprintf(f,
            "#pragma once\n"
            "class Player {\n"
            "public:\n"
            "    int score  = 0;\n"
            "    int shield = 50;\n"
            "    int health = 0;\n"
            "    void tick() {}\n"
            "};\n");
        fclose(f);
    }
}

int main() {
    printf("Writing V1 Player layout...\n");
    writeV1Header();

    printf("Compiling and loading V1 module...\n");
    compileShared("tests/hotreloading/first.cpp", "/tmp/sew_hotreload_v1.so");

    void* lib1 = dlopen("/tmp/sew_hotreload_v1.so", RTLD_NOW | RTLD_GLOBAL);
    if (!lib1) {
        fprintf(stderr, "dlopen V1 failed: %s\n", dlerror());
        return 1;
    }

    VoidFn start1 = (VoidFn)dlsym(lib1, "start");
    if (start1) start1();

    VoidFn tick1 = (VoidFn)dlsym(lib1, "tick");
    if (tick1) {
        printf("Executing initial V1 state:\n ");
        tick1();
    }

    // mutation
    GetReflectionFn getRefl1 = (GetReflectionFn)dlsym(lib1, "getReflection");
    if (!getRefl1) {
        fprintf(stderr, "getReflection symbol not found in V1 module\n");
        return 1;
    }

    Reflection* pRf = getRefl1();
    if (!pRf) {
        fprintf(stderr, "Reflection pointer is null\n");
        return 1;
    }
    Reflection rf = *pRf;

    printf("Inspecting V1 fields:\n");
    int* healthPtr = (int*)rf.ptr("health");
    int* scorePtr  = (int*)rf.ptr("score");
    if (healthPtr && scorePtr) {
        printf("  health: %d, score: %d\n", *healthPtr, *scorePtr);
        
        // Mutate through reflection API
        int updated_health = 50;
        rf.set("health", &updated_health, sizeof(int));
        printf("Modified health to %d through reflection API\n", *healthPtr);
    }

    if (tick1) {
        printf("Executing modified V1 state:\n ");
        tick1();
    }

    // hot reload
    printf("\nWriting V2 Player layout (fields rearranged, added shield)...\n");
    writeV2Header();

    printf("Compiling and loading V2 module...\n");
    compileShared("tests/hotreloading/second.cpp", "/tmp/sew_hotreload_v2.so");

    void* lib2 = dlopen("/tmp/sew_hotreload_v2.so", RTLD_NOW | RTLD_GLOBAL);
    if (!lib2) {
        fprintf(stderr, "dlopen V2 failed: %s\n", dlerror());
        return 1;
    }

    VoidFn tick2 = (VoidFn)dlsym(lib2, "tick");
    if (tick2) {
        printf("Executing V2 post-migration state:\n ");
        tick2();
    }

    // redirection
    printf("\nDemonstrating pointer redirection via Registry...\n");
    void* v2PlayerAddr = rf.ptr();
    usz origAddr = (usz)v2PlayerAddr;

    struct DummyPlayer { int score; int shield; int health; };
    static DummyPlayer dummy = { 10, 20, 300 };

    ReflectionRegistry::redirect(origAddr, (usz)&dummy);
    void* resolvedAddr = ReflectionRegistry::resolveCast<void*>(origAddr);

    printf("Original target address: %p\n", v2PlayerAddr);
    printf("Redirected target address: %p\n", resolvedAddr);
    printf("Reflection current instance address: %p\n", rf.ptr());

    return 0;
}