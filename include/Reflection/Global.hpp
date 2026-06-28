#pragma once
#include <Reflection/Registry.hpp>
#include <Reflection/Reflection.hpp>
#include <Collection/Map.hpp>

namespace Sew { namespace Reflect {

struct GlobalValue {
    enum class Kind {
        None,
        Pointer,
        Struct
    };

    Kind kind = Kind::None;
    void* ptr = nullptr;
    const StructDescriptor* structDesc = nullptr;

    GlobalValue() = default;
    GlobalValue(void* p) : kind(Kind::Pointer), ptr(p) {}
    GlobalValue(const StructDescriptor* desc) : kind(Kind::Struct), structDesc(desc) {}
};

class GlobalRegistry {
public:
    static Map<String, GlobalValue>& values() {
        static Map<String, GlobalValue> s_values;
        return s_values;
    }

    struct Proxy {
        String key;

        Proxy& operator=(void* ptr) {
            reflect(ptr);
            values().set(key, GlobalValue(ptr));
            return *this;
        }

        Proxy& operator=(const char* str) {
            return *this = String(str);
        }

        template<typename T>
        Proxy& operator=(T* ptr) {
            reflect(ptr);
            values().set(key, GlobalValue((void*)ptr));
            return *this;
        }

        Proxy& operator=(const StructDescriptor* desc) {
            values().set(key, GlobalValue(desc));
            return *this;
        }

        Proxy& operator=(const String& structName) {
            const StructDescriptor* oldDesc = ReflectionRegistry::getOldStruct(structName);
            const StructDescriptor* desc = ReflectionRegistry::getStruct(structName);
            values().set(key, GlobalValue(desc));

            if (oldDesc) {
                ReflectionRegistry::hotReload();
            }
            return *this;
        }

        operator void*() const {
            GlobalValue* val = values().get(key);
            return val && val->kind == GlobalValue::Kind::Pointer ? val->ptr : nullptr;
        }

        operator const StructDescriptor*() const {
            GlobalValue* val = values().get(key);
            return val && val->kind == GlobalValue::Kind::Struct ? val->structDesc : nullptr;
        }
    };

    Proxy operator[](const String& key) {
        return Proxy{key};
    }
};

// Global instance variable
inline GlobalRegistry global;

}} // namespace Sew::Reflect

#define captureStruct(Type) (Sew::Reflect::ReflectionTypeTraits<Type>::descriptor(), #Type)
