#include "test_reflection.hpp"
#include <Reflection/Reflection.hpp>
#include <Reflection/Global.hpp>
#include <cstdio>

using namespace Sew;
using namespace Sew::Reflect;
using namespace Collection;

// Method definitions for TestPlayer
void TestPlayer::takeDamage(int amount) {
    score -= amount;
}

int TestPlayer::getScore() {
    return score;
}

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

// ─────────────────────────────────────────────────────────────────────────────
// Simulate what the Sew compiler would inject at file scope:
// The compiler stores a static Reflection for each reflect()-able instance and
// hands the caller a pointer to it.  The user just dereferences the pointer.
// ─────────────────────────────────────────────────────────────────────────────

static TestPlayer g_player;  // the actual live instance
static Reflection g_playerRf = reflect(g_player);  // compiler-injected Reflection
static Reflection* g_playerRfPtr = &g_playerRf;    // pointer the caller receives

int main() {
    fprintf(stderr, "\n\033[38;2;0;210;255m\033[1m  Sew Reflection Tests\033[0m\n\n");

    // Initialise the live instance
    g_player.name  = "Hero";
    g_player.score = 100;

    TestItem item;
    item.id   = 42;
    item.name = "Sword";
    g_player.item = &item;

    // ── Dereference the Reflection pointer ───────────────────────────────────
    // In real Sew-compiled code the caller just does:
    //   Reflection rf = *reflPointer;
    // and uses rf normally.
    Reflection rf = *g_playerRfPtr;

    // 0. ptr() — no args: returns the tracked instance base address
    check("ptr() returns the tracked instance address", rf.ptr() == &g_player);

    // 1. Layout Reflection
    check("Type of score is int",       rf.type("score") == "int");
    check("Type of name is String",     rf.type("name")  == "String");
    check("Type of item is TestItem*",  rf.type("item")  == "TestItem*");

    check("ptr(name) to score matches field address", rf.ptr("score") == &g_player.score);
    check("ptr(name) to name  matches field address", rf.ptr("name")  == &g_player.name);

    int* scorePtr = (int*)rf.ptr("score");
    check("Getting score value via pointer works", *scorePtr == 100);

    // 2. Setting properties
    int newScore = 150;
    rf.set("score", &newScore, sizeof(int));
    check("Setting property value works (score updated)", g_player.score == 150);

    // 3. Sub-reflection — spec: rf.reflect("property")
    Reflection rfItem = rf.reflect("item");
    check("Sub-reflected item ptr() returns item address", rfItem.ptr() == g_player.item);
    check("Sub-reflected item has correct type",            rfItem.type("id")   == "int");
    check("Sub-reflected item has correct field value",     *(int*)rfItem.ptr("id") == 42);
    check("Sub-reflected item has correct String field",    *(String*)rfItem.ptr("name") == "Sword");

    // spec: reflect(rf.ptr("property")) — reflect a field's void* directly
    Reflection rfItemViaPtr = reflect(rf.ptr("item"));  // reflect on the void* stored in item field
    // rfItemViaPtr wraps the TestItem* address; ptr() returns it
    check("reflect(rf.ptr(field)) tracks the field address",
          rfItemViaPtr.ptr() == rf.ptr("item"));

    // 4. Method dispatching
    void* originalDmgFn = rf.originalMethod("takeDamage");
    check("Method metadata lookup works", originalDmgFn != nullptr);

    if (originalDmgFn) {
        typedef void* (*WrapperFn)(void*, void*);
        void* args[1];
        int dmg = 20;
        args[0] = &dmg;
        ((WrapperFn)originalDmgFn)(&g_player, args);
        check("Original method invocation via reflection works", g_player.score == 130);
    }

    // 5. Dynamic method overriding
    bool overrideCalled = false;
    rf.override("takeDamage", [&](void* self, void* args) -> void* {
        overrideCalled = true;
        return nullptr;
    });

    auto* over = ReflectionRegistry::getOverride((usz)&g_player, "takeDamage");
    check("Method override successfully registered", over != nullptr);

    if (over) {
        void* args[1];
        int dmg = 10;
        args[0] = &dmg;
        (*over)(&g_player, args);
        check("Overridden method gets invoked correctly", overrideCalled == true);

        // Verify wrapper dispatch respects overrides
        overrideCalled = false;
        const StructDescriptor* desc = ReflectionRegistry::getStruct("TestPlayer");
        const MethodDescriptor* methodDesc = nullptr;
        if (desc) {
            for (usz i = 0; i < desc->methods.size(); ++i) {
                if (desc->methods[i].name == "takeDamage") {
                    methodDesc = &desc->methods[i];
                    break;
                }
            }
        }
        if (methodDesc) {
            void* args2[1];
            int dmg2 = 10;
            args2[0] = &dmg2;
            typedef void* (*WrapperFn)(void*, void*);
            ((WrapperFn)methodDesc->functionPtr)(&g_player, args2);
            check("Wrapper method dispatch respects runtime overrides", overrideCalled == true);
        }
    }

    // 6. Pointer redirection / hot-reload swizzling
    // When compiled through Sew, raw casts (T*)(addr) are rewritten to
    // ReflectionRegistry::resolveCast<T*>(addr).  We call it directly here.
    usz initialAddr = (usz)&g_player;
    TestPlayer* resolvedInit = ReflectionRegistry::resolveCast<TestPlayer*>(initialAddr);
    check("resolveCast resolves to same address initially", resolvedInit == &g_player);

    TestPlayer player2;
    player2.name  = "New Hero";
    player2.score = 999;

    ReflectionRegistry::redirect(initialAddr, (usz)&player2);

    TestPlayer* resolvedNew = ReflectionRegistry::resolveCast<TestPlayer*>(initialAddr);
    check("resolveCast resolves to redirected address after hot-reload simulation",
          resolvedNew == &player2);
    check("Redirected pointer accesses new instance data", resolvedNew->score == 999);

    // After redirect, rf.ptr() must also follow the table (it calls resolvePointer)
    check("rf.ptr() follows the redirect table after hot-reload",
          rf.ptr() == &player2);

    // 7. Global Variable Registry
    global["Player"] = captureStruct(TestPlayer);
    const StructDescriptor* globalDesc = global["Player"];
    check("Global registry works with captureStruct",
          globalDesc != nullptr && globalDesc->name == "TestPlayer");

    fprintf(stderr, "\n  \033[1m%d passed, %d failed\033[0m\n\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
