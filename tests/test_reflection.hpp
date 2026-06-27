#pragma once
#include <Collection/String.hpp>
#include <Reflection/Reflection.hpp>
#include <cstddef>

// Suppress Clang's -Winvalid-offsetof for non-standard-layout types.
// These offsets are correct in practice; the Sew compiler would derive them
// from the AST at parse time rather than using the offsetof macro.
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Winvalid-offsetof"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

// Helper: byte offset of a field via a null-pointer reinterpret
// Used identically to offsetof but avoids the macro warning path.
#define SEW_FIELD_OFFSET(Type, Field) \
    ((usz)(const char*)&(((Type*)nullptr)->Field) - (usz)nullptr)

using namespace Collection;
using namespace Sew::Reflect;

struct TestItem {
    int id;
    String name;
};

struct TestPlayer {
    String name;
    int score;
    TestItem* item;

    void takeDamage(int amount);
    int getScore();
};

// ─────────────────────────────────────────────────────────────────────────────
// Reflection type traits — normally emitted by the Sew compiler when it sees
// reflect(someVar) called on a type.  Here we provide them manually so the
// test binary contains all the metadata needed without compiler rewriting.
// ─────────────────────────────────────────────────────────────────────────────

namespace Sew { namespace Reflect {

// ── TestItem wrapper ─────────────────────────────────────────────────────────

template<>
struct ReflectionTypeTraits<TestItem> {
    static constexpr const char* name() { return "TestItem"; }

    static const StructDescriptor* descriptor() {
        static bool initialized = false;
        static StructDescriptor desc;
        if (!initialized) {
            initialized = true;
            desc.name = "TestItem";
            desc.size = sizeof(TestItem);

            {
                FieldDescriptor f;
                f.name   = "id";
                f.kind   = TypeKind::Int;
                f.offset = offsetof(TestItem, id);
                f.size   = sizeof(int);
                desc.fields.push(f);
            }
            {
                FieldDescriptor f;
                f.name   = "name";
                f.kind   = TypeKind::String;
                f.offset = offsetof(TestItem, name);
                f.size   = sizeof(String);
                desc.fields.push(f);
            }

            desc.factory = []() -> void* { return new TestItem(); };
            desc.destroy  = [](void* p) { delete static_cast<TestItem*>(p); };

            ReflectionRegistry::registerStruct(desc);
        }
        return ReflectionRegistry::getStruct("TestItem");
    }
};

// ── TestPlayer method wrappers ────────────────────────────────────────────────
//
// Each wrapper has signature  void*(void* self, void* args)
// where args is a void*[] of pointers to each argument, matching the test:
//     void* args[1]; args[0] = &dmg;
//     ((WrapperFn)originalDmgFn)(&player, args);

static void* _TestPlayer_takeDamage_wrapper(void* self, void* argsRaw) {
    void** args = static_cast<void**>(argsRaw);
    // Check for instance-level override first
    usz addr = (usz)self;
    auto* over = ReflectionRegistry::getOverride(addr, "takeDamage");
    if (over) {
        return (*over)(self, argsRaw);
    }
    int amount = *static_cast<int*>(args[0]);
    static_cast<TestPlayer*>(self)->takeDamage(amount);
    return nullptr;
}

static void* _TestPlayer_getScore_wrapper(void* self, void* /*argsRaw*/) {
    static int result;
    // Check for instance-level override first
    usz addr = (usz)self;
    auto* over = ReflectionRegistry::getOverride(addr, "getScore");
    if (over) {
        return (*over)(self, nullptr);
    }
    result = static_cast<TestPlayer*>(self)->getScore();
    return &result;
}

// ── TestPlayer traits ─────────────────────────────────────────────────────────

template<>
struct ReflectionTypeTraits<TestPlayer> {
    static constexpr const char* name() { return "TestPlayer"; }

    static const StructDescriptor* descriptor() {
        static bool initialized = false;
        static StructDescriptor desc;
        if (!initialized) {
            initialized = true;

            // Make sure TestItem is registered first
            ReflectionTypeTraits<TestItem>::descriptor();

            desc.name = "TestPlayer";
            desc.size = sizeof(TestPlayer);

            // Fields
            {
                FieldDescriptor f;
                f.name   = "name";
                f.kind   = TypeKind::String;
                f.offset = offsetof(TestPlayer, name);
                f.size   = sizeof(String);
                desc.fields.push(f);
            }
            {
                FieldDescriptor f;
                f.name   = "score";
                f.kind   = TypeKind::Int;
                f.offset = offsetof(TestPlayer, score);
                f.size   = sizeof(int);
                desc.fields.push(f);
            }
            {
                FieldDescriptor f;
                f.name           = "item";
                f.kind           = TypeKind::Pointer;
                f.offset         = offsetof(TestPlayer, item);
                f.size           = sizeof(TestItem*);
                f.customTypeName = "TestItem";
                desc.fields.push(f);
            }

            // Methods
            {
                MethodDescriptor m;
                m.name        = "takeDamage";
                m.returnType  = "void";
                m.paramTypes.push("int");
                m.functionPtr = (void*)&_TestPlayer_takeDamage_wrapper;
                desc.methods.push(m);
            }
            {
                MethodDescriptor m;
                m.name        = "getScore";
                m.returnType  = "int";
                m.functionPtr = (void*)&_TestPlayer_getScore_wrapper;
                desc.methods.push(m);
            }

            desc.factory = []() -> void* { return new TestPlayer(); };
            desc.destroy  = [](void* p) { delete static_cast<TestPlayer*>(p); };

            ReflectionRegistry::registerStruct(desc);
        }
        return ReflectionRegistry::getStruct("TestPlayer");
    }
};

}} // namespace Sew::Reflect

// Restore previous diagnostic settings
#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
