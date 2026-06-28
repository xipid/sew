#pragma once
#include <Reflection/Registry.hpp>
#include <Collection/Array.hpp>

namespace Sew { namespace Reflect {

class __attribute__((visibility("default"))) Reflection {
private:
    void* _instance = nullptr;
    const StructDescriptor* _desc = nullptr;
    bool _isPrototype = false;

    // For prototype-based dynamic properties
    struct DynamicProp {
        String name;
        Array<u8> data;
        String typeName;
    };

    Map<String, DynamicProp>* _dynamicProps = nullptr;

public:
    Reflection();
    Reflection(void* inst, const StructDescriptor* desc);
    Reflection(void* inst, const String& typeName);

    ~Reflection();

    // Copy semantics
    Reflection(const Reflection& o);
    Reflection& operator=(const Reflection& o);

    // Returns the raw pointer to the reflected instance itself.
    // In the spec: reflect(rf.ptr()) or storing the address before field access.
    void* ptr() const;

    String type(const String& name) const;
    void* ptr(const String& name) const;
    Reflection reflect(const String& name) const;
    Array<String> list() const;
    void reset(const String& name);
    void set(const String& name, const void* bytes, usz length);

    void* originalMethod(const String& name) const;

    void* call(const String& methodName, void** args = nullptr) const {
        void* method = originalMethod(methodName);
        if (method) {
            typedef void* (*WrapperFn)(void*, void*);
            return ((WrapperFn)method)(_instance, args);
        }
        return nullptr;
    }

    template<typename F>
    void override(const String& methodName, F lambda) {
        usz instAddr = (usz)_instance;
        if (_isPrototype) instAddr = (usz)this;

        ReflectionRegistry::registerOverride(instAddr, methodName, Xi::Func<void*(void*, void*)>(
            [lambda](void* self, void* args) -> void* {
                return (void*)lambda(self, args);
            }
        ));
    }
};

// Helper for type trait resolution
template<typename T>
struct ReflectionTypeTraits {
    static constexpr const char* name() { return "Unknown"; }
    static const StructDescriptor* descriptor() { return nullptr; }
};

template<typename T>
inline Reflection reflect(T& val) {
    ReflectionRegistry::registerInstance(ReflectionTypeTraits<T>::name(), (void*)&val);
    usz resolved = ReflectionRegistry::resolvePointer((usz)&val);
    return Reflection(
        (void*)resolved,
        ReflectionTypeTraits<T>::descriptor()
    );
}

template<typename T>
inline Reflection reflect(T* val) {
    if (val) {
        ReflectionRegistry::registerInstance(ReflectionTypeTraits<T>::name(), (void*)val);
    }
    usz resolved = ReflectionRegistry::resolvePointer((usz)val);
    return Reflection(
        (void*)resolved,
        ReflectionTypeTraits<T>::descriptor()
    );
}

// reflect(rf.ptr("property")) — reflect a void* whose type is looked up in the
// registry by address.  Used by the spec's: reflect(rf.ptr("property")).
inline Reflection reflect(void* val) {
    usz resolved = ReflectionRegistry::resolvePointer((usz)val);
    // Type is unknown without a trait; returns an untyped (prototype) Reflection.
    return Reflection((void*)resolved, static_cast<const StructDescriptor*>(nullptr));
}

// reflect() — prototype / empty reflection for constructing new structs.
inline Reflection reflect() {
    return Reflection();
}

}} // namespace Sew::Reflect
