#pragma once
#include <Reflection/Type.hpp>
#include <Collection/Map.hpp>
#include <Xi/Func.hpp>

namespace Sew { namespace Reflect {

class ReflectionRegistry {
public:
    static Map<String, StructDescriptor>& structs();
    static Map<String, StructDescriptor>& oldStructs();
    static Map<usz, String>& activeInstances();
    static Map<usz, usz>& redirectionTable();
    static Map<usz, Map<String, Xi::Func<void*(void*, void*)>>>& methodOverrides();

    static void registerStruct(const StructDescriptor& desc) {
        const StructDescriptor* oldDesc = getStruct(desc.name);
        if (oldDesc) {
            oldStructs().set(desc.name, *oldDesc);
        }
        structs().set(desc.name, desc);
    }

    static const StructDescriptor* getStruct(const String& name) {
        return structs().get(name);
    }

    static const StructDescriptor* getOldStruct(const String& name) {
        return oldStructs().get(name);
    }

    static Map<String, usz>& globalVariables();
    static void registerVariable(const String& name, void* ptr);
    static Map<usz, bool>& heapInstances();

    static void registerInstance(const String& typeName, void* ptr);
    static void deregisterInstance(void* ptr);
    static void* create(const String& typeName);
    static void destroy(void* ptr);
    static void hotReload();

    static bool& hasRedirections() {
        static bool s_has = false;
        return s_has;
    }

    static usz resolvePointer(usz addr) {
        if (!hasRedirections() || addr == 0) return addr;
        usz current = addr;
        for (int i = 0; i < 32; ++i) { // prevent infinite cycles in case of bugs
            usz* redirected = redirectionTable().get(current);
            if (!redirected) break;
            current = *redirected;
        }
        return current;
    }

    template<typename TargetType>
    static TargetType resolveCast(usz addr) {
        return (TargetType)resolvePointer(addr);
    }

    template<typename TargetType, typename SourceType>
    static TargetType resolveCast(SourceType ptr) {
        return (TargetType)resolvePointer((usz)ptr);
    }

    static void redirect(usz oldAddr, usz newAddr) {
        if (oldAddr == 0 || oldAddr == newAddr) return;
        
        // 1. Path compression: resolve newAddr to its final destination
        usz finalAddr = resolvePointer(newAddr);
        
        // 2. Collapse any existing redirection paths pointing to oldAddr
        for (auto it = redirectionTable().begin(); it != redirectionTable().end(); ++it) {
            if (it->value == oldAddr) {
                it->value = finalAddr;
            }
        }
        
        // 3. Set the new redirect
        redirectionTable().set(oldAddr, finalAddr);
        hasRedirections() = true;
    }

    static void registerOverride(usz instanceAddr, const String& methodName, Xi::Func<void*(void*, void*)> func) {
        Map<String, Xi::Func<void*(void*, void*)>>* instanceMap = methodOverrides().get(instanceAddr);
        if (!instanceMap) {
            Map<String, Xi::Func<void*(void*, void*)>> m;
            methodOverrides().set(instanceAddr, m);
            instanceMap = methodOverrides().get(instanceAddr);
        }
        instanceMap->set(methodName, func);
    }

    static Xi::Func<void*(void*, void*)>* getOverride(usz instanceAddr, const String& methodName) {
        Map<String, Xi::Func<void*(void*, void*)>>* instanceMap = methodOverrides().get(instanceAddr);
        if (instanceMap) {
            return instanceMap->get(methodName);
        }
        return nullptr;
    }
};

}} // namespace Sew::Reflect
